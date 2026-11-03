#pragma once
// The words the control passes its display each tick (Overlay::display): the
// display reads the saved controls itself, and everything else from these.
// The last nine words carry what the shown page draws, laid out by page; M1
// also borrows the two value words, which only M2..M4's header reads.
#include "generator.h"

namespace conductor {
enum Word {
    kState,     // page 2 | lane 3 | shift 1 | playing 1 | step 4 | bar in phrase 2 | frozen 1 | lane frozen 1 | variation 2 | lane variation 2 | phrase 5
    kValues0,   // M2..M4: the page's slots 0..3, each value + 128
    kValues1,   // and 4..7
    kSeed,      // seed label 16 | fill 7 | lane fill 7
    kRolesWord, // each lane's role, 3 bits | lanes 5..6's style, 3 bits
    kTargets,   // each lane's track, 3 bits | lane 7's style, 3 bits
    kActivity,  // lanes that just played 8 | muted lanes 8 | lanes 0..4's style, 3 bits
    kPayload,
    kWords = kPayload + 9,
};

// A cell: what one lane does in one beat (M1) or one step (M2), 2 bits.
enum Cell { kCellRest, kCellHeld, kCellNotes, kCellMany };
// A drum lane's step on M2: its lowest voice.
enum DrumCell { kDrumNone, kDrumHigh, kDrumMid, kDrumLow };
// The shared rhythm, a beat (M1) or a step (M2) at a time, 2 bits.
enum Beat { kBeatRest, kBeatAccent, kBeatBack, kBeatKick };

// M1: the harmony, the phrase's groove by beat, then every lane's beats.
enum ConductWord { kChords = kPayload, kGroove };  // kChords: four degrees, 3 bits each | position 2 | seventh 1 | next degree 3 | next seventh 1
static const u8 kPhraseCellWords[8] = {kValues0, kValues1, kPayload + 2, kPayload + 3,
                                       kPayload + 4, kPayload + 5, kPayload + 6, kPayload + 7};
// M2: every lane's steps, then the bar's groove by step.
enum LanesWord { kStepCells = kPayload, kStepGroove = kPayload + 8 };
// M3: the selected lane's bar on twelve rows, or eight drum voices; then the harmony.
enum RollWord { kRange = kPayload, kRoll, kRollChords = kPayload + 7 };  // kRange: lowest row's pitch 7 | semitones a row 4 | drum 1
const u32 kRollRows = 12, kDrumRows = 8;

struct Words { u32 v[kWords]; };
struct Position { u32 tick; bool playing; };

inline u32 field(u32 word, u32 shift, u32 bits) { return word >> shift & ((1u << bits) - 1u); }
inline bool bit(const u32* w, u32 first, u32 index) { return (w[first + index / 32] >> (index % 32) & 1) != 0; }
inline void set_bit(Words& w, u32 first, u32 index) { w.v[first + index / 32] |= 1u << (index % 32); }
inline u32 bits_at(const u32* w, u32 first, u32 index, u32 bits) { return field(w[first + index / 32], index % 32, bits); }
inline void put_bits(Words& w, u32 first, u32 index, u32 value) { w.v[first + index / 32] |= value << (index % 32); }

// A lane's style: the choice in its role's first control (a bass's "fifth").
inline void style_place(u32 lane, u32& word, u32& shift) {
    if (lane < 5) word = kActivity, shift = 16 + 3 * lane;
    else if (lane < 7) word = kRolesWord, shift = 24 + 3 * (lane - 5);
    else word = kTargets, shift = 24;
}
inline u32 lane_style(const u32* w, u32 lane) {
    u32 word, shift;
    style_place(lane, word, shift);
    return field(w[word], shift, 3);
}

// M1's beats: lane-major, sixteen beats a lane, 2 bits each.
inline u32 phrase_cell(const u32* w, u32 lane, u32 beat) {
    const u32 index = 2 * (lane * 16 + beat);
    return field(w[kPhraseCellWords[index / 32]], index % 32, 2);
}

// What a lane does over steps [first, last) of a bar: rests, holds a note
// from before, starts one or two notes, or starts three or more.
inline u32 cell_of(const Bar& bar, u32 lane, u32 first, u32 last) {
    const u32 from = first * kTicks, to = last * kTicks;
    u32 starts = 0;
    bool held = false;
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        if (e.lane != lane) continue;
        if (e.tick >= from && e.tick < to) ++starts;
        else if (e.tick < from && e.tick + e.length > from) held = true;
    }
    return starts >= 3 ? kCellMany : starts ? kCellNotes : held ? kCellHeld : kCellRest;
}
// A drum step: its lowest voice. Kick and low tom sit low; snare, clap and
// rim in the middle; hats and the high tom high.
inline u32 drum_cell(const Bar& bar, u32 lane, u32 step) {
    static const u8 kHeight[8] = {kDrumLow, kDrumMid, kDrumMid, kDrumMid, kDrumHigh, kDrumHigh, kDrumHigh, kDrumLow};
    u32 cell = kDrumNone;
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        if (e.lane != lane || e.tick / kTicks != step) continue;
        const u32 h = e.voice < 8 ? kHeight[e.voice] : static_cast<u32>(kDrumMid);
        if (h > cell) cell = h;
    }
    return cell;
}
inline u32 beat_of(u32 kick, u32 snare, u32 accent, u32 first, u32 count) {
    const u32 mask = ((1u << count) - 1u) << first;
    return kick & mask ? kBeatKick : snare & mask ? kBeatBack : accent & mask ? kBeatAccent : kBeatRest;
}

