#include "core.h"

namespace conductor {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.unused = 0; }
inline void start(Voice&, Shared&, opxy::u32) {}
inline void destroy(Voice&, Shared&, opxy::u32) {}

inline void render(Voice&, Shared&, opxy::u32, opxy::Output output, opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
}
}
