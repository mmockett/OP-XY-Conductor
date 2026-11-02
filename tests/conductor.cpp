// Conductor's generator and control, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../control.cpp"
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

// A saved control word holding an enum's choice, as stock stores it.
static i32 choice_word(u32 choice, u32 count) { return static_cast<i32>(((2 * choice + 1) * 32768u / (2 * count)) << 16); }
static i32 percent_word(u32 percent) { return static_cast<i32>((percent * 32767u / 100u) << 16); }

static Globals globals(u32 key, u32 scale, u32 prog, u32 dens, u32 cmplx, u32 hrate, u32 swing, u32 evolve) {
    const i32 words[kGlobals] = {choice_word(key, 12), choice_word(scale, 7), choice_word(prog, 6), percent_word(dens),
                                 percent_word(cmplx), choice_word(hrate, 4), percent_word(swing), percent_word(evolve)};
    Globals g;
    for (u32 i = 0; i < kGlobals; ++i) g.v[i] = global_value(i, words[i]);
    return g;
}

static bool same_lanes(const Bar& a, const Bar& b, u32 lanes) {
    u32 i = 0, j = 0;
    for (;;) {
        while (i < a.count && !(lanes >> a.events[i].lane & 1)) ++i;
        while (j < b.count && !(lanes >> b.events[j].lane & 1)) ++j;
        if (i == a.count || j == b.count) return i == a.count && j == b.count;
        if (std::memcmp(&a.events[i], &b.events[j], sizeof(Event)) != 0) return false;
        ++i, ++j;
    }
}

static bool same_ticks(const Bar& a, const Bar& b, u32 lane) {
    u32 i = 0, j = 0;
    for (;;) {
        while (i < a.count && a.events[i].lane != lane) ++i;
        while (j < b.count && b.events[j].lane != lane) ++j;
        if (i == a.count || j == b.count) return i == a.count && j == b.count;
        if (a.events[i].tick != b.events[j].tick) return false;
        ++i, ++j;
    }
}

static bool in_scale(const Globals& g, u32 pitch) {
    for (u32 d = 0; d < 7; ++d)
        if ((pitch + 120 - static_cast<u32>(degree_semitones(g.v[kKey], g.v[kScale], static_cast<i32>(d)))) % 12 == 0) return true;
    return false;
}

static void globals_decode() {
    for (u32 c = 0; c < 12; ++c) check(global_value(kKey, choice_word(c, 12)) == static_cast<i32>(c), "key choices decode");
    check(global_value(kDensity, percent_word(100)) == 100 && global_value(kDensity, 0) == 0, "percentages decode");
}

static void generation() {
    static Song song;
    static Bar a, b;
    default_song(song);
    const Globals g = globals(2, 2, 0, 60, 50, 1, 20, 50);
    generate(song, g, 9, a);
    generate(song, g, 9, b);
    check(a.count > 0 && same_lanes(a, b, 0xff), "a bar is a pure function of the song, controls and bar");

    bool ranges = true, ordered = true, chords_in_scale = true, every_role = true;
    u32 roles_heard = 0;
    for (u32 prog = 0; prog < 6; ++prog) {
        const Globals h = globals(prog * 2, prog, prog, 40 + prog * 10, 20 + prog * 15, prog % 4, 10, 80);
        for (i32 bar = 0; bar < 32; ++bar) {
            generate(song, h, bar, a);
            for (u32 i = 0; i < a.count; ++i) {
                const Event& e = a.events[i];
                ranges = ranges && e.tick < kBarTicks && e.lane < kLanes && e.pitch <= 127 && e.velocity >= 1 &&
                         e.velocity <= 127 && e.length >= 2;
                ordered = ordered && (i == 0 || a.events[i - 1].tick <= e.tick);
                const u32 role = static_cast<u32>(song.lanes[e.lane].main[kRoleSlot]);
                roles_heard |= 1u << role;
                if (role == kChord) chords_in_scale = chords_in_scale && in_scale(h, e.pitch);
            }
        }
    }
    for (u32 l = 0; l < kLanes; ++l)
        if (song.lanes[l].main[kRoleSlot] != kOff)
            every_role = every_role && (roles_heard >> song.lanes[l].main[kRoleSlot] & 1);
    check(ranges, "every note is within MIDI's ranges and its bar");
    check(ordered, "a bar's notes are in tick order");
    check(chords_in_scale, "chords keep to the key and scale");
    check(every_role, "every default lane plays");
    check(!(roles_heard & 1), "off lanes stay silent");
}

