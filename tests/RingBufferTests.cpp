#include "TestHarness.h"
#include "util/SpscRingBuffer.h"

#include <thread>

using sonarc::SpscRingBuffer;

TEST_CASE("Ring buffer rounds capacity up to a power of two")
{
    SpscRingBuffer<int> ring(100);
    CHECK(ring.capacity() == 128);
}

TEST_CASE("Ring buffer preserves order across wrap-around")
{
    SpscRingBuffer<int> ring(8);
    int next = 0, expected = 0;
    int out[8];

    for (int round = 0; round < 50; ++round)
    {
        int in[5];
        for (int& v : in)
            v = next++;
        CHECK(ring.push(in, 5) == 5);

        const auto n = ring.pop(out, 5);
        CHECK(n == 5);
        for (std::size_t i = 0; i < n; ++i)
            CHECK(out[i] == expected++);
    }
}

TEST_CASE("Ring buffer drops what doesn't fit")
{
    SpscRingBuffer<int> ring(4);
    const int in[6] = { 1, 2, 3, 4, 5, 6 };
    CHECK(ring.push(in, 6) == 4);
    CHECK(ring.availableToRead() == 4);

    ring.discardAll();
    CHECK(ring.availableToRead() == 0);
}

TEST_CASE("Ring buffer transfers a stream between two threads intact")
{
    SpscRingBuffer<std::uint32_t> ring(1024);
    constexpr std::uint32_t total = 2'000'000;

    std::thread producer([&]
    {
        std::uint32_t value = 0;
        std::uint32_t chunk[64];
        while (value < total)
        {
            std::uint32_t count = 0;
            for (; count < 64 && value + count < total; ++count)
                chunk[count] = value + count;
            std::size_t sent = 0;
            while (sent < count)
                sent += ring.push(chunk + sent, count - sent);
            value += count;
        }
    });

    std::uint32_t expected = 0;
    bool ordered = true;
    std::uint32_t buffer[100];
    while (expected < total)
    {
        const auto n = ring.pop(buffer, 100);
        for (std::size_t i = 0; i < n; ++i)
            ordered = ordered && buffer[i] == expected++;
    }

    producer.join();
    CHECK(ordered);
}
