#include <opxy/track_control.hpp>
#include "display.h"
#include "words.h"

namespace conductor {
using namespace opxy::draw;

// One layout on every page, left to right: the lanes' labels, their bars
// (x 86..410), wires, then the tracks they play (x 446). Eight rows, one a
// lane, at the same height on every page.
//
// Node slots, in tools/artwork.py's shapes.
enum Node {
    kBar = 0,            // 8: the two-row header's stepped bar
    kPageMark = 8,       // under the shown page's label
    kTrackBoxes = 9,     // 8: tracks 1..8, down the right
    kWires = 17,         // 8: each lane to its track, a line turned and scaled
    kAccent = 25,        // M2..M4: beside the selected lane
    kRowBand = 26,       // M2..M4: behind the selected lane
    kBarBand = 27,       // M1: behind the bar playing
    kStepBand = 28,      // M2, M3: behind the step playing
    kGridLines = 29,     // 3: between bars or beats; M4's octave guides
    kPlayhead = 32,      // M1
    kPlayMark = 33,      // M1: the playhead's head
    kUnderlines = 34,    // 4: M1, under each bar's chords
    kChordMarks = 38,    // 4: M1, under the sounding chord: a phrase, half, a bar, half a bar long
    kChordTicks = 42,    // 4: M1, where chords change mid-bar
    kMeter = 46,         // 5: M1, how far the next phrase will change
    kGrooveMarks = 51,   // 16: M1 by beat, M2 by step
    kOffbeats = 67,      // 16: M1, the swung off-beats; M2, accents
    kScaleLine = 83,     // M4: the keyboard's span
    kToneLines = 84,     // 6: M3, the chord's notes
    kCells = 90,         // 128: a lane's beat, step or note, or a stretch of its range
    kNodes = 218,
};
const u32 kCellCount = kNodes - kCells;
// Text slots: the header's sixteen, then the pages' own. Each slot has its
// own colour in the artwork; a size is the slot's own times the scale.
enum Text {
    kLaneText = 16,      // 8 at 10 px, light: each lane's role and style
    kBrightText = 24,    // 10 px, white: the selected lane's label, or M1's next phrase
    kBigText = 25,       // 15 px, white: the phrase or the bar
    kChordText = 26,     // 12 px, white: the sounding chord
    kMidText = 27,       // 3 at 11 px, grey: other chords, labels
    kDimText = 30,       // 9 px, dark grey: a caption
    kPageText = 31,      // 10 px, white: the shown page
};
// The palette: solid greys, then outlines ("rings") on black.
enum Shade {
    kBlack, kBand, kRow, kGrid, kRule, kMuted, kDim, kSoft, kMid, kLight, kWhite,
    kRingRule, kRingDim, kRingLow, kRingMid, kRingWhite,
};
static const char* const kActions[4] = {"reroll", "freeze", "fill", "vary"};
// Advance widths of ' '..'~' in the screen font at 10 px, in tenths of a pixel.
static const u8 kGlyphWidths[95] = {
    28, 32, 36, 56, 56, 92, 69, 21, 29, 29, 44, 68, 28, 33, 28, 29, 56, 56, 56, 56, 56, 56, 56, 56,
    56, 56, 28, 28, 68, 68, 68, 48, 98, 69, 66, 66, 73, 60, 56, 72, 74, 28, 49, 62, 56, 94, 75, 74,
    60, 74, 66, 62, 55, 74, 65, 102, 64, 61, 56, 29, 29, 29, 68, 51, 29, 54, 58, 52, 58, 53, 32, 59,
    57, 24, 24, 51, 24, 89, 57, 57, 58, 58, 34, 48, 32, 57, 51, 82, 51, 51, 44, 38, 25, 38, 68};

// Geometry, Q10 pixels.
const i32 kLeft = 86 * unit, kSpan = 324 * unit, kBeat = 20736;  // 20.25 px: a beat of the phrase, a step of a bar
const i32 kWireX = 414 * unit, kTrackX = 446 * unit;
inline i32 px(i32 pixels) { return pixels * unit; }
inline i32 row_y(u32 row) { return px(82) + static_cast<i32>(row) * 14746; }  // 14.4 px apart

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
inline void right_text(Frame& frame, u32 slot, i32 right, i32 baseline, const char* s, i32 size, i32 scale = unit) {
    text(frame, slot, right - text_width(s, size) * scale / unit, baseline, scale, s);
}
inline void centred(Frame& frame, u32 slot, i32 x, i32 baseline, const char* s, i32 size, i32 scale = unit) {
    text(frame, slot, x - text_width(s, size) * scale / unit / 2, baseline, scale, s);
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
inline u32 isqrt(u32 v) {
    u32 r = 0;
    for (u32 b = 1u << 30; b; b >>= 2)
        if (r + b <= v) v -= r + b, r = (r >> 1) + b;
        else r >>= 1;
    return r;
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

// What every page shares.
struct View {
    const u32* w;
    u32 page, selected, step, bar;
    bool playing, frozen;
    u32 role(u32 lane) const { return field(w[kRolesWord], 3 * lane, 3) % kRoles; }
    u32 target(u32 lane) const { return field(w[kTargets], 3 * lane, 3); }
    bool muted(u32 lane) const { return field(w[kActivity], 8 + lane, 1) != 0; }
    bool picks() const { return page != kConduct; }  // M2..M4 show a selected lane
};

// A time's shade: what has played dark, what plays white, the bar playing
// light and what is to come grey. Stopped, everything waits in grey.
inline u32 time_shade(const View& v, u32 lane, bool past, bool now, bool this_bar) {
    if (v.muted(lane)) return kMuted;
    if (!v.playing) return kMid;
    return now ? kWhite : past ? kDim : this_bar ? kLight : kMid;
}

// "2 bass fifth": the lane, its role and its style.
inline void lane_labels(const View& v, Frame& frame) {
    for (u32 l = 0; l < kLanes; ++l) {
        char line[text_capacity];
        u32 n = 0;
        line[0] = 0;
        append_number(line, n, static_cast<i32>(l) + 1);
        append(line, n, " ");
        const u32 role = v.role(l);
        append(line, n, kRoleNames[role]);
        if (role > kDrum) {
            const Spec& s = kDetailSpecs[role - 1][0];
            if (s.format == kChoice) {
                char style[8];
                format(style, s, static_cast<i32>(lane_style(v.w, l)));
                append(line, n, " ");
                append(line, n, style);
            }
        }
        const i32 y = row_y(l) + px(35) / 10;
        if (v.picks() && l == v.selected) {
            text(frame, kBrightText, px(10), y, unit, line);
            show(frame, kAccent, px(3), row_y(l) - px(6), kWhite);
            // M3 draws the lane's own notes across the rows, so no band there.
            if (v.page != kDetail) show(frame, kRowBand, kLeft, row_y(l) - px(7), kRow);
        } else {
            text(frame, kLaneText + l, px(10), y, unit, line);
        }
    }
}

// Tracks down the right, each lane wired to its own. On M1 and M2 a track
// lights while a lane plays it; on M3 and M4 the selected lane's is lit.
inline void routing(const View& v, const DisplayState& state, Frame& frame) {
    u32 used = 0, muted_only = 0, lit = 0;
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        const u32 t = v.target(l);
        if (v.muted(l)) muted_only |= 1u << t;
        else used |= 1u << t;
        if (v.picks() ? l == v.selected : !v.muted(l) && state.glow[l] > 256) lit |= 1u << t;
    }
    for (u32 t = 0; t < kLanes; ++t) {
        const u32 shade = lit >> t & 1 ? kWhite : used >> t & 1 ? kRingMid : muted_only >> t & 1 ? kRingLow : kRingRule;
        show(frame, kTrackBoxes + t, kTrackX, row_y(t) - px(55) / 10, shade);
    }
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        // A 60 px line from the lane's row to its track's, turned and scaled.
        const i32 dx = 300, dy = (row_y(v.target(l)) - row_y(l)) * 10 / unit;  // tenths of a pixel
        const i32 length = static_cast<i32>(isqrt(static_cast<u32>(dx * dx + dy * dy)));
        const bool lit_wire = v.picks() ? l == v.selected : !v.muted(l) && state.glow[l] > 256;
        const u32 shade = v.muted(l) ? kMuted : lit_wire ? (v.picks() ? kWhite : kLight) : kDim;
        show(frame, kWires + l, kWireX, row_y(l), shade, length * unit / 600);
        const Turn turn = {dx * unit / length, dy * unit / length};
        opxy::draw::turn(frame, kWires + l, turn);
    }
}

inline void bar_lines(Frame& frame) {
    for (u32 b = 1; b < 4; ++b) show(frame, kGridLines + b - 1, kLeft + px(81) * static_cast<i32>(b), px(63), kGrid);
}

// One cell from the pool, or none once it is spent.
struct Cells {
    u32 next;
    void put(Frame& frame, i32 x, i32 y, u32 shade, i32 scale) {
        if (next < kCellCount) show(frame, kCells + next++, x, y, shade, scale);
    }
};

// M1: the phrase. The chords over its four bars, the groove every lane leans
// on, then each lane's beats: a beat's mark grows with its notes and shrinks
// to a dash where a note only holds.
inline void draw_conduct(const Input& input, const View& v, Frame& frame) {
    const u32* w = v.w;
    const i32 key = global_value(kKey, input.slots[kKey]), scale = global_value(kScale, input.slots[kScale]);
    const u32 chords = w[kChords], position = field(chords, 12, 2);
    const bool seventh = field(chords, 14, 1) != 0;
    const u32 at = v.bar * kSteps + v.step, now_beat = at / 4;

    show(frame, kBarBand, kLeft + px(81) * static_cast<i32>(v.bar), px(63), kBand);
    bar_lines(frame);

    // The chords: as many over the phrase as `hrate` makes, the sounding one
    // underlined white and named, with the next few.
    const u32 rate = kHarmonySteps[global_value(kHarmony, input.slots[kHarmony])];
    const u32 count = 64 / rate, cur = at / rate;
    for (u32 b = 0; b < 4; ++b) show(frame, kUnderlines + b, kLeft + px(81) * static_cast<i32>(b) + px(2), px(59), kRule);
    const u32 mark = count == 1 ? 0 : count == 2 ? 1 : count == 4 ? 2 : 3;
    show(frame, kChordMarks + mark, kLeft + kSpan / static_cast<i32>(count) * static_cast<i32>(cur) + px(2), px(59), kWhite);
    if (count == 8)
        for (u32 b = 0; b < 4; ++b) show(frame, kChordTicks + b, kLeft + px(81) * static_cast<i32>(b) + px(405) / 10, px(57), kDim);
    u32 labelled = 0;
    for (u32 i = cur; i < count && labelled < 4; ++i) {
        const i32 degree = static_cast<i32>(field(chords, 3 * ((position + i - cur) % 4), 3));
        char name[16];
        chord_name(name, key, scale, degree, seventh);
        const i32 x = kLeft + kSpan / static_cast<i32>(count) * static_cast<i32>(i) + px(4);
        if (i == cur) {
            u32 n = 0;
            while (name[n]) ++n;
            name[n++] = ' ';
            numeral(name + n, key, scale, degree);
            text(frame, kChordText, x, px(55), unit, name);
        } else {
            text(frame, kMidText + labelled - 1, x, px(55), unit, name);
        }
        ++labelled;
    }
    for (u32 i = 0; i < cur && labelled < 4; ++i) {
        const i32 degree = static_cast<i32>(field(chords, 3 * ((position + 4 - (cur - i) % 4) % 4), 3));
        char name[8];
        chord_name(name, key, scale, degree, seventh);
        text(frame, kMidText + labelled++ - 1, kLeft + kSpan / static_cast<i32>(count) * static_cast<i32>(i) + px(4), px(55),
             unit, name);
    }

    // The phrase, and what comes next: frozen, it holds; else the meter shows
    // how much the next will change.
    const u32 phrase = field(w[kState], 19, 5);
    char letter[2] = {static_cast<char>('A' + phrase % 26), 0};
    text(frame, kBigText, px(8), px(56), unit, letter);
    char line[text_capacity];
    u32 n = 0;
    line[0] = 0;
    if (!v.playing) {
        append(line, n, "from bar 1");
    } else {
        append(line, n, v.frozen ? "hold " : "next ");
        line[n++] = static_cast<char>('A' + (phrase + (v.frozen ? 0 : 1)) % 26);
        line[n] = 0;
    }
    right_text(frame, kBrightText, px(472), px(55), line, 10);
    const i32 evolve = global_value(kEvolve, input.slots[kEvolve]);
    for (u32 i = 0; i < 5; ++i) {
        const bool on = v.frozen || static_cast<i32>(i) < (evolve + 10) / 20;
        show(frame, kMeter + i, px(418) + px(11) * static_cast<i32>(i), px(585) / 10,
             v.frozen ? kWhite : on ? kLight : kRule);
    }

    // The groove: kicks solid, backbeats hollow, off-beats moved by swing.
    text(frame, kDimText, px(10), px(72), unit, "groove");
    const i32 swing = global_value(kSwing, input.slots[kSwing]);
    for (u32 b = 0; b < 16; ++b) {
        const u32 beat = field(w[kGroove], 2 * b, 2);
        const bool past = v.playing && b < now_beat, now = v.playing && b == now_beat;
        const i32 x = kLeft + kBeat * static_cast<i32>(b);
        if (beat == kBeatKick) show(frame, kGrooveMarks + b, x + px(4), px(69), now ? kWhite : past ? kDim : kMid);
        else if (beat == kBeatBack) show(frame, kGrooveMarks + b, x + px(4), px(69), now ? kRingWhite : past ? kRingDim : kRingMid);
        show(frame, kOffbeats + b, x + kBeat / 2 + px(swing * 9) / 100, px(69), past ? kDim : kSoft);
    }

    // Each lane's beats.
    static const i32 kSizes[4] = {0, 512, 1024, 1434};
    Cells cells = {0};
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        for (u32 b = 0; b < 16; ++b) {
            const u32 c = phrase_cell(w, l, b);
            if (!c) continue;
            const u32 shade = time_shade(v, l, b < now_beat, b == now_beat, b / 4 == v.bar);
            cells.put(frame, kLeft + kBeat * static_cast<i32>(b) + kBeat / 2, row_y(l), shade, kSizes[c]);
        }
    }

