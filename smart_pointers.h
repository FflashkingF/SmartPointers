#pragma once

#include <iostream>
#include <memory>
#include <type_traits>

namespace {
struct BaseControlBlock {
    uint shared_count = 0;
    uint weak_count = 0;

    virtual void destroy() = 0;
    virtual void deallocate() = 0;
    virtual ~BaseControlBlock() = default;
};

template <typename T, typename Delete, typename Alloc>
struct RegularControlBlock : BaseControlBlock {
    using RCB_Alloc = typename std::allocator_traits<
        Alloc>::template rebind_alloc<RegularControlBlock>;
    using RCB_AllocTraits = std::allocator_traits<RCB_Alloc>;

    T* ptr = nullptr;
    [[no_unique_address]] Delete deleter;
    [[no_unique_address]] Alloc allocator;

    void destroy() override {
        deleter(ptr);
        ptr = nullptr;
    }
    void deallocate() override {
        RCB_Alloc RCB_allocator = std::move(allocator);
        RCB_AllocTraits::deallocate(RCB_allocator, this, 1);
    }

    RegularControlBlock() {}
    RegularControlBlock(uint shc, uint wc, T* other_ptr,
                        const Delete& other_deleter,
                        const Alloc& other_allocator) {
        shared_count = shc;
        weak_count = wc;
        ptr = other_ptr;
        deleter = other_deleter;
        allocator = other_allocator;
    }
};

template <typename T, typename Alloc>
struct MakeSharedControlBlock : BaseControlBlock {
    using MSCB = MakeSharedControlBlock;
    using MSCB_Alloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<MSCB>;
    using MSCB_AllocTraits = std::allocator_traits<MSCB_Alloc>;

    char object[sizeof(T)];
    [[no_unique_address]] Alloc allocator;

    template <typename... Args>
    MakeSharedControlBlock(Args&&... args) {
        new (reinterpret_cast<T*>(object)) T(std::forward<Args>(args)...);
    }

    void destroy() override {
        std::allocator_traits<Alloc>::destroy(allocator,
                                              reinterpret_cast<T*>(object));
    }

    void deallocate() override {
        MSCB_Alloc MSCB_allocator = std::move(allocator);
        MSCB_AllocTraits::deallocate(MSCB_allocator, this, 1);
    }
};

}  // namespace

template <typename T>
class WeakPtr;

template <typename T>
class EnableSharedFromThis;

template <typename T>
class SharedPtr {
  private:
    T* ptr = nullptr;
    BaseControlBlock* cb = nullptr;

    SharedPtr(const WeakPtr<T>& wp) : ptr(wp.ptr), cb(wp.cb) {
        if (cb != nullptr) {
            ++(cb->shared_count);
        }
    }

  public:
    SharedPtr() {}

    SharedPtr(const SharedPtr& shp) : ptr(shp.ptr), cb(shp.cb) {
        if (cb != nullptr) {
            ++(cb->shared_count);
        }
    }

    SharedPtr(SharedPtr&& shp) : ptr(shp.ptr), cb(shp.cb) {
        shp.cb = nullptr;
        shp.ptr = nullptr;
    }

    SharedPtr& operator=(const SharedPtr& shp) {
        if (this == &shp) {
            return *this;
        }
        SharedPtr copy(shp);
        swap(copy);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& shp) {
        SharedPtr copy(std::move(shp));
        swap(copy);
        return *this;
    }

    ~SharedPtr() {
        if (cb == nullptr) {
            return;
        }
        --(cb->shared_count);
        if (cb->shared_count == 0) {
            if (cb->weak_count == 0) {
                cb->destroy();
                cb->deallocate();
            } else {
                cb->destroy();
            }
        }
    }

    template <typename U>
    SharedPtr(SharedPtr<U> shp) : ptr(shp.ptr), cb(shp.cb) {
        if (cb) {
            ++(cb->shared_count);
        }
    }

    template <typename U>
    SharedPtr(U* other_ptr) {
        ptr = other_ptr;
        cb = new RegularControlBlock<T, std::default_delete<U>,
                                     std::allocator<U>>(
            1, 0, ptr, std::default_delete<U>(), std::allocator<U>());

        if constexpr (std::is_base_of_v<EnableSharedFromThis<U>, U>) {
            other_ptr->enable_wp = *this;
        }
    }

    template <typename U, typename Delete>
    SharedPtr(U* other_ptr, Delete deleter) {
        ptr = other_ptr;
        cb = new RegularControlBlock<T, Delete, std::allocator<U>>(
            1, 0, ptr, deleter, std::allocator<U>());

        if constexpr (std::is_base_of_v<EnableSharedFromThis<U>, U>) {
            other_ptr->enable_wp = *this;
        }
    }

