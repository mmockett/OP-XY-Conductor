// Conductor's display, run on the host with its control: what the pages put
// on screen. python sdk.py test --machine <folder> runs it.
#include "../control.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace conductor;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

static i32 words_out[opxy::control::display_words];
extern "C" u32 sdk_control_copies(u32) { return 1; }
extern "C" i32 sdk_control_display(u32 i) { return words_out[i]; }

static opxy::control::Input input;
static opxy::control::Overlay overlay;
static ControlState control_state;
static DisplayState display_state;
static conductor::Frame frame;
static opxy::draw::Input in;

static void tick(u32 ticks96) {
    std::memset(&overlay, 0, sizeof overlay);
    input.transport.ticks96 = ticks96;
    input.transport.playing = true;
    control(control_state, input, overlay);
    std::memcpy(words_out, overlay.display, sizeof words_out);
    input.page_key = 0;
}

static void draw() { display(display_state, in, frame); }

static bool shows(const char* s) {
    for (u32 i = 0; i < conductor::Frame::text_count; ++i)
        if (!std::strcmp(frame.texts[i].chars, s)) return true;
    return false;
}

// Opens a page (1..4) on a lane and draws it at bar 2, step 7.
static void open(u32 page, u32 lane) {
    control_state.lane = static_cast<u8>(lane);
    input.page_key = page;
    tick(kBarTicks + 6 * kTicks);
    draw();
}

int main() {
    input.track = 11;
    input.transport.bpm_x10 = 1200;
    control_reset(control_state);
    for (u32 i = 0; i < 4; ++i) in.shown[i] = static_cast<u8>(i);
    in.reduced_motion = 1;
    display_reset(display_state);

    // Shift shows the actions on the row being edited, and only there.
    open(2, 4);
    input.shift = 1;
    tick(kBarTicks + 6 * kTicks);
    const char* titles[8];
    char values[8][header_capacity];
    header_captions(in, reinterpret_cast<const u32*>(words_out), titles, values);
    check(!std::strcmp(titles[0], "reroll") && !std::strcmp(titles[3], "vary") && !std::strcmp(titles[4], "vel"),
          "Shift on the first row shows the actions there, and the second row's controls");
    for (u32 i = 0; i < 4; ++i) in.shown[i] = static_cast<u8>(i + 4);
    header_captions(in, reinterpret_cast<const u32*>(words_out), titles, values);
    check(!std::strcmp(titles[0], "lane") && !std::strcmp(titles[4], "reroll") && !std::strcmp(titles[7], "vary"),
          "Shift on the second row shows the actions there, and the first row's controls");
    for (u32 i = 0; i < 4; ++i) in.shown[i] = static_cast<u8>(i);
    input.shift = 0;

    // M3 gives the left column to the selected lane: a drum lane's voices,
    // a synth lane's notes, never the list of lanes.
    open(3, 0);
    bool voices = true;
    for (u32 v = 0; v < kDrumRows; ++v) voices = voices && shows(kVoiceNames[v]);
    check(voices && shows("1 drum") && !shows("2 bass fifth"), "M3 on a drum lane names its voices, not the other lanes");
    open(3, 1);
    u32 notes = 0;
    for (u32 i = kLaneText; i < kLaneText + kLanes; ++i) {
        const char* t = frame.texts[i].chars;
        notes += t[0] >= 'A' && t[0] <= 'G' && ((t[1] >= '0' && t[1] <= '9') || t[1] == 'b' || t[1] == '#');
    }
    check(notes >= 6 && !shows("1 drum"), "M3 on a synth lane names its notes, not the other lanes");
    // At this density the arp rests outside each phrase's last two bars.
    open(3, 4);
    check(shows("rests this bar"), "M3 says when the lane rests for the bar");

    // MIDI: shown, but nothing can be routed there until the SDK sends it.
    open(4, 0);
    bool dim = true;
    for (u32 c = 0; c < kLanes; ++c) dim = dim && frame.nodes[kMidiBoxes + c].shade == kRingRule;
    check(dim && shows("midi: not yet") && kRouteSpecs[kTargetSlot].maximum == 7,
          "M4 shows the MIDI channels as not yet playable, and no lane can be routed to one");

    // No wire turns by less than 2.5 degrees unless it runs straight, on any page.
    bool turns = true;
    for (u32 page = 1; page <= kPages; ++page)
        for (u32 lane = 0; lane < kLanes; ++lane) {
            for (u32 l = 0; l < kLanes; ++l) control_state.song.lanes[l].route[kTargetSlot] = static_cast<i8>((l + lane) % kLanes);
            open(page, lane);
            for (u32 l = 0; l < kLanes; ++l) {
                const opxy::draw::Node& n = frame.nodes[kWires + l];
                if (!n.scale) continue;
                const double degrees = std::fabs(std::atan2(static_cast<double>(n.sin), static_cast<double>(n.cos))) * 57.2958;
                turns = turns && (degrees == 0 || degrees >= 2.5);
            }
        }
    check(turns, "every wire runs straight or turns by at least 2.5 degrees");

    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