    const i32 x = kLeft + static_cast<i32>(at) * (kBeat / 4);
    show(frame, kPlayhead, x, px(63), v.playing ? kMid : kSoft);
    show(frame, kPlayMark, x, px(62), v.playing ? kWhite : kMid);
}

// M2: the bar playing, step by step, every lane: a drum's steps at the height
// of their lowest voice, other lanes' notes sized by how many start.
inline void draw_lanes(const View& v, Frame& frame) {
    const u32* w = v.w;
    char number_text[4];
    number(number_text, static_cast<i32>(v.bar) + 1, false);
    text(frame, kBigText, px(8), px(56), unit, number_text);
    text(frame, kMidText, px(20), px(55), unit * 9 / 11, "bar");
    text(frame, kDimText, px(10), px(72), unit, "groove");
    show(frame, kStepBand, kLeft + kBeat * static_cast<i32>(v.step), px(63), kRow);
    bar_lines(frame);
    for (u32 s = 0; s < kSteps; ++s) {
        const u32 beat = field(w[kStepGroove], 2 * s, 2);
        const bool now = v.playing && s == v.step;
        const i32 x = kLeft + kBeat * static_cast<i32>(s) + kBeat / 2;
        if (beat == kBeatKick) show(frame, kGrooveMarks + s, x, px(69), now ? kWhite : kMid);
        else if (beat == kBeatBack) show(frame, kGrooveMarks + s, x, px(69), now ? kRingWhite : kRingMid);
        if (beat == kBeatAccent || beat == kBeatKick)
            show(frame, kOffbeats + s, x, px(635) / 10, now ? kWhite : kLight);
    }
    static const i32 kSizes[4] = {0, 614, 922, 1434};
    static const i32 kDrumY[4] = {0, -3584, 0, 3584};  // high, middle, low: +-3.5 px
    Cells cells = {0};
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        const bool drum = v.role(l) == kDrum;
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 c = bits_at(w, kStepCells, 2 * (l * kSteps + s), 2);
            if (!c) continue;
            const u32 shade = time_shade(v, l, s < v.step, s == v.step, true);
            const i32 x = kLeft + kBeat * static_cast<i32>(s) + kBeat / 2;
            if (drum) cells.put(frame, x, row_y(l) + kDrumY[c], shade, 512);
            else cells.put(frame, x, row_y(l), shade, kSizes[c]);
        }
    }
}

