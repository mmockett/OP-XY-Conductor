#pragma once
#include <opxy/page_header.hpp>

namespace conductor {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<0, 0, 233, 32, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u16 glow[8];  // each lane's light, Q10: full when it plays, then fading
};
}
