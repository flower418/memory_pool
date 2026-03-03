#include "../include/MemoryPool_1.h"

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
    lastSlot_ = nullptr;
    freeList_.store(nullptr, std::memory_order_relaxed); // 头节点为 nullptr，不需要同步
}

// 无锁入队操作
bool MemoryPool::pushFreeList(Slot *slot) {
    while (true) {
        // 获取当前头节点
        Slot *oldHead = freeList_.load(std::memory_order_relaxed);
        // 将新节点的 next 指向当前头节点
        slot->next.store(oldHead, std::memory_order_relaxed);
        // 尝试将新节点设置为头节点
        if (freeList_.compare_exchange_weak(oldHead, slot, std::memory_order_release, std::memory_order_relaxed)) {
            return true;
        }
    }
}

// 无锁出队操作
Slot *MemoryPool::popFreeList() {
    while (true) {
        Slot *oldHead = freeList_.load(std::memory_order_relaxed);
        if (oldHead == nullptr) {
            return nullptr;
        }

        // 获取下一个节点
        Slot *newHead = oldHead->next.load(std::memory_order_relaxed);

        // 尝试更新头节点
        if (freeList_.compare_exchange_weak(oldHead, newHead, std::memory_order_release, std::memory_order_relaxed)) {
            return oldHead;
        }
    }
}

void *MemoryPool::allocate() {
    // 优先使用空闲链表里的内存槽
    Slot *slot = popFreeList();
    if (slot != nullptr) {
        return slot;
    }

    // 如果空闲链表为空，则分配新的内存
    std::lock_guard<std::mutex> lock(mutexForBlock_);
    if (curSlot_ >= lastSlot_) {
        allocateNewBlock();
    }

    Slot *result = curSlot_;
    // 移动到下一个 slot
    curSlot_ = reinterpret_cast<Slot *>(reinterpret_cast<char *>(curSlot_) + slotSize_);

    return result;
}

void MemoryPool::deallocate(void *ptr) {
    if (!ptr) {
        return;
    }

    Slot *slot = static_cast<Slot *>(ptr);
    pushFreeList(slot);
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