// M3: the selected lane's bar, a row a semitone (or a drum voice), over lines
// at the sounding chord's notes.
inline void draw_roll(const Input& input, const View& v, Frame& frame) {
    const u32* w = v.w;
    const u32 lane = v.selected;
    char line[text_capacity];
    u32 n = 0;
    number(line, static_cast<i32>(v.bar) + 1, false);
    text(frame, kBigText, px(8), px(56), unit, line);
    if (v.role(lane) == kOff) {
        line[0] = 0;
        append(line, n, "lane ");
        append_number(line, n, static_cast<i32>(lane) + 1);
        append(line, n, " is off: give it a role on M2");
        text(frame, kMidText, kLeft + px(4), px(55), unit, line);
        return;
    }
    const i32 key = global_value(kKey, input.slots[kKey]), scale = global_value(kScale, input.slots[kScale]);
    const u32 chords = w[kRollChords];
    const i32 degree = static_cast<i32>(field(chords, 3 * field(chords, 12, 2), 3));
    const bool seventh = field(chords, 14, 1) != 0;
    char name[16];
    chord_name(name, key, scale, degree, seventh);
    u32 m = 0;
    while (name[m]) ++m;
    name[m++] = ' ';
    numeral(name + m, key, scale, degree);
    text(frame, kChordText, kLeft + px(4), px(55), unit, name);
    line[0] = 0;
    append(line, n, "next ");
    chord_name(name, key, scale, static_cast<i32>(field(chords, 15, 3)), field(chords, 18, 1) != 0);
    append(line, n, name);
    text(frame, kMidText, kLeft + px(80), px(55), unit * 9 / 11, line);

    const bool drum = field(w[kRange], 11, 1) != 0;
    const i32 low = static_cast<i32>(field(w[kRange], 0, 7)), band = static_cast<i32>(field(w[kRange], 7, 4));
    const u32 rows = drum ? kDrumRows : kRollRows;
    const i32 pitch = drum ? px(155) / 10 : px(103) / 10, bottom = px(186);
    char range[16];
    n = 0;
    range[0] = 0;
    if (drum) {
        append(range, n, "kick to low tom");
    } else {
        note_name(name, low);
        append(range, n, name);
        append(range, n, " - ");
        note_name(name, low + band * static_cast<i32>(rows) - 1);
        append(range, n, name);
        // The chord's notes: a line on each row a chord tone falls on.
        u32 tones = 0;
        for (u32 t = 0; t < (seventh ? 4u : 3u); ++t)
            tones |= 1u << static_cast<u32>(((degree_semitones(key, scale, degree + 2 * static_cast<i32>(t)) % 12) + 12) % 12);
        u32 lines = 0;
        for (u32 r = 0; r < rows && lines < 6 && band == 1; ++r)
            if (tones >> static_cast<u32>((low + static_cast<i32>(r)) % 12) & 1)
                show(frame, kToneLines + lines++, kLeft, bottom - pitch * static_cast<i32>(r), kRule);
    }
    right_text(frame, kMidText + 1, px(410), px(55), range, 11, unit * 9 / 11);
    show(frame, kStepBand, kLeft + kBeat * static_cast<i32>(v.step), px(63), kRow);
    bar_lines(frame);
    Cells cells = {0};
    for (u32 r = 0; r < rows; ++r)
        for (u32 s = 0; s < kSteps; ++s) {
            if (!bit(w, kRoll, r * kSteps + s)) continue;
            const u32 shade = time_shade(v, lane, s < v.step, s == v.step, true);
            cells.put(frame, kLeft + kBeat * static_cast<i32>(s) + kBeat / 2, bottom - pitch * static_cast<i32>(r), shade, 1434);
        }
}

