#pragma once

#include <mutex>
#include <cassert>

#define MEMORY_POOL_NUM 64 // 一个 pool 管理 64 个 block
#define SLOT_BASE_SIZE 8 // 一个 slot 最小 8 字节
#define MAX_SLOT_SIZE 512 // 一个 slot 最大 512 字节

struct Slot {
    Slot *next;
};

class MemoryPool {
private:
    int blockSize_; // 内存块大小
    int slotSize_; // 槽大小
    Slot *firstBlock_; // 指向内存池管理的首个内存块
    Slot *curSlot_; // 指向当前未被使用过的槽
    Slot *freeList_; // 指向空闲的槽
    Slot *lastSlot_; // 当前内存块中最后能够存放元素的位置标识（超过该位置需申请新的内存）
    std::mutex mutexForFreeList_;
    std::mutex mutexForBlock_; // 多线程情况下避免重复开辟内存

private:
    void allocateNewBlock();
    size_t padPointer(char *p, size_t align);

public:
    MemoryPool(size_t BlockSize = 4096);
    ~MemoryPool();

    void init(size_t);
    void *allocate();
    void deallocate(void *);
};

// 用来根据请求的内存大小分配 slot，如 8 字节映射给 slot0，16字节映射给 slot1
class HashBucket {
public:
    static void initMemoryPool();
    static MemoryPool &getMemoryPool(int index);

    static void *useMemory(size_t size) {
        if (size <= 0) {
            return nullptr;
        }
        if (size > MAX_SLOT_SIZE) { // 大于 512 字节的内存，则使用 new
            return operator new(size);
        }

        return getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate(); // size / 8 向上取整，由于索引从 0 开始，所以要减一
    }

    static void freeMemory(void *ptr, size_t size) {
        if (!ptr) {
            return;
        }
        if (size > MAX_SLOT_SIZE) {
            operator delete(ptr);
            return;
        }

        getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr);
    }

    // 用于创建一个对象
    template<typename T, typename... Args> // 可变参数，&& 表示万能引用
    static T *newElement(Args&&... args) {
        T *p = nullptr;
        if ((p = reinterpret_cast<T *>(HashBucket::useMemory(sizeof(T)))) != nullptr) { // 为 T 分配一片内存
            new(p) T(std::forward<Args>(args)...); // std::forward 进行完美转发
        }

        return p;
    }

    template<typename T>
    static void deleteElement(T *p) {
        if (p) {
            p->~T();
            HashBucket::freeMemory(reinterpret_cast<void *>(p), sizeof(T));
        }
    }
};
