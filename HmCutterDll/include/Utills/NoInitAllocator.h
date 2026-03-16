#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace HmCutter {

/// value-initialize(0-초기화)를 건너뛰는 allocator.
/// std::vector<uint8_t, NoInitAllocator<uint8_t>>와 함께 사용하면
/// resize() 시 memset 비용을 제거할 수 있다.
template <typename T>
struct NoInitAllocator {
    using value_type = T;

    NoInitAllocator() noexcept = default;

    template <typename U>
    NoInitAllocator(const NoInitAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::allocator<T>{}.deallocate(p, n);
    }

    template <typename U>
    void construct(U* p) noexcept {
        ::new (static_cast<void*>(p)) U;
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
};

template <typename T, typename U>
bool operator==(const NoInitAllocator<T>&, const NoInitAllocator<U>&) noexcept { return true; }

template <typename T, typename U>
bool operator!=(const NoInitAllocator<T>&, const NoInitAllocator<U>&) noexcept { return false; }

using RawByteVec = std::vector<uint8_t, NoInitAllocator<uint8_t>>;

} // namespace HmCutter
