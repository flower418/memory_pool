#include "../include/MemoryPool.h"

MemoryPool::MemoryPool(size_t BlockSize) : blockSize_(BlockSize) {}
MemoryPool::~MemoryPool() {
    // 把连续的 block 都删掉
    Slot *cur = firstBlock_;
    while (cur) {
        Slot *next = cur->next;
        operator delete(reinterpret_cast<void *>(cur)); // 用于释放使用 operator new 申请的空间，由于未存放任何东西，所以不需要调用 delete 来调用析构函数
        cur = next;
    }
}

// 每个 pool 里的 slot 大小相同，比如 pool0 的 slot 为 8 字节；pool1 的 slot 全为 16 字节
void MemoryPool::init(size_t size) {
    assert(size > 0);
    slotSize_ = size;
    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    freeList_ = nullptr;
    lastSlot_ = nullptr;
}

void *MemoryPool::allocate() {
    // 优先使用空闲链表里的内存槽
    if (freeList_ != nullptr) {
        {
            // 第一次检查是在无锁的情况下，第二次加锁需要重新检查，防止多线程下冲突
            std::lock_guard<std::mutex> lock(mutexForFreeList_);
            if (freeList_ != nullptr) {
                Slot *temp = freeList_;
                freeList_ = freeList_->next;
                return temp;
            }
        }
    }

    Slot *temp;
    {
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ >= lastSlot_) {
            // 当前 pool 无 slot 可用，需要新分配一个 block
            allocateNewBlock();
        }

        temp = curSlot_; // 当前未被使用过的槽
        curSlot_ += slotSize_ / sizeof(Slot *); // 指针运算 +1 相当于 +8，所以要除以 sizeof(Slot *)
    }

    return temp;
}

void MemoryPool::deallocate(void *ptr) {
    if (ptr) {
        // 回收内存，将内存通过头插法插入到空闲链表中
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        reinterpret_cast<Slot *>(ptr)->next = freeList_; // 指针间类型转换
        freeList_ = reinterpret_cast<Slot *>(ptr);
    }
}

void MemoryPool::allocateNewBlock() {
    // 头插法插入新的内存块
    void *newBlock = operator new(blockSize_);
    reinterpret_cast<Slot *>(newBlock)->next = firstBlock_;
    firstBlock_ = reinterpret_cast<Slot *>(newBlock);

    char *body = reinterpret_cast<char *>(newBlock) + sizeof(Slot *); // 将 newBlock 类型转为 char *，方便跳过每个 block 最开始的指针，指向实际存储的第一个位置
    size_t paddingSize = padPointer(body, slotSize_); // 计算对齐需要填充内存的大小
    curSlot_ = reinterpret_cast<Slot *>(body + paddingSize); // 需要进行 padding，确保 slot 的开始位置是 slotSize 的整数倍

    // 标记 block 的末尾
    lastSlot_ = reinterpret_cast<Slot *>(reinterpret_cast<size_t>(newBlock) + blockSize_ - 1); // block 的起始为 newBlock，跨过了 blockSize 个字节，0 索引需要减一

    freeList_ = nullptr;
}

size_t MemoryPool::padPointer(char *p, size_t align) {
    // align 传入的是 slotSize
    size_t missAlignment = reinterpret_cast<size_t>(p) % align; // 如果已经对齐，padding 大小为 0，如果没对齐，会表示超出开头的树
    // 然后需要 padding 的大小就是 align 到 missAlignment 的差
    return missAlignment == 0 ? 0 : align - missAlignment;
}

void HashBucket::initMemoryPool() {
    for (int i = 0; i < MEMORY_POOL_NUM; i++) {
        getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
    }
}

MemoryPool &HashBucket::getMemoryPool(int index) {
    static MemoryPool MemoryPool[MEMORY_POOL_NUM];
    return MemoryPool[index];   
}