// M4: each lane's range on a keyboard from C1 to C7, in dashes, moved by its
// transpose (a faint copy marks where it was), and wired to its track.
inline void draw_route(const View& v, Frame& frame) {
    const u32* w = v.w;
    const i32 lowest = 24, highest = 96, x0 = px(92), width = px(312);
    struct Key {
        static i32 x(i32 note, i32 x0, i32 width) { return x0 + width / 72 * (note - 24); }
    };
    text(frame, kDimText, px(8), px(56), unit, "range");
    show(frame, kScaleLine, x0, px(58), kMid);
    static const char* const kOctaves[3] = {"C2", "C4", "C6"};
    for (u32 i = 0; i < 3; ++i) {
        const i32 x = Key::x(36 + 24 * static_cast<i32>(i), x0, width);
        show(frame, kGridLines + i, x, px(63), kGrid);
        centred(frame, kMidText + i, x, px(51), kOctaves[i], 11, unit * 9 / 11);
    }
    Cells cells = {0};
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        const u32 r = lane_range(w, l);
        const i32 low = static_cast<i32>(field(r, 0, 7)), high = static_cast<i32>(field(r, 7, 7));
        const i32 xpose = static_cast<i32>(field(r, 14, 6)) - 24;
        const bool sel = l == v.selected, muted = v.muted(l);
        for (u32 pass = xpose ? 0u : 1u; pass < 2; ++pass) {
            const i32 lo = clampi(low + (pass ? xpose : 0), lowest, highest), hi = clampi(high + (pass ? xpose : 0), lowest, highest);
            if (hi <= lo) continue;
            const i32 a = Key::x(lo, x0, width), b = Key::x(hi, x0, width), dash = px(18);
            const u32 shade = !pass ? kRingRule : sel ? kWhite : muted ? kMuted : kDim;
            // Dashes 18 px long, evenly spread so the last ends at the top.
            const i32 count = b - a <= dash ? 1 : (b - a - dash + px(20) - 1) / px(20) + 1;
            for (i32 k = 0; k < count; ++k) {
                const i32 x = count == 1 ? (a + b) / 2 : a + dash / 2 + (b - a - dash) / (count - 1) * k;
                cells.put(frame, x, row_y(l), shade, 1536);
            }
            if (pass && sel) {
                // The range's own notes, though the keyboard stops at C1 and C7.
                char note[8];
                note_name(note, clampi(low + xpose, 0, 127));
                right_text(frame, kBigText, a - px(4), row_y(l) + px(35) / 10, note, 15, unit * 3 / 5);
                note_name(note, clampi(high + xpose, 0, 127));
                text(frame, kChordText, b + px(4), row_y(l) + px(35) / 10, unit * 3 / 4, note);
            }
        }
    }
}

