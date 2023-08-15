#pragma once

#include <deque>

template <typename T>
using SharedPtr = std::shared_ptr<T>;

template <typename T>
using WeakPtr = std::weak_ptr<T>;

template <typename T>
using EnableSharedFromThis = std::enable_shared_from_this<T>;

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
    return std::allocate_shared<T>(std::forward<const Alloc>(alloc),
                                   std::forward<Args>(args)...);
}

// TODO: Solution above is smart enough, but please, remove it and add your declarations here
