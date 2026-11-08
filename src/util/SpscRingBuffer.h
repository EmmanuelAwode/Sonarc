#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace sonarc {

// Lock-free single-producer / single-consumer ring buffer.
// push() must only be called from one thread (the producer) and pop(),
// availableToRead() and discardAll() from one other thread (the consumer).
// None of these calls allocate, lock or block.
template <typename T>
class SpscRingBuffer
{
public:
    explicit SpscRingBuffer(std::size_t minimumCapacity)
    {
        std::size_t capacity = 1;
        while (capacity < minimumCapacity)
            capacity <<= 1;

        storage.resize(capacity);
        mask = capacity - 1;
    }

    std::size_t capacity() const noexcept { return storage.size(); }

    // Copies up to `count` items in. Returns how many fit; the rest are dropped.
    std::size_t push(const T* items, std::size_t count) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto read = readIndex.load(std::memory_order_acquire);
        const auto n = std::min(count, storage.size() - (write - read));

        for (std::size_t i = 0; i < n; ++i)
            storage[(write + i) & mask] = items[i];

        writeIndex.store(write + n, std::memory_order_release);
        return n;
    }

    // Copies up to `maxCount` items out. Returns how many were read.
    std::size_t pop(T* destination, std::size_t maxCount) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        const auto write = writeIndex.load(std::memory_order_acquire);
        const auto n = std::min(maxCount, write - read);

        for (std::size_t i = 0; i < n; ++i)
            destination[i] = storage[(read + i) & mask];

        readIndex.store(read + n, std::memory_order_release);
        return n;
    }

    std::size_t availableToRead() const noexcept
    {
        return writeIndex.load(std::memory_order_acquire) - readIndex.load(std::memory_order_relaxed);
    }

    void discardAll() noexcept
    {
        readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::vector<T> storage;
    std::size_t mask = 0;
    std::atomic<std::size_t> writeIndex { 0 };
    std::atomic<std::size_t> readIndex { 0 };
};

} // namespace sonarc
