// Conductor makes no sound of its own.
#include "../engine.cpp"
#include <cstdio>

int main() {
    static conductor::Voice voice;
    static conductor::Shared shared;
    opxy::i32 parameters[8] = {0x40000000, 0x40000000, 0x7fffffff, 0x40000000};
    opxy::u32 pitch[opxy::block_frames] = {};
    opxy::i32 output[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 1;
    conductor::initialize(voice, shared, 1);
    conductor::start(voice, shared, 1);
    conductor::render(voice, shared, 1, output, parameters, pitch);
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        if (output[i]) { std::puts("FAIL not silent"); return 1; }
    return 0;
}
