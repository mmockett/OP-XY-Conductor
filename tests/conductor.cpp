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

// Whether a lane starts or holds a note anywhere in steps [first, last).
static bool sounds(const Bar& bar, u32 lane, u32 first, u32 last) {
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        const u32 start = e.tick, end = e.tick + e.length;
        if (e.lane == lane && start < last * kTicks && end > first * kTicks) return true;
    }
    return false;
}

// M1's words mark every beat of the phrase that sounds, and only those; M2's
// every step; M3's put the selected lane's notes on rows inside the range it
// labels; M4's carry each lane's range and transpose.
static void playing_pages() {
    const u32 at = 2 * kBarTicks;
    input.page_key = kConduct + 1;
    tick(at, true);
    static Bar bar;
    bool beats = true, many = true;
    for (u32 b = 0; b < kPhrase; ++b) {
        generate(state.song, state.globals, static_cast<i32>(b), bar);
        for (u32 l = 0; l < kLanes; ++l)
            for (u32 q = 0; q < 4; ++q) {
                const u32 cell = phrase_cell(shown(), l, 4 * b + q);
                beats = beats && (cell != kCellRest) == sounds(bar, l, 4 * q, 4 * q + 4);
                u32 starts = 0;
                for (u32 i = 0; i < bar.count; ++i)
                    starts += bar.events[i].lane == l && bar.events[i].tick / kTicks / 4 == q;
                many = many && (cell == kCellMany) == (starts >= 3);
            }
    }
    check(beats, "M1 marks each beat of the phrase a lane sounds in, and no other");
    check(many, "M1 marks a beat that starts three or more notes as full");

    input.page_key = kLane + 1;
    tick(at + 1, true);
    bool steps = state.current.count > 0;
    for (u32 l = 0; l < kLanes; ++l)
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 cell = bits_at(shown(), kStepCells, 2 * (l * kSteps + s), 2);
            const bool drum = state.song.lanes[l].main[kRoleSlot] == kDrum;
            bool starts = false;
            for (u32 i = 0; i < state.current.count; ++i)
                starts = starts || (state.current.events[i].lane == l && state.current.events[i].tick / kTicks == s);
            steps = steps && (drum ? (cell != kDrumNone) == starts : (cell != kCellRest) == sounds(state.current, l, s, s + 1));
        }
    check(steps, "M2 marks each step a lane sounds in, and no other");

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

    input.page_key = kRoute + 1;
    state.song.lanes[6].route[kLow] = 40;
    state.song.lanes[6].route[kHigh] = 91;
    state.song.lanes[6].route[kXpose] = -24;
    state.song.lanes[7].route[kXpose] = 24;
    tick(at + 3, true);
    bool ranges = true;
    for (u32 l = 0; l < kLanes; ++l) {
        const u32 r = lane_range(shown(), l);
        const Lane& lane = state.song.lanes[l];
        ranges = ranges && static_cast<i32>(field(r, 0, 7)) == lane.route[kLow] &&
                 static_cast<i32>(field(r, 7, 7)) == lane.route[kHigh] &&
                 static_cast<i32>(field(r, 14, 6)) - 24 == lane.route[kXpose];
    }
    check(ranges, "M4 carries every lane's range and transpose");
    tick(at + 4, false);
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

// Stopped, nothing regenerates or moves however the transport's ticks run,
// on any page, until a control changes. On the device this is the 027 fix:
// whatever made the song move while stopped, the control now ignores the
// stopped transport's position.
static void stopped_transport() {
    std::memset(&input, 0, sizeof input);
    std::memset(&state, 0, sizeof state);
    input.track = 11;
    input.transport.bpm_x10 = 1200;
    input.elapsed_us = 16667;
    const i32 words_in[kGlobals] = {choice_word(0, 12), choice_word(0, 7), choice_word(0, 6), percent_word(50),
                                    percent_word(40), choice_word(1, 4), 0, percent_word(30)};
    for (u32 i = 0; i < kGlobals; ++i) input.values[11][i] = words_in[i];
    control_reset(state);
    // Play into the song's sixth bar, then stop.
    for (u32 t = 0; t < 5 * kBarTicks + 7 * kTicks; t += 8) tick(t, true);
    bool quiet = true, still = true, held = true;
    for (u32 page = 1; page <= kPages; ++page) {
        input.page_key = page;
        // The lane lights fade for kFlashUs after the last note; then the
        // screen must hold.
        for (u32 i = 0; i < 12; ++i) tick(5 * kBarTicks + 7 * kTicks, false);
        const u32 generations = state.generations;
        i32 shown[kWords];
        std::memcpy(shown, overlay.display, sizeof shown);
        // The stopped transport's ticks: where play stopped, running on, back
        // to the start, or anything at all.
        u32 noise = 12345;
        for (u32 i = 0; i < 2000; ++i) {
            noise = noise * 1664525u + 1013904223u;
            const u32 ticks = i < 500 ? 5 * kBarTicks + 7 * kTicks : i < 1000 ? 5 * kBarTicks + 8 * i : i < 1500 ? 0 : noise;
            tick(ticks, false);
            quiet = quiet && overlay.note_count == 0;
            still = still && std::memcmp(shown, overlay.display, sizeof shown) == 0;
        }
        held = held && state.generations == generations && state.bar == 0;
    }
    check(quiet, "stopped, nothing plays");
    check(held, "stopped, no page regenerates the song, wherever the transport's ticks run");
    check(still, "stopped, no page's screen changes");
    // A control turned while stopped regenerates once, then holds again.
    input.values[11][kDensity] = percent_word(70);
    tick(0, false);
    const u32 after = state.generations;
    for (u32 i = 0; i < 200; ++i) tick(i * 37, false);
    check(after > 0 && state.generations == after, "stopped, a turned control regenerates once and then holds");
}

// At 60 GUI ticks a second, even the busiest song never starts more notes in
// one tick than a tick may start (note_capacity): none is dropped.
static void notes_per_tick() {
    std::memset(&input, 0, sizeof input);
    std::memset(&state, 0, sizeof state);
    input.track = 11;
    input.transport.bpm_x10 = 2400;
    const i32 words_in[kGlobals] = {choice_word(0, 12), choice_word(0, 7), choice_word(0, 6), percent_word(100),
                                    percent_word(100), choice_word(0, 4), percent_word(50), percent_word(100)};
    for (u32 i = 0; i < kGlobals; ++i) input.values[11][i] = words_in[i];
    control_reset(state);
    tick(0, false);
    // 240 bpm is 384 ticks96 a second, 6.4 a GUI tick at 60 a second.
    u32 most = 0, total = 0, expected = 0;
    static Bar bar;
    for (u32 b = 0; b < 16; ++b) {
        generate(state.song, state.globals, static_cast<i32>(b), bar);
        expected += bar.count;
    }
    for (u32 frame = 0; frame * 32 / 5 < 16 * kBarTicks; ++frame) {
        tick(frame * 32 / 5, true);
        most = overlay.note_count > most ? overlay.note_count : most;
        total += overlay.note_count;
    }
    check(total == expected && most * 2 <= opxy::control::note_capacity,
          "at 60 ticks a second and 240 bpm, every note plays and no tick starts half note_capacity");
}

int main() {
    globals_decode();
    generation();
    freezing();
    controlling();
    stopped_transport();
    notes_per_tick();
    if (!failures) std::puts("ok");
    return failures ? 1 : 0;
}
