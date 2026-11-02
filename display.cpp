#include <opxy/track_control.hpp>
#include "display.h"
#include "words.h"

namespace conductor {
using namespace opxy::draw;

// Node slots, in tools/artwork.py's shapes.
enum Node {
    kBar = 0,           // 8: the two-row header's stepped bar
    kHighlight = 8,     // M2, M4: the selected lane's row
    kChordBoxes = 9,    // 4: M1, the phrase's chords
    kChordMark = 13,    // M1: under the sounding chord
    kPlayhead = 14,     // M3
    kPageMarks = 15,    // 4: over M1..M4, the shown page's bright
    kStrikes = 19,      // 8: M4, through muted lanes
    kCircles = 27,      // 15: M1's seven degrees, then each lane's light
    kLaneLights = kCircles + 7,
    kCells = 42,        // 192, 24 x 10 at unit scale: M1's phrase, M2's lanes, M3's roll, M4's routes
    kNodes = 234,
};
// Text slots: the header's sixteen, then the pages' own.
enum Text {
    kRows = 16,         // 8 at 12 px
    kChordName = 24,    // 20 px
    kPageLabel = 25,    // the shown page's, bright
    kOtherPages = 26,   // 3, grey
    kNotes = 29,        // 3 at 10 px, grey
};
// The palette: sixteen greys, black to white.
enum Grey { kBlack = 0, kDeep = 1, kDark = 2, kFaint = 3, kDim = 5, kMid = 8, kLight = 11, kBright = 13, kWhite = 15 };
static const char* const kVoiceNames[8] = {"kick", "snare", "clap", "rim", "hat", "open", "tom", "low"};
static const char* const kActions[4] = {"reroll", "freeze", "fill", "vary"};
// Advance widths of ' '..'~' in the screen font at 10 px, in tenths of a pixel.
static const u8 kGlyphWidths[95] = {
    28, 32, 36, 56, 56, 92, 69, 21, 29, 29, 44, 68, 28, 33, 28, 29, 56, 56, 56, 56, 56, 56, 56, 56,
    56, 56, 28, 28, 68, 68, 68, 48, 98, 69, 66, 66, 73, 60, 56, 72, 74, 28, 49, 62, 56, 94, 75, 74,
    60, 74, 66, 62, 55, 74, 65, 102, 64, 61, 56, 29, 29, 29, 68, 51, 29, 54, 58, 52, 58, 53, 32, 59,
    57, 24, 24, 51, 24, 89, 57, 57, 58, 58, 34, 48, 32, 57, 51, 82, 51, 51, 44, 38, 25, 38, 68};

inline i32 px(i32 pixels) { return pixels * unit; }
inline void park_text(Frame& frame, u32 slot) { text(frame, slot, px(hidden_x), px(hidden_y), unit, ""); }
inline void show(Frame& frame, u32 node, i32 x, i32 y, u32 shade, i32 scale = unit) {
    place(frame, node, x, y, scale, shade, fill_and_stroke);
}
// Q10 pixels.
inline i32 text_width(const char* s, i32 size) {
    i32 tenths = 0;
    for (; *s; ++s) tenths += *s >= ' ' && *s <= '~' ? kGlyphWidths[*s - ' '] : 56;
    return tenths * size * unit / 100;
}
inline void centred(Frame& frame, u32 slot, i32 x, i32 baseline, const char* s, i32 size) {
    text(frame, slot, x - text_width(s, size) / 2, px(baseline), unit, s);
}

inline void append(char* out, u32& n, const char* s) {
    while (*s && n + 1 < text_capacity) out[n++] = *s++;
    out[n] = 0;
}
inline void append_number(char* out, u32& n, i32 v) {
    char digits[12];
    number(digits, v, false);
    append(out, n, digits);
}
inline void hex4(char* out, u32 v) {
    static const char* const digits = "0123456789abcdef";
    for (u32 i = 0; i < 4; ++i) out[i] = digits[v >> (12 - 4 * i) & 15];
    out[4] = 0;
}

inline void global_text(char* out, u32 slot, i32 v) {
    const char* name = nullptr;
    switch (slot) {
    case kKey: name = kNoteNames[v]; break;
    case kScale: name = kScaleNames[v]; break;
    case kProgression: name = kProgressionNames[v]; break;
    case kHarmony: name = kHarmonyNames[v]; break;
    default: number(out, v, false); return;
    }
    copy(out, header_capacity, name);
}

// The header's titles and values by slot: the page's controls, or on Shift the
// actions encoders 1..4 take, on both rows.
inline void header_captions(const Input& input, const u32* w, const char* (&titles)[8],
                            char (&values)[8][header_capacity]) {
    const u32 page = field(w[kState], 0, 2), lane = field(w[kState], 2, 3);
    const bool shift = field(w[kState], 5, 1) != 0, all = page == kConduct;
    const u32 role = field(w[kRolesWord], 3 * lane, 3);
    for (u32 slot = 0; slot < 8; ++slot) {
        char* value = values[slot];
        value[0] = 0;
        if (shift) {
            const u32 action = slot % 4;
            titles[slot] = kActions[action];
            if (action == 0 && all) hex4(value, field(w[kSeed], 0, 16));
            else if (action == 0) copy(value, header_capacity, "lane");
            else if (action == 1) format(value, kRouteSpecs[kMute], static_cast<i32>(field(w[kState], all ? 13 : 14, 1)));
            else if (action == 2) number(value, static_cast<i32>(field(w[kSeed], all ? 16 : 23, 7)), false);
            else number(value, static_cast<i32>(field(w[kState], all ? 15 : 17, 2)), false);
        } else if (page == kConduct) {
            titles[slot] = kGlobalTitles[slot];
            global_text(value, slot, global_value(slot, input.slots[slot]));
        } else {
            const Spec& s = spec(page, slot, role);
            titles[slot] = s.title;
            format(value, s, static_cast<i32>(field(w[kValues0 + slot / 4], 8 * (slot % 4), 8)) - 128);
        }
    }
}

// A lane's light: bright as it plays, fading through the greys.
inline u32 glow_shade(u32 glow, u32 rest) { return rest + (glow * (kWhite - rest) >> 10); }
inline bool lane_silent(const u32* w, u32 lane) {
    return field(w[kRolesWord], 3 * lane, 3) == kOff || field(w[kActivity], 8 + lane, 1);
}
inline void lane_light(const u32* w, const DisplayState& state, u32 lane, i32 x, i32 y, Frame& frame) {
    show(frame, kLaneLights + lane, px(x), px(y), lane_silent(w, lane) ? kFaint : glow_shade(state.glow[lane], kDim));
}
inline void lane_label(const u32* w, u32 lane, i32 x, i32 baseline, Frame& frame) {
    char line[text_capacity];
    u32 n = 0;
    line[0] = 0;
    append_number(line, n, static_cast<i32>(lane) + 1);
    append(line, n, " ");
    append(line, n, kRoleNames[field(w[kRolesWord], 3 * lane, 3) % kRoles]);
    text(frame, kRows + lane, px(x), px(baseline), unit, line);
}
inline void lane_highlight(u32 selected, i32 top, i32 pitch, Frame& frame) {
    show(frame, kHighlight, 0, px(top + pitch * static_cast<i32>(selected) - 1), kDark);
}

// M1: the chord wheel, the progression, the phrase's rhythm and the lanes.
inline void draw_conduct(const Input& input, const u32* w, const DisplayState& state, Frame& frame) {
    const i32 key = global_value(kKey, input.slots[kKey]), scale = global_value(kScale, input.slots[kScale]);
    const u32 chords = w[kChords], position = field(chords, 12, 2), next = field(chords, 15, 3);
    const u32 state_word = w[kState], step = field(state_word, 7, 4), bar = field(state_word, 11, 2);
    const bool seventh = field(chords, 14, 1) != 0, playing = field(state_word, 6, 1) != 0;
    const u32 current = field(chords, 3 * position, 3);

    // The seven degrees clockwise from the top; the loop's lit and numbered.
    const i32 cx = 84, cy = 120, radius = 56;
    u32 used = 0;
    for (u32 i = 0; i < 4; ++i) used |= 1u << field(chords, 3 * i, 3);
    u32 numbered = 0;
    for (u32 d = 0; d < 7; ++d) {
        const i32 angle = -900 + static_cast<i32>(d) * 3600 / 7;
        const i32 c = sine(angle + 900), s = sine(angle);
        const bool on = d == current;
        show(frame, kCircles + d, px(cx) + radius * c, px(cy) + radius * s,
             on ? kWhite : d == next ? kLight : used >> d & 1 ? kMid : kFaint, on ? 2 * unit : unit * 8 / 5);
        if (!(used >> d & 1)) continue;
        char roman[8];
        numeral(roman, key, scale, static_cast<i32>(d));
        centred(frame, kRows + 4 + numbered++, px(cx) + (radius + 22) * c, cy + 4 + (radius + 22) * s / unit, roman, 12);
    }
    char name[12];
    chord_name(name, key, scale, static_cast<i32>(current), seventh);
    centred(frame, kChordName, px(cx), cy + 7, name, 20);
    char line[text_capacity];
    u32 n = 0;
    line[0] = 0;
    append(line, n, "next ");
    chord_name(name, key, scale, static_cast<i32>(next), field(chords, 18, 1) != 0);
    append(line, n, name);
    centred(frame, kNotes, px(cx), cy + 22, line, 10);

    const i32 x0 = 184;
    for (u32 i = 0; i < 4; ++i) {
        const i32 x = x0 + 74 * static_cast<i32>(i);
        show(frame, kChordBoxes + i, px(x), px(46), i == position ? kDim : kDark);
        chord_name(name, key, scale, static_cast<i32>(field(chords, 3 * i, 3)), seventh);
        centred(frame, kRows + i, px(x + 34), 65, name, 12);
    }
    show(frame, kChordMark, px(x0 + 74 * static_cast<i32>(position)), px(74), kWhite);

    // The phrase, a row a bar: kicks brightest, then backbeats, then accents.
    static const u8 kBeatShades[4] = {kDeep, kDim, kLight, kBright};
    for (u32 b = 0; b < kPhrase; ++b)
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 beat = field(w[kPhraseRhythm + b], 2 * s, 2);
            const bool now = b == bar && s == step;
            const u32 shade = playing && now ? kWhite : beat == kBeatRest && b == bar ? kDark : kBeatShades[beat];
            show(frame, kCells + b * kSteps + s, px(x0 + 18 * static_cast<i32>(s) + 2 * static_cast<i32>(s / 4)),
                 px(84 + 11 * static_cast<i32>(b)), shade, unit * 3 / 5);
        }

