#include <opxy/track_control.hpp>
#include "display.h"
#include "words.h"

namespace conductor {
using namespace opxy::draw;

// One layout on every page, left to right: the lanes' labels, their bars
// (x 86..386), wires, then where they play: tracks (x 426) and MIDI channels
// (x 452), which stay dim until the SDK can send to them. Eight rows, one a
// lane, at the same height on every page; M3 gives them to the selected
// lane's own voices or notes.
//
// Node slots, in tools/artwork.py's shapes.
enum Node {
    kBar = 0,            // 8: the two-row header's stepped bar
    kPageMark = 8,       // under the shown page's label
    kWires = 9,          // 8: each lane to its track, a line turned and scaled, under the boxes
    kTrackBoxes = 17,    // 8: tracks 1..8, down the right
    kMidiBoxes = 25,     // 8: MIDI channels 1..8, beside them
    kAccent = 33,        // M2, M4: beside the selected lane
    kRowBand = 34,       // M2, M4: behind the selected lane
    kBarBand = 35,       // M1: behind the bar playing
    kStepBand = 36,      // M2, M3: behind the step playing
    kGridLines = 37,     // 3: between bars or beats; M4's octave guides
    kPlayhead = 40,      // M1
    kPlayMark = 41,      // M1: the playhead's head
    kUnderlines = 42,    // 4: M1, under each bar's chords
    kChordMarks = 46,    // 4: M1, under the sounding chord: a phrase, half, a bar, half a bar long
    kChordTicks = 50,    // 4: M1, where chords change mid-bar
    kMeter = 54,         // 5: M1, how far the next phrase will change
    kGrooveMarks = 59,   // 16: M1 by beat, M2 by step
    kOffbeats = 75,      // 16: M1, the swung off-beats; M2, accents
    kScaleLine = 91,     // M4: the keyboard's span
    kToneLines = 92,     // 6: M3, the chord's notes
    kCells = 98,         // 128: a lane's beat, step or note, or a stretch of its range
    kNodes = 226,
};
const u32 kCellCount = kNodes - kCells;
// Text slots: the header's sixteen, then the pages' own. Each slot has its
// own colour in the artwork; a size is the slot's own times the scale.
enum Text {
    kLaneText = 16,      // 8 at 10 px, light: each lane's role and style; M3, the lane's voices or notes
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
const i32 kLeft = 86 * unit, kSpan = 300 * unit, kBeat = 19200;  // 18.75 px: a beat of the phrase, a step of a bar
const i32 kBarWidth = kSpan / 4;
const i32 kWireX = 390 * unit, kTrackX = 426 * unit, kMidiX = 452 * unit;
inline i32 px(i32 pixels) { return pixels * unit; }
inline i32 row_y(u32 row) { return px(82) + static_cast<i32>(row) * 14746; }  // 14.4 px apart
// M3's rows: twelve notes a semitone apart, low at the bottom, or the eight
// drum voices, kick at the top. The wire to the track leaves from the middle.
const i32 kRollBottom = 186 * unit, kNotePitch = 103 * unit / 10, kDrumTop = 775 * unit / 10, kDrumPitch = 155 * unit / 10;
const i32 kRollMiddle = (kDrumTop + kRollBottom) / 2;
static const char* const kVoiceNames[kDrumRows] = {"kick", "snare", "clap", "rim", "hat", "open hat", "tom", "low tom"};

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

// The header row being edited: the second once an encoder press has moved
// any position to its alternate, as page_header works it out.
inline u32 header_row(const Input& input) {
    for (u32 col = 0; col < 4; ++col)
        if (input.shown[col] == page_header_slots[1][col]) return 1;
    return 0;
}

// The header's titles and values by slot: the page's controls, or on Shift
// the actions encoders 1..4 take, on the row being edited.
inline void header_captions(const Input& input, const u32* w, const char* (&titles)[8],
                            char (&values)[8][header_capacity]) {
    const u32 page = field(w[kState], 0, 2), lane = field(w[kState], 2, 3);
    const bool shift = field(w[kState], 5, 1) != 0, all = page == kConduct;
    const u32 role = field(w[kRolesWord], 3 * lane, 3), row = header_row(input);
    for (u32 slot = 0; slot < 8; ++slot) {
        char* value = values[slot];
        value[0] = 0;
        if (shift && slot / 4 == row) {
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
inline void lane_name(const View& v, u32 l, char (&line)[text_capacity]) {
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
}

// Every lane down the left. M3 gives the rows to the selected lane instead.
inline void lane_labels(const View& v, Frame& frame) {
    if (v.page == kDetail) return;
    for (u32 l = 0; l < kLanes; ++l) {
        char line[text_capacity];
        lane_name(v, l, line);
        const i32 y = row_y(l) + px(35) / 10;
        if (v.picks() && l == v.selected) {
            text(frame, kBrightText, px(10), y, unit, line);
            show(frame, kAccent, px(3), row_y(l) - px(6), kWhite);
            show(frame, kRowBand, kLeft, row_y(l) - px(7), kRow);
        } else {
            text(frame, kLaneText + l, px(10), y, unit, line);
        }
    }
}

// Tracks down the right, each lane wired to its own. On M1 and M2 a track
// lights while a lane plays it; on M3 and M4 the selected lane's is lit. M3
// wires only the selected lane, from the middle of its notes. MIDI channels
// sit beside the tracks, outlined in the dimmest grey: nothing can play them
// until the SDK sends notes over MIDI.
inline void routing(const View& v, const DisplayState& state, Frame& frame) {
    for (u32 c = 0; c < kLanes; ++c) show(frame, kMidiBoxes + c, kMidiX, row_y(c) - px(55) / 10, kRingRule);
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
        if (v.role(l) == kOff || (v.page == kDetail && l != v.selected)) continue;
        // A 60 px line from the lane's row to its track's, turned and scaled.
        const i32 from = v.page == kDetail ? kRollMiddle : row_y(l);
        const i32 dx = (kTrackX - kWireX) * 10 / unit, dy = (row_y(v.target(l)) - from) * 10 / unit;  // tenths of a pixel
        const i32 length = static_cast<i32>(isqrt(static_cast<u32>(dx * dx + dy * dy)));
        const bool lit_wire = v.picks() ? l == v.selected : !v.muted(l) && state.glow[l] > 256;
        const u32 shade = v.muted(l) ? kMuted : lit_wire ? (v.picks() ? kWhite : kLight) : kDim;
        show(frame, kWires + l, kWireX, from, shade, length * unit / 600);
        const Turn turn = {dx * unit / length, dy * unit / length};
        opxy::draw::turn(frame, kWires + l, turn);
    }
}

inline void bar_lines(Frame& frame) {
    for (u32 b = 1; b < 4; ++b) show(frame, kGridLines + b - 1, kLeft + kBarWidth * static_cast<i32>(b), px(63), kGrid);
}

// One cell from the pool, or none once it is spent.
// M1 and M2 give each lane's beat or step its own cell (8 x 16, the whole
// pool), so a bar or beat changes only the cells whose marks change. M3 and
// M4 take cells in order up to `end`; M4 keeps the last ones for the selected
// lane, so turning its range moves only its own.
struct Cells {
    u32 next, end;
    void put(Frame& frame, i32 x, i32 y, u32 shade, i32 scale) {
        if (next < end) show(frame, kCells + next++, x, y, shade, scale);
    }
    static void at(Frame& frame, u32 lane, u32 slot, i32 x, i32 y, u32 shade, i32 scale) {
        show(frame, kCells + lane * kSteps + slot, x, y, shade, scale);
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

    show(frame, kBarBand, kLeft + kBarWidth * static_cast<i32>(v.bar), px(63), kBand);
    bar_lines(frame);

    // The chords: as many over the phrase as `hrate` makes, the sounding one
    // underlined white and named, with the next few.
    const u32 rate = kHarmonySteps[global_value(kHarmony, input.slots[kHarmony])];
    const u32 count = 64 / rate, cur = at / rate;
    for (u32 b = 0; b < 4; ++b) show(frame, kUnderlines + b, kLeft + kBarWidth * static_cast<i32>(b) + px(2), px(59), kRule);
    const u32 mark = count == 1 ? 0 : count == 2 ? 1 : count == 4 ? 2 : 3;
    show(frame, kChordMarks + mark, kLeft + kSpan / static_cast<i32>(count) * static_cast<i32>(cur) + px(2), px(59), kWhite);
    if (count == 8)
        for (u32 b = 0; b < 4; ++b) show(frame, kChordTicks + b, kLeft + kBarWidth * static_cast<i32>(b) + kBarWidth / 2, px(57), kDim);
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
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        for (u32 b = 0; b < 16; ++b) {
            const u32 c = phrase_cell(w, l, b);
            if (!c) continue;
            const u32 shade = time_shade(v, l, b < now_beat, b == now_beat, b / 4 == v.bar);
            Cells::at(frame, l, b, kLeft + kBeat * static_cast<i32>(b) + kBeat / 2, row_y(l), shade, kSizes[c]);
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
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        const bool drum = v.role(l) == kDrum;
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 c = bits_at(w, kStepCells, 2 * (l * kSteps + s), 2);
            if (!c) continue;
            const u32 shade = time_shade(v, l, s < v.step, s == v.step, true);
            const i32 x = kLeft + kBeat * static_cast<i32>(s) + kBeat / 2;
            if (drum) Cells::at(frame, l, s, x, row_y(l) + kDrumY[c], shade, 512);
            else Cells::at(frame, l, s, x, row_y(l), shade, kSizes[c]);
        }
    }
}

// M3: the selected lane's bar. Its own rows run down the left: a drum lane's
// voices, kick at the top, or a synth lane's notes, a semitone a row (the
// naturals named), over lines at the sounding chord's notes. The lane, the
// chord and the bar sit above; one wire takes it all to the lane's track.
inline void draw_roll(const Input& input, const View& v, Frame& frame) {
    const u32* w = v.w;
    const u32 lane = v.selected;
    char line[text_capacity];
    lane_name(v, lane, line);
    text(frame, kBrightText, px(10), px(55), unit, line);
    show(frame, kAccent, px(3), px(49), kWhite);
    u32 n = 0;
    line[0] = 0;
    append(line, n, "bar ");
    append_number(line, n, static_cast<i32>(v.bar) + 1);
    right_text(frame, kMidText + 1, kLeft + kSpan, px(55), line, 11, unit * 9 / 11);
    if (v.role(lane) == kOff) {
        text(frame, kMidText, kLeft + px(4), px(55), unit * 9 / 11, "off: give it a role on M2");
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
    n = 0;
    line[0] = 0;
    append(line, n, "next ");
    chord_name(name, key, scale, static_cast<i32>(field(chords, 15, 3)), field(chords, 18, 1) != 0);
    append(line, n, name);
    text(frame, kMidText, kLeft + px(80), px(55), unit * 9 / 11, line);

    const bool drum = field(w[kRange], 11, 1) != 0;
    const i32 low = static_cast<i32>(field(w[kRange], 0, 7)), band = static_cast<i32>(field(w[kRange], 7, 4));
    const u32 rows = drum ? kDrumRows : kRollRows;
    struct Row {
        bool drum;
        i32 y(u32 r) const { return drum ? kDrumTop + kDrumPitch * static_cast<i32>(r) : kRollBottom - kNotePitch * static_cast<i32>(r); }
    } at = {drum};
    if (drum) {
        for (u32 r = 0; r < rows; ++r) text(frame, kLaneText + r, px(10), at.y(r) + px(35) / 10, unit, kVoiceNames[r]);
    } else {
        // Each natural note named; on a lane wider than an octave, every
        // other row by its lowest note.
        u32 labels = 0;
        for (u32 r = 0; r < rows && labels < kLanes; ++r) {
            const i32 note = low + band * static_cast<i32>(r);
            const bool natural = (0xAB5u >> static_cast<u32>(((note % 12) + 12) % 12) & 1) != 0;
            if (band == 1 ? !natural : r % 2 != 0) continue;
            note_name(name, note, note_names(key, scale));
            text(frame, kLaneText + labels++, px(10), at.y(r) + px(35) / 10, unit * 9 / 10, name);
        }
        // The chord's notes: a line on each row a chord tone falls on.
        u32 tones = 0;
        for (u32 t = 0; t < (seventh ? 4u : 3u); ++t)
            tones |= 1u << static_cast<u32>(((degree_semitones(key, scale, degree + 2 * static_cast<i32>(t)) % 12) + 12) % 12);
        u32 lines = 0;
        for (u32 r = 0; r < rows && lines < 6 && band == 1; ++r)
            if (tones >> static_cast<u32>((low + static_cast<i32>(r)) % 12) & 1)
                show(frame, kToneLines + lines++, kLeft, at.y(r), kRule);
    }
    show(frame, kStepBand, kLeft + kBeat * static_cast<i32>(v.step), px(63), kRow);
    bar_lines(frame);
    Cells cells = {0, kCellCount};
    for (u32 r = 0; r < rows; ++r)
        for (u32 s = 0; s < kSteps; ++s) {
            if (!bit(w, kRoll, r * kSteps + s)) continue;
            const u32 shade = time_shade(v, lane, s < v.step, s == v.step, true);
            cells.put(frame, kLeft + kBeat * static_cast<i32>(s) + kBeat / 2, at.y(r), shade, 1434);
        }
    if (!cells.next) centred(frame, kMidText + 2, kLeft + kSpan / 2, kRollMiddle + px(4), "rests this bar", 11);
}

// M4: each lane's range on a keyboard from C1 to C7, in dashes, moved by its
// transpose (faint marks show where it was), and wired to its track. The
// MIDI column says it can't play yet.
inline void draw_route(const View& v, Frame& frame) {
    const u32* w = v.w;
    const i32 lowest = 24, highest = 96, x0 = px(92), width = px(288);
    struct Key {
        static i32 x(i32 note, i32 x0, i32 width) { return x0 + width / 72 * (note - 24); }
    };
    right_text(frame, kDimText, px(474), px(197), "midi: not yet", 9);
    show(frame, kScaleLine, x0, px(58), kMid);
    static const char* const kOctaves[3] = {"C2", "C4", "C6"};
    for (u32 i = 0; i < 3; ++i) {
        const i32 x = Key::x(36 + 24 * static_cast<i32>(i), x0, width);
        show(frame, kGridLines + i, x, px(63), kGrid);
        centred(frame, kMidText + i, x, px(51), kOctaves[i], 11, unit * 9 / 11);
    }
    // A range takes at most 15 dashes and two marks.
    const u32 kept = 17;
    Cells others = {0, kCellCount - kept}, own = {kCellCount - kept, kCellCount};
    for (u32 l = 0; l < kLanes; ++l) {
        if (v.role(l) == kOff) continue;
        Cells& cells = l == v.selected ? own : others;
        const u32 r = lane_range(w, l);
        const i32 low = static_cast<i32>(field(r, 0, 7)), high = static_cast<i32>(field(r, 7, 7));
        const i32 xpose = static_cast<i32>(field(r, 14, 6)) - 24;
        const bool sel = l == v.selected, muted = v.muted(l);
        if (xpose) {
            // Where the range was before its transpose: a faint upright mark at each end.
            const i32 marks[2] = {clampi(low, lowest, highest), clampi(high, lowest, highest)};
            for (u32 m = 0; m < 2; ++m) {
                const u32 node = kCells + cells.next;
                cells.put(frame, Key::x(marks[m], x0, width), row_y(l), kDim, unit);
                opxy::draw::turn(frame, node, Turn{0, unit});
            }
        }
        {
            const i32 lo = clampi(low + xpose, lowest, highest), hi = clampi(high + xpose, lowest, highest);
            if (hi <= lo) continue;
            const i32 a = Key::x(lo, x0, width), b = Key::x(hi, x0, width), dash = px(18);
            const u32 shade = sel ? kWhite : muted ? kMuted : kDim;
            // Dashes 18 px long, evenly spread so the last ends at the top.
            const i32 count = b - a <= dash ? 1 : (b - a - dash + px(20) - 1) / px(20) + 1;
            for (i32 k = 0; k < count; ++k) {
                const i32 x = count == 1 ? (a + b) / 2 : a + dash / 2 + (b - a - dash) / (count - 1) * k;
                cells.put(frame, x, row_y(l), shade, 1536);
            }
            if (sel) {
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
