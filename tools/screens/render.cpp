// Runs Conductor's control and display on the host and prints one frame of a
// page: "n <node> <x> <y> <scale> <shade> <paint>" and "t <slot> <x> <y> <text>",
// Q10 pixels. tools/screens/screens.py draws it.
//   render <page 0..3> <selected lane 0..7>
#include "control.cpp"
#include "display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace conductor;

static i32 words_out[opxy::control::display_words];
extern "C" u32 sdk_control_copies(u32) { return 1; }
extern "C" i32 sdk_control_display(u32 i) { return words_out[i]; }

static i32 choice_word(u32 c, u32 n) { return static_cast<i32>(((2 * c + 1) * 32768u / (2 * n)) << 16); }
static i32 percent_word(u32 p) { return static_cast<i32>((p * 32767u / 100u) << 16); }

static opxy::control::Input input;
static opxy::control::Overlay overlay;
static ControlState control_state;
static DisplayState display_state;
static conductor::Frame frame;

static void tick(u32 ticks96) {
    std::memset(&overlay, 0, sizeof overlay);
    input.transport.ticks96 = ticks96;
    input.transport.playing = 1;
    control(control_state, input, overlay);
    std::memcpy(words_out, overlay.display, sizeof words_out);
    input.page_key = 0;
}

int main(int argc, char** argv) {
    const u32 page = argc > 1 ? static_cast<u32>(std::atoi(argv[1])) : 0;
    const u32 lane = argc > 2 ? static_cast<u32>(std::atoi(argv[2])) : 0;
    // C minor, pop, playing bar 2 step 7, lane 4 muted.
    const i32 globals[kGlobals] = {choice_word(0, 12), choice_word(1, 7), choice_word(0, 6), percent_word(60),
                                   percent_word(50),   choice_word(1, 4), percent_word(10), percent_word(30)};
    input.track = 11;
    input.transport.bpm_x10 = 1200;
    for (u32 i = 0; i < kGlobals; ++i) input.values[11][i] = globals[i];
    control_reset(control_state);
    control_state.song.lanes[3].route[kMute] = 1;
    control_state.lane = static_cast<u8>(lane);
    input.page_key = page + 1;
    const u32 at = kBarTicks + 6 * kTicks;
    for (u32 t = at - 30; t <= at; t += 3) tick(t);

    opxy::draw::Input in;
    std::memset(&in, 0, sizeof in);
    for (u32 i = 0; i < kGlobals; ++i) in.slots[i] = globals[i];
    for (u32 i = 0; i < 4; ++i) in.shown[i] = static_cast<u8>(i);
    in.reduced_motion = 1;
    in.elapsed = 34;
    display_reset(display_state);
    for (int f = 0; f < 3; ++f) display(display_state, in, frame);

    for (u32 i = 0; i < conductor::Frame::node_count; ++i) {
        const opxy::draw::Node& n = frame.nodes[i];
        std::printf("n %u %d %d %d %u %u\n", i, n.x, n.y, n.scale, n.shade, n.paint);
    }
    for (u32 i = 0; i < conductor::Frame::text_count; ++i) {
        const opxy::draw::Text& t = frame.texts[i];
        std::printf("t %u %d %d %s\n", i, t.x, t.y, t.chars);
    }
}