    n = 0;
    line[0] = 0;
    append(line, n, "seed ");
    char seed[8];
    hex4(seed, field(w[kSeed], 0, 16));
    append(line, n, seed);
    append(line, n, "   var ");
    append_number(line, n, static_cast<i32>(field(state_word, 15, 2)));
    text(frame, kNotes + 1, px(x0), px(144), unit, line);
    const char* const motion = field(state_word, 13, 1) ? "frozen" : "evolving";
    text(frame, kNotes + 2, px(476) - text_width(motion, 10), px(144), unit, motion);
    for (u32 l = 0; l < kLanes; ++l) lane_light(w, state, l, x0 + 6 + 40 * static_cast<i32>(l), 172, frame);
}

// M2: every lane's bar, a row a lane, accents brightest.
inline void draw_lanes(const u32* w, u32 selected, const DisplayState& state, Frame& frame) {
    const i32 top = 43, pitch = 19;
    const u32 step = field(w[kState], 7, 4);
    const bool playing = field(w[kState], 6, 1) != 0;
    lane_highlight(selected, top, pitch, frame);
    for (u32 l = 0; l < kLanes; ++l) {
        const i32 y = top + pitch * static_cast<i32>(l);
        const bool silent = lane_silent(w, l);
        lane_light(w, state, l, 7, y + 8, frame);
        lane_label(w, l, 16, y + 13, frame);
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 index = l * kSteps + s;
            const bool live = playing && s == step;
            const u32 shade = bit(w, kHits, index) ? (silent ? kDim : bit(w, kAccents, index) || live ? kWhite : kLight)
                                                   : live ? kDim : s % 4 == 0 ? kFaint : kDark;
            show(frame, kCells + index, px(66 + 24 * static_cast<i32>(s) + 3 * static_cast<i32>(s / 4)), px(y + 4),
                 shade, unit * 9 / 10);
        }
    }
}

