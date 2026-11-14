#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

namespace driftwave {

/** A one-way pipe of mono audio from the audio thread to the interface, for
    drawing only. Single writer, single reader, no locks and no allocation
    after construction. If the reader falls behind, the writer drops the block
    rather than wait: a display may skip, the audio never does. */
class SignalFeed {
public:
    SignalFeed() : buffer(size_t(kSize), 0.0f) {}

    /** Audio thread. Stores the mid (L+R)/2 of the block. */
    void push(const float* left, const float* right, int numSamples) noexcept
    {
        const int w = writePos.load(std::memory_order_relaxed);
        const int r = readPos.load(std::memory_order_acquire);
        const int free = kSize - 1 - ((w - r) & kMask);
        if (numSamples > free)
            return;
        for (int i = 0; i < numSamples; ++i)
            buffer[size_t((w + i) & kMask)] = 0.5f * (left[i] + right[i]);
        writePos.store((w + numSamples) & kMask, std::memory_order_release);
    }

    /** Interface thread. Returns how many samples were copied. */
    int pull(float* destination, int maxSamples) noexcept
    {
        const int r = readPos.load(std::memory_order_relaxed);
        const int w = writePos.load(std::memory_order_acquire);
        const int count = std::min((w - r) & kMask, maxSamples);
        for (int i = 0; i < count; ++i)
            destination[i] = buffer[size_t((r + i) & kMask)];
        readPos.store((r + count) & kMask, std::memory_order_release);
        return count;
    }

private:
    static constexpr int kSize = 1 << 15, kMask = kSize - 1;
    std::vector<float> buffer;
    std::atomic<int> writePos { 0 }, readPos { 0 };
};

} // namespace driftwave