static void freezing() {
    static Song song;
    static Bar a, b;
    default_song(song);
    const Globals g = globals(0, 1, 5, 60, 60, 1, 0, 100);
    const u32 melodic = 1u << 1 | 1u << 2 | 1u << 3 | 1u << 5;  // bass, chord, lead, pad
    bool evolves = false;
    for (i32 phrase = 1; phrase < 16 && !evolves; ++phrase) {
        generate(song, g, 1, a);
        generate(song, g, 1 + 4 * phrase, b);
        evolves = !same_lanes(a, b, melodic);
    }
    check(evolves, "at full evolution, later phrases change");
    song.frozen_at = 0;
    bool held = true;
    for (i32 phrase = 1; phrase < 16; ++phrase) {
        generate(song, g, 1, a);
        generate(song, g, 1 + 4 * phrase, b);
        held = held && same_lanes(a, b, melodic);
    }
    check(held, "a frozen song repeats its phrase");
    song.frozen_at = -1;
    song.lanes[2].frozen_at = 0;
    held = true;
    for (i32 phrase = 1; phrase < 16; ++phrase) {
        generate(song, g, 2, a);
        generate(song, g, 2 + 4 * phrase, b);
        held = held && same_ticks(a, b, 2);
    }
    check(held, "a frozen lane repeats its rhythm while the harmony moves");
}

static opxy::control::Input input;
static opxy::control::Overlay overlay;
static ControlState state;

static void tick(u32 ticks96, bool playing) {
    std::memset(&overlay, 0, sizeof overlay);
    input.transport.ticks96 = ticks96;
    input.transport.playing = playing;
    control(state, input, overlay);
    input.page_key = 0;
    input.turn_count = 0;
}

static void turn_once(u32 slot, i16 direction) {
    input.turn_count = 1;
    input.turns[0].slot = static_cast<u8>(slot);
    input.turns[0].direction = direction;
}

static const u32* shown() { return reinterpret_cast<const u32*>(overlay.display); }

// M2's words mark each lane's notes, and only those; M3's put the selected
// lane's notes on rows inside the range it labels.
static void playing_pages() {
    const u32 at = 2 * kBarTicks;
    input.page_key = kLane + 1;
    tick(at, true);
    Words expected = {};
    for (u32 i = 0; i < state.current.count; ++i)
        set_bit(expected, kHits, state.current.events[i].lane * kSteps + state.current.events[i].tick / kTicks);
    bool same = state.current.count > 0;
    for (u32 i = 0; i < 4; ++i) same = same && shown()[kHits + i] == expected.v[kHits + i];
    check(same, "M2 marks every lane's notes in their steps and nothing else");

    input.page_key = kDetail + 1;
    i32 margins[2] = {0, 0}, bands[2] = {0, 0};
    const u32 lanes[2] = {1, 4};  // a bass inside an octave, an arp over two
    for (u32 k = 0; k < 2; ++k) {
        state.lane = static_cast<u8>(lanes[k]);
        tick(at + 1 + k, true);
        const u32 range = shown()[kRange];
        const i32 low = static_cast<i32>(field(range, 0, 7)), band = static_cast<i32>(field(range, 7, 4));
        bool placed = band >= 1 && !field(range, 11, 1);
        i32 lowest = 127, highest = 0;
        for (u32 i = 0; i < state.current.count; ++i) {
            const Event& e = state.current.events[i];
            if (e.lane != lanes[k]) continue;
            const i32 row = (e.pitch - low) / band;
            placed = placed && e.pitch >= low && row < static_cast<i32>(kRollRows) &&
                     bit(shown(), kRoll, static_cast<u32>(row) * kSteps + e.tick / kTicks);
            lowest = e.pitch < lowest ? e.pitch : lowest;
            highest = e.pitch > highest ? e.pitch : highest;
        }
        check(lowest <= highest && placed, "M3 shows each of the lane's notes on a row inside its labelled range");
        margins[k] = (lowest - low) - (low + static_cast<i32>(kRollRows) * band - 1 - highest);
        bands[k] = band;
    }
    check(bands[0] == 1 && margins[0] >= -1 && margins[0] <= 1, "M3 centres a lane inside an octave on semitone rows");
    check(bands[1] > 1, "M3 widens its rows for a lane wider than twelve semitones");
    tick(at + 3, false);
}

