#pragma once
#include <opxy/audio.hpp>

namespace conductor {
// Conductor makes no sound of its own: it plays the instrument tracks.
struct Voice { opxy::u32 unused; };
struct Shared {};
}