// The page labels are artwork; the shown page's is drawn over its own, white.
inline void draw_pages(u32 page, Frame& frame) {
    show(frame, kPageMark, px(4) + px(120) * static_cast<i32>(page), px(2025) / 10, kWhite);
    text(frame, kPageText, px(6) + px(120) * static_cast<i32>(page), px(216), unit, kPageNames[page]);
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    for (u32 l = 0; l < kLanes; ++l) state.glow[l] = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    u32 w[kWords];
    for (u32 i = 0; i < kWords; ++i) w[i] = static_cast<u32>(opxy::control::from_control(i));
    View v;
    v.w = w;
    v.page = field(w[kState], 0, 2);
    v.selected = field(w[kState], 2, 3);
    v.playing = field(w[kState], 6, 1) != 0;
    v.step = field(w[kState], 7, 4);
    v.bar = field(w[kState], 11, 2);
    v.frozen = field(w[kState], 13, 1) != 0;

    const char* titles[8];
    char values[8][header_capacity];
    header_captions(input, w, titles, values);
    page_header(state.header, input, frame, titles, values, kBar);

    // A lane that just played lights its track at once, which then fades over 0.2 s.
    const i32 fade = clamp(input.elapsed, 0, 1024) * 5;
    for (u32 l = 0; l < kLanes; ++l) {
        const i32 glow = field(w[kActivity], l, 1) ? 1024 : static_cast<i32>(state.glow[l]) - fade;
        state.glow[l] = static_cast<u16>(input.reduced_motion && !field(w[kActivity], l, 1) ? 0 : clamp(glow, 0, 1024));
    }

    for (u32 i = kPageMark; i < kNodes; ++i) hide(frame, i);
    for (u32 i = kLaneText; i < Frame::text_count; ++i) park_text(frame, i);
    lane_labels(v, frame);
    routing(v, state, frame);
    switch (v.page) {
    case kLane: draw_lanes(v, frame); break;
    case kDetail: draw_roll(input, v, frame); break;
    case kRoute: draw_route(v, frame); break;
    default: draw_conduct(input, v, frame);
    }
    draw_pages(v.page, frame);
}
}