// M3: the selected lane's bar, its notes held across their steps, labelled
// with its range; a drum lane's voices on their own rows.
inline void draw_roll(const Input& input, const u32* w, u32 lane, Frame& frame) {
    if (field(w[kRolesWord], 3 * lane, 3) == kOff) {
        char line[text_capacity];
        u32 n = 0;
        line[0] = 0;
        append(line, n, "lane ");
        append_number(line, n, static_cast<i32>(lane) + 1);
        append(line, n, " is off");
        centred(frame, kRows, px(240), 118, line, 12);
        centred(frame, kNotes, px(240), 134, "give it a role on M2", 10);
        return;
    }
    const bool drum = field(w[kRange], 11, 1) != 0, playing = field(w[kState], 6, 1) != 0;
    const i32 low = static_cast<i32>(field(w[kRange], 0, 7)), band = static_cast<i32>(field(w[kRange], 7, 4));
    const i32 key = global_value(kKey, input.slots[kKey]);
    const u32 rows = drum ? kDrumRows : kRollRows, step = field(w[kState], 7, 4);
    const i32 top = 44, pitch = drum ? 19 : 13;
    for (u32 r = 0; r < rows; ++r) {
        // Row 0, the lowest voice or pitch, at the bottom.
        const i32 y = top + pitch * static_cast<i32>(rows - 1 - r);
        const bool root = !drum && band == 1 && (low + static_cast<i32>(r)) % 12 == key;
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 index = r * kSteps + s;
            const u32 rest = (root ? kFaint : kDeep) + (s % 4 == 0 ? 1 : 0);
            const u32 shade = bit(w, kRoll, index) ? (playing && s == step ? kWhite : kBright) : rest;
            show(frame, kCells + index, px(44 + 27 * static_cast<i32>(s)), px(y), shade);
        }
        if (drum) text(frame, kRows + r, px(4), px(y + 9), unit, kVoiceNames[r]);
    }
    if (!drum) {
        char label[8];
        note_name(label, low + band * static_cast<i32>(rows) - 1);
        text(frame, kRows, px(4), px(top + 9), unit, label);
        note_name(label, low);
        text(frame, kRows + 1, px(4), px(top + pitch * static_cast<i32>(rows - 1) + 9), unit, label);
    }
    if (playing) show(frame, kPlayhead, px(44 + 27 * static_cast<i32>(step) + 11), px(top), kMid);
}