static void controlling() {
    std::memset(&input, 0, sizeof input);
    std::memset(&state, 0, sizeof state);
    input.track = 11;
    input.transport.bpm_x10 = 1200;
    const Globals g = globals(0, 0, 0, 50, 40, 1, 0, 30);
    const i32 words[kGlobals] = {choice_word(0, 12), choice_word(0, 7), choice_word(0, 6), percent_word(50),
                                 percent_word(40), choice_word(1, 4), 0, percent_word(30)};
    for (u32 i = 0; i < kGlobals; ++i) input.values[11][i] = words[i];
    control_reset(state);

    tick(0, false);
    check(state.page == kConduct && !overlay.own_turns, "M1 leaves the encoders to stock");
    input.shift = 1;
    tick(0, false);
    check(overlay.own_turns == 1, "Shift on M1 takes the encoders");
    turn_once(2, 1);
    const u8 fill = state.song.fill;
    tick(0, false);
    check(state.song.fill == fill + 5, "Shift + encoder 3 fills every lane on M1");
    input.shift = 0;

    input.page_key = 2;
    tick(0, false);
    check(state.page == kLane && overlay.own_turns == 1, "M2 opens the lanes page, which takes the encoders");
    turn_once(0, 1);
    tick(0, false);
    check(state.lane == 1, "encoder 1 selects the lane");
    turn_once(kRoleSlot, 1);
    tick(0, false);
    check(state.song.lanes[1].main[kRoleSlot] == kChord, "encoder 2 steps the lane's role");
    input.page_key = 2;
    tick(0, false);
    check(state.page == kLane, "M2 again stays on the lanes page");
    input.page_key = 4;
    tick(0, false);
    check(state.page == kRoute, "M4 opens the route page from another of its own");
    input.page_key = 3;
    tick(0, false);
    check(state.page == kDetail, "M3 opens the lane page");
    input.page_key = 1;
    tick(0, false);
    check(state.page == kConduct && !overlay.own_turns, "M1 returns to the saved controls");

    // Playback: one bar, tick by tick, plays exactly the bar's notes on their tracks.
    default_song(state.song);
    state.song.lanes[1].route[kTargetSlot] = 6;
    state.song.lanes[3].route[kMute] = 1;
    state.dirty = 1;
    static Bar expected;
    generate(state.song, g, 0, expected);
    u32 want = 0, got = 0, on_seven = 0;
    bool targets = true;
    for (u32 i = 0; i < expected.count; ++i) want += expected.events[i].lane != 3;
    tick(0, false);
    for (u32 t = 0; t < kBarTicks; t = t + 5 < kBarTicks || t == kBarTicks - 1 ? t + 5 : kBarTicks - 1) {
        tick(t, true);
        for (u32 n = 0; n < overlay.note_count; ++n) {
            ++got;
            on_seven += overlay.notes[n].track == 6;
            targets = targets && overlay.notes[n].track != 3;
        }
    }
    check(got == want, "a bar plays each of its unmuted notes once");
    check(on_seven > 0 && targets, "lanes play on their routed tracks, muted lanes not at all");
    tick(kBarTicks + 40, false);
    check(state.playing == 0, "stopping ends playback");

    // Starting again later in the song plays from there, not what it skipped.
    static Bar later;
    generate(state.song, g, 1, later);
    u32 at = 0;
    const u32 restart = kBarTicks + 300;
    for (u32 i = 0; i < later.count; ++i) at += later.events[i].tick == 300 && later.events[i].lane != 3;
    tick(restart, true);
    check(overlay.note_count == at, "starting plays only the notes at the start position");
    tick(restart + 1, false);

    // Stopped, the song waits at its start however the transport's ticks run.
    tick(5 * kBarTicks + 7 * kTicks, false);
    const i32 stopped_bar = state.bar;
    const u32 stopped_state = static_cast<u32>(overlay.display[kState]);
    tick(9 * kBarTicks + 2 * kTicks, false);
    check(stopped_bar == 0 && state.bar == 0, "stopped, the song stays at its first bar");
    check(static_cast<u32>(overlay.display[kState]) == stopped_state && field(stopped_state, 7, 4) == 0,
          "stopped, the display's step stays put");
    playing_pages();

    // Save and load.
    static u8 data[1024];
    state.song.seed = 1234;
    state.song.lanes[5].detail[kPad - 1][2] = 7;
    const u32 bytes = control_save(state, data, sizeof data);
    check(bytes == 4 + sizeof(Song), "the song saves whole");
    static ControlState loaded;
    std::memset(&loaded, 0, sizeof loaded);
    control_load(loaded, data, bytes);
    check(std::memcmp(&loaded.song, &state.song, sizeof(Song)) == 0, "a saved song loads back");
    data[0] ^= 1;
    control_load(loaded, data, bytes);
    check(loaded.song.seed == 0 && loaded.song.lanes[0].main[kRoleSlot] == kDrum, "another version loads the default song");
    control_load(loaded, data, 0);
    check(loaded.song.lanes[1].main[kRoleSlot] == kBass, "a project without data starts with the default song");
}

int main() {
    globals_decode();
    generation();
    freezing();
    controlling();
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