    template <typename U, typename Delete, typename Alloc>
    SharedPtr(U* other_ptr, Delete deleter, Alloc allocator) {
        ptr = other_ptr;

        using RCB = RegularControlBlock<T, Delete, Alloc>;
        using RCB_Alloc = typename RCB::RCB_Alloc;
        using RCB_AllocTraits = std::allocator_traits<RCB_Alloc>;

        RCB_Alloc RCB_allocator = std::move(allocator);
        RCB* RCB_ptr = RCB_AllocTraits::allocate(RCB_allocator, 1);
        new (RCB_ptr) RegularControlBlock<T, Delete, Alloc>();
        cb = RCB_ptr;
        RCB_ptr->shared_count = 1;
        RCB_ptr->deleter = deleter;
        RCB_ptr->allocator = std::move(RCB_allocator);

        if constexpr (std::is_base_of_v<EnableSharedFromThis<U>, U>) {
            other_ptr->enable_wp = *this;
        }
    }

    uint use_count() const {
        return cb == nullptr ? 0 : cb->shared_count;
    }

    void reset() {
        SharedPtr().swap(*this);
    }
    template <typename U>
    void reset(U* other_ptr) {
        SharedPtr<T>(other_ptr).swap(*this);
    }

    void swap(SharedPtr& shp) {
        std::swap(cb, shp.cb);
        std::swap(ptr, shp.ptr);
    }

    T& operator*() const {
        return *ptr;
    }

    T* operator->() const {
        return ptr;
    }

    T* get() const {
        return ptr;
    }

    template <typename U, typename Alloc, typename... Args>
    friend SharedPtr<U> allocateShared(const Alloc&, Args&&...);

    template <typename U>
    friend class WeakPtr;

    template <typename U>
    friend class SharedPtr;
};

template <typename T>
class WeakPtr {
  private:
    T* ptr = nullptr;
    BaseControlBlock* cb = nullptr;

  public:
    WeakPtr() {}

    template <typename U>
    WeakPtr(SharedPtr<U> shp) : ptr(shp.ptr), cb(shp.cb) {
        if (cb) {
            ++(cb->weak_count);
        }
    }

    WeakPtr(const WeakPtr& wp) : ptr(wp.ptr), cb(wp.cb) {
        if (cb != nullptr) {
            ++(cb->weak_count);
        }
    }

    WeakPtr(WeakPtr&& wp) : ptr(wp.ptr), cb(wp.cb) {
        wp.ptr = nullptr;
        wp.cb = nullptr;
    }

    WeakPtr& operator=(const WeakPtr& wp) {
        WeakPtr copy(wp);
        swap(copy);
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& wp) {
        WeakPtr copy(std::move(wp));
        swap(copy);
        return *this;
    }

    ~WeakPtr() {
        if (cb == nullptr) {
            return;
        }
        --(cb->weak_count);
        if (cb->weak_count == 0 && cb->shared_count == 0) {
            cb->deallocate();
        }
    }

    void swap(WeakPtr& wp) {
        std::swap(ptr, wp.ptr);
        std::swap(cb, wp.cb);
    }

    template <typename U>
    WeakPtr(WeakPtr<U> wp) : ptr(wp.ptr), cb(wp.cb) {
        if (cb) {
            ++(cb->weak_count);
        }
    }

    bool expired() const {
        return cb == nullptr || cb->shared_count == 0;
    }

    SharedPtr<T> lock() const {
        return expired() ? SharedPtr<T>() : SharedPtr<T>(*this);
    }

    uint use_count() const {
        return cb == nullptr ? 0 : cb->shared_count;
    }
    template <typename U>
    friend class SharedPtr;

    template <typename U>
    friend class WeakPtr;
};

template <typename T>
class EnableSharedFromThis {
  private:
    WeakPtr<T> enable_wp;

  public:
    SharedPtr<T> shared_from_this() const {
        return SharedPtr<T>(enable_wp);
    }

    template <typename U>
    friend class SharePtr;
};

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(const Alloc& alloc,
                            Args&&... args) {  // todo do const Alloc

    using MSCB = MakeSharedControlBlock<T, Alloc>;
    using MSCB_Alloc =
        typename std::allocator_traits<Alloc>::template rebind_alloc<MSCB>;
    using MSCB_AllocTraits = std::allocator_traits<MSCB_Alloc>;

    SharedPtr<T> shp;
    MSCB_Alloc MSCB_allocator = alloc;
    MSCB* MSCB_ptr = MSCB_AllocTraits::allocate(MSCB_allocator, 1);
    MSCB_AllocTraits::construct(MSCB_allocator, MSCB_ptr,
                                std::forward<Args>(args)...);
    shp.cb = MSCB_ptr;
    MSCB_ptr->shared_count = 1u;
    shp.ptr = reinterpret_cast<T*>(MSCB_ptr->object);
    MSCB_ptr->allocator = std::move(MSCB_allocator);
    return shp;
}

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return allocateShared<T>(std::allocator<T>(), std::forward<Args>(args)...);
}