// M4: where each lane plays, a column a track.
inline void draw_route(const u32* w, u32 selected, const DisplayState& state, Frame& frame) {
    const i32 top = 57, pitch = 17;
    const i32 column0 = px(120), column = px(336) / 10;  // 33.6 px: a digit and ten spaces
    // Each caption holds the column labels three apart by spaces alone.
    static const char* const kColumns[3] = {"1          2          3", "4          5          6", "7          8"};
    for (u32 i = 0; i < 3; ++i)
        text(frame, kNotes + i, column0 + 3 * static_cast<i32>(i) * column - px(28) / 10, px(52), unit, kColumns[i]);
    lane_highlight(selected, top, pitch, frame);
    for (u32 l = 0; l < kLanes; ++l) {
        const i32 y = top + pitch * static_cast<i32>(l);
        const u32 target = field(w[kTargets], 3 * l, 3);
        const bool off = field(w[kRolesWord], 3 * l, 3) == kOff, muted = field(w[kActivity], 8 + l, 1) != 0;
        lane_light(w, state, l, 7, y + 8, frame);
        lane_label(w, l, 16, y + 12, frame);
        for (u32 t = 0; t < kLanes; ++t) {
            const i32 cx = column0 + static_cast<i32>(t) * column;
            if (t == target) {
                show(frame, kCells + l * kLanes + t, cx - px(72) / 10, px(y + 5),
                     off || muted ? kDim : glow_shade(state.glow[l], kLight), unit * 3 / 5);
            } else {
                show(frame, kCells + l * kLanes + t, cx - px(3), px(y + 7), t == l ? kDim : kFaint, unit / 4);
            }
        }
        if (muted && !off) show(frame, kStrikes + l, px(106), px(y + 8), kMid);
    }
}

// The pages over M1..M4, a quarter of the screen each.
inline void draw_pages(u32 page, Frame& frame) {
    u32 other = kOtherPages;
    for (u32 p = 0; p < kPages; ++p) {
        const i32 x = 120 * static_cast<i32>(p);
        show(frame, kPageMarks + p, px(x + 2), px(202), p == page ? kWhite : kFaint);
        text(frame, p == page ? kPageLabel : other++, px(x + 4), px(216), unit, kPageNames[p]);
    }
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    for (u32 l = 0; l < kLanes; ++l) state.glow[l] = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    u32 w[kWords];
    for (u32 i = 0; i < kWords; ++i) w[i] = static_cast<u32>(opxy::control::from_control(i));
    const u32 page = field(w[kState], 0, 2), lane = field(w[kState], 2, 3);

    const char* titles[8];
    char values[8][header_capacity];
    header_captions(input, w, titles, values);
    page_header(state.header, input, frame, titles, values, kBar);

    // A lane that just played lights at once, then fades over 0.2 s.
    const i32 fade = clamp(input.elapsed, 0, 1024) * 5;
    for (u32 l = 0; l < kLanes; ++l) {
        const i32 glow = field(w[kActivity], l, 1) ? 1024 : static_cast<i32>(state.glow[l]) - fade;
        state.glow[l] = static_cast<u16>(input.reduced_motion && !field(w[kActivity], l, 1) ? 0 : clamp(glow, 0, 1024));
    }

    for (u32 i = kHighlight; i < kNodes; ++i) hide(frame, i);
    for (u32 i = kRows; i < Frame::text_count; ++i) park_text(frame, i);
    switch (page) {
    case kLane: draw_lanes(w, lane, state, frame); break;
    case kDetail: draw_roll(input, w, lane, frame); break;
    case kRoute: draw_route(w, lane, state, frame); break;
    default: draw_conduct(input, w, state, frame);
    }
    draw_pages(page, frame);
}
}