// The word builders stay out of line: inlined, they grew the control's one
// function past the reach of Blackfin's short conditional jumps.

// The phrase's beats and groove: four bars' generation, which the control
// works out once a phrase, or when something changes, and keeps.
struct PhraseCells { u32 cells[8]; u32 groove; };

__attribute__((noinline)) inline void phrase_cells(const Song& song, const Globals& globals, i32 phrase, Bar& scratch, PhraseCells& out) {
    for (u32 i = 0; i < 8; ++i) out.cells[i] = 0;
    out.groove = 0;
    World world(song, globals);
    for (u32 b = 0; b < kPhrase; ++b) {
        const i32 bar = phrase * static_cast<i32>(kPhrase) + static_cast<i32>(b);
        generate(song, globals, bar, scratch);
        const World::Skeleton s = world.skeleton(bar);
        for (u32 q = 0; q < 4; ++q) {
            const u32 beat = 4 * b + q;
            out.groove |= beat_of(s.kick, s.snare, s.accent, 4 * q, 4) << (2 * beat);
            for (u32 l = 0; l < kLanes; ++l) {
                const u32 index = 2 * (l * 16 + beat);
                out.cells[index / 32] |= cell_of(scratch, l, 4 * q, 4 * q + 4) << (index % 32);
            }
        }
    }
}

inline u32 chords_word(const Song& song, const Globals& globals, i32 bar_index, u32 step) {
    World world(song, globals);
    const Chord chord = world.chord_at(bar_index * static_cast<i32>(kSteps) + static_cast<i32>(step));
    const Chord next = world.chord_at(chord.start + chord.length);
    const i8* loop = world.progression(phrase_of(floor_div(chord.index * chord.length, kSteps)));
    u32 out = 0;
    for (u32 i = 0; i < 4; ++i) out |= static_cast<u32>(loop[i] & 7) << (3 * i);
    return out | static_cast<u32>(chord.position) << 12 | (chord.seventh ? 1u : 0u) << 14 |
           static_cast<u32>(((next.degree % 7) + 7) % 7) << 15 | (next.seventh ? 1u : 0u) << 18;
}

__attribute__((noinline)) inline void lanes_words(const Song& song, const Globals& globals, const Bar& bar, i32 bar_index, Words& w) {
    for (u32 l = 0; l < kLanes; ++l) {
        const bool drum = song.lanes[l].main[kRoleSlot] == kDrum;
        for (u32 s = 0; s < kSteps; ++s)
            put_bits(w, kStepCells, 2 * (l * kSteps + s), drum ? drum_cell(bar, l, s) : cell_of(bar, l, s, s + 1));
    }
    World world(song, globals);
    const World::Skeleton sk = world.skeleton(bar_index);
    for (u32 s = 0; s < kSteps; ++s) w.v[kStepGroove] |= beat_of(sk.kick, sk.snare, sk.accent, s, 1) << (2 * s);
}

