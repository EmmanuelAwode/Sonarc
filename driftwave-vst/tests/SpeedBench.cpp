// Measures how much processor time the plugin needs, as a share of real time (reverb on throughout).
#include "dsp/EffectChain.h"

#include <chrono>
#include <cstdio>
#include <vector>

int main()
{
    using namespace driftwave;
    const double sr = 48000.0;
    const int block = 512, blocks = int(sr * 20 / block);
    std::vector<float> l(block), r(block);
    struct Case { const char* name; bool on; float speed; bool sync; };
    for (const Case c : { Case { "speed off", false, 1.0f, false }, Case { "tape 80%", true, 0.8f, false },
                          Case { "sync 80%", true, 0.8f, true }, Case { "sync 50%", true, 0.5f, true }, Case { "sync 150%", true, 1.5f, true } }) {
        EffectChain chain;
        chain.prepare(sr);
        ChainParams p;
        p.speedOn = c.on; p.speed = c.speed; p.speedSync = c.sync;
        chain.setParams(p);
        unsigned state = 1;
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < block; ++i) {
                state = state * 1664525u + 1013904223u;
                l[size_t(i)] = r[size_t(i)] = float(state >> 8) / 8388608.0f - 1.0f;
            }
            chain.process(l.data(), r.data(), block);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%-10s  %.2f%% of one core\n", c.name, 100.0 * seconds / 20.0);
    }
    return 0;
}
