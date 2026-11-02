#pragma once
// The words the control passes its display each tick (Overlay::display): the
// display reads the saved controls itself, and everything else from these.
// The last nine words carry what the shown page draws, laid out by page.
#include "generator.h"

namespace conductor {
enum Word {
    kState,     // page 2 | lane 3 | shift 1 | playing 1 | step 4 | bar in phrase 2 | frozen 1 | lane frozen 1 | variation 2 | lane variation 2
    kValues0,   // the page's slots 0..3, each value + 128
    kValues1,   // and 4..7
    kSeed,      // seed label 16 | fill 7 | lane fill 7
    kRolesWord, // each lane's role, 3 bits
    kTargets,   // each lane's track, 3 bits
    kActivity,  // lanes that just played 8 | muted lanes 8
    kPayload,
    kWords = kPayload + 9,
};
// M1: the harmony, then the phrase's rhythm, a word a bar, 2 bits a step.
enum ConductWord { kChords = kPayload, kPhraseRhythm };  // kChords: four degrees, 3 bits each | position 2 | seventh 1 | next degree 3 | next seventh 1
enum Beat { kBeatRest, kBeatAccent, kBeatBack, kBeatKick };
// M2: each lane's bar, 16 steps a lane, two lanes a word; then its accents.
enum LanesWord { kHits = kPayload, kAccents = kPayload + 4 };
// M3: the selected lane's bar on twelve rows, or eight drum voices.
enum RollWord { kRange = kPayload, kRoll };  // kRange: lowest row's pitch 7 | semitones a row 4 | drum 1
const u32 kRollRows = 12, kDrumRows = 8;
const u32 kAccentVelocity = 100;

struct Words { u32 v[kWords]; };
struct Position { u32 tick; bool playing; };

inline u32 field(u32 word, u32 shift, u32 bits) { return word >> shift & ((1u << bits) - 1u); }
inline bool bit(const u32* w, u32 first, u32 index) { return (w[first + index / 32] >> (index % 32) & 1) != 0; }
inline void set_bit(Words& w, u32 first, u32 index) { w.v[first + index / 32] |= 1u << (index % 32); }

inline void conduct_words(const Song& song, const Globals& globals, i32 bar_index, u32 step, Words& w) {
    World world(song, globals);
    const Chord chord = world.chord_at(bar_index * static_cast<i32>(kSteps) + static_cast<i32>(step));
    const Chord next = world.chord_at(chord.start + chord.length);
    const i8* loop = world.progression(phrase_of(floor_div(chord.index * chord.length, kSteps)));
    for (u32 i = 0; i < 4; ++i) w.v[kChords] |= static_cast<u32>(loop[i] & 7) << (3 * i);
    w.v[kChords] |= static_cast<u32>(chord.position) << 12 | (chord.seventh ? 1u : 0u) << 14 |
                    static_cast<u32>(((next.degree % 7) + 7) % 7) << 15 | (next.seventh ? 1u : 0u) << 18;
    const i32 first = phrase_of(bar_index) * static_cast<i32>(kPhrase);
    for (u32 b = 0; b < kPhrase; ++b) {
        const World::Skeleton s = world.skeleton(first + static_cast<i32>(b));
        for (u32 i = 0; i < kSteps; ++i) {
            const u32 beat = s.kick >> i & 1 ? kBeatKick : s.snare >> i & 1 ? kBeatBack : s.accent >> i & 1 ? kBeatAccent : kBeatRest;
            w.v[kPhraseRhythm + b] |= beat << (2 * i);
        }
    }
}

inline void lanes_words(const Bar& bar, Words& w) {
    for (u32 i = 0; i < bar.count; ++i) {
        const Event& e = bar.events[i];
        const u32 index = e.lane * kSteps + e.tick / kTicks;
        set_bit(w, kHits, index);
        if (e.velocity >= kAccentVelocity) set_bit(w, kAccents, index);
    }
}

// Twelve rows of one semitone, centred on the notes, or of as many as the
// lane's span needs.
inline void roll_words(const Bar& bar, u32 lane, bool drum, Words& w) {
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

inline void words(const Song& song, u32 page, u32 lane_index, const Bar& bar, i32 bar_index, Position at,
                  bool shift, const u32* flash_us, const Globals& globals, Words& w) {
    for (u32 i = 0; i < kWords; ++i) w.v[i] = 0;
    const Lane& lane = song.lanes[lane_index];
    const u32 step = at.tick / kTicks % kSteps;
    w.v[kState] = page | lane_index << 2 | (shift ? 1u : 0u) << 5 | (at.playing ? 1u : 0u) << 6 | step << 7 |
                  static_cast<u32>(((bar_index % 4) + 4) % 4) << 11 | (song.frozen_at >= 0 ? 1u : 0u) << 13 |
                  (lane.frozen_at >= 0 ? 1u : 0u) << 14 | (song.variation & 3u) << 15 | (lane.variation & 3u) << 17;
    const u32 role = static_cast<u32>(lane.main[kRoleSlot]);
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
        w.v[kRolesWord] |= static_cast<u32>(other.main[kRoleSlot] & 7) << (3 * l);
        w.v[kTargets] |= static_cast<u32>(other.route[kTargetSlot] & 7) << (3 * l);
        if (flash_us[l]) w.v[kActivity] |= 1u << l;
        if (other.route[kMute]) w.v[kActivity] |= 1u << (8 + l);
    }
    switch (page) {
    case kConduct: conduct_words(song, globals, bar_index, step, w); break;
    case kLane: lanes_words(bar, w); break;
    case kDetail: roll_words(bar, lane_index, role == kDrum, w); break;
    default: break;
    }
}
}