// Twelve rows of one semitone, centred on the notes, or of as many as the
// lane's span needs.
__attribute__((noinline)) inline void roll_words(const Bar& bar, u32 lane, bool drum, Words& w) {
    i32 low = 127, high = 0;
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        if (e.lane != lane) continue;
        if (e.pitch < low) low = e.pitch;
        if (e.pitch > high) high = e.pitch;
    }
    if (low > high) low = high = 60;
    const i32 rows = kRollRows, span = high - low + 1;
    const i32 band = drum || span <= rows ? 1 : (span + rows - 1) / rows;
    if (band == 1) low = clampi(low - (rows - span) / 2, 0, 128 - rows);
    w.v[kRange] = static_cast<u32>(low) | static_cast<u32>(band) << 7 | (drum ? 1u : 0u) << 11;
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        if (e.lane != lane) continue;
        const u32 row = drum ? e.voice % kDrumRows : static_cast<u32>((e.pitch - low) / band);
        const u32 first = e.tick / kTicks, last = drum ? first : (e.tick + e.length - 1u) / kTicks;
        for (u32 s = first; s <= last && s < kSteps; ++s) set_bit(w, kRoll, row * kSteps + s);
    }
}

__attribute__((noinline)) inline void words(const Song& song, u32 page, u32 lane_index, const Bar& bar, i32 bar_index, Position at,
                  bool shift, const u32* flash_us, const Globals& globals, const PhraseCells& phrase, Words& w) {
    for (u32 i = 0; i < kWords; ++i) w.v[i] = 0;
    const Lane& lane = song.lanes[lane_index];
    const u32 step = at.tick / kTicks % kSteps;
    w.v[kState] = page | lane_index << 2 | (shift ? 1u : 0u) << 5 | (at.playing ? 1u : 0u) << 6 | step << 7 |
                  static_cast<u32>(((bar_index % 4) + 4) % 4) << 11 | (song.frozen_at >= 0 ? 1u : 0u) << 13 |
                  (lane.frozen_at >= 0 ? 1u : 0u) << 14 | (song.variation & 3u) << 15 | (lane.variation & 3u) << 17 |
                  static_cast<u32>(phrase_of(bar_index) & 31) << 19;
    const u32 role = static_cast<u32>(lane.main[kRoleSlot]);
    if (page != kConduct)
        for (u32 s = 0; s < 8; ++s) {
            i32 value = 0;
            if (page == kLane) value = s == 0 ? static_cast<i32>(lane_index) : lane.main[s];
            else if (page == kRoute) value = s == 0 ? static_cast<i32>(lane_index) : lane.route[s];
            else if (page == kDetail && role > kOff && role < kRoles) value = lane.detail[role - 1][s];
            w.v[kValues0 + s / 4] |= static_cast<u32>(value + 128) << (8 * (s % 4));
        }
    w.v[kSeed] = seed_label(song.seed) | static_cast<u32>(song.fill) << 16 | static_cast<u32>(lane.fill) << 23;
    for (u32 l = 0; l < kLanes; ++l) {
        const Lane& other = song.lanes[l];
        const u32 r = static_cast<u32>(other.main[kRoleSlot] & 7);
        w.v[kRolesWord] |= r << (3 * l);
        w.v[kTargets] |= static_cast<u32>(other.route[kTargetSlot] & 7) << (3 * l);
        if (flash_us[l]) w.v[kActivity] |= 1u << l;
        if (other.route[kMute]) w.v[kActivity] |= 1u << (8 + l);
        if (r > kDrum && r < kRoles) {
            u32 word, at_bit;
            style_place(l, word, at_bit);
            w.v[word] |= static_cast<u32>(other.detail[r - 1][0] & 7) << at_bit;
        }
    }
    switch (page) {
    case kConduct:
        w.v[kChords] = chords_word(song, globals, bar_index, step);
        for (u32 i = 0; i < 8; ++i) w.v[kPhraseCellWords[i]] = phrase.cells[i];
        w.v[kGroove] = phrase.groove;
        break;
    case kLane: lanes_words(song, globals, bar, bar_index, w); break;
    case kDetail:
        roll_words(bar, lane_index, role == kDrum, w);
        w.v[kRollChords] = chords_word(song, globals, bar_index, step);
        break;
    default: break;  // M4 draws from the lanes' roles, targets and mutes alone
    }
}
}
