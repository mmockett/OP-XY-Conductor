#pragma once
// What the control and the display both know: the musical vocabulary and the
// controls of Conductor's own pages (M2..M4), which the control edits and the
// display names.
#include <opxy/types.hpp>

namespace conductor {
using opxy::i32;
using opxy::u32;
using opxy::u16;
using opxy::u8;
typedef __INT8_TYPE__ i8;
typedef __INT16_TYPE__ i16;

const u32 kLanes = 8, kSteps = 16, kTicks = 24, kBarTicks = kSteps * kTicks, kPhrase = 4;

enum Role { kOff, kDrum, kBass, kChord, kPad, kArp, kLead, kRoles };
static const char* const kRoleNames[kRoles] = {"off", "drum", "bass", "chord", "pad", "arp", "lead"};

// Conductor's pages: M1 (the eight saved controls), then its own.
enum Page { kConduct, kLane, kDetail, kRoute, kPages };
static const char* const kPageNames[kPages] = {"conductor", "lanes", "lane", "route"};

static const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const u8 kScales[7][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}};
enum Progression { kPop, kMinor, kModal, kDrone, kJazz, kWander };
static const u8 kHarmonySteps[4] = {8, 16, 32, 64};

// The saved controls on M1, by parameter slot.
enum Global { kKey, kScale, kProgression, kDensity, kComplexity, kHarmony, kSwing, kEvolve, kGlobals };
static const u8 kGlobalChoices[kGlobals] = {12, 7, 6, 0, 0, 4, 0, 0};
static const char* const kGlobalTitles[kGlobals] = {"key", "scale", "prog", "dens", "cmplx", "hrate", "swing", "evolve"};
static const char* const kScaleNames[7] = {"maj", "min", "dor", "phr", "lyd", "mix", "hmin"};
static const char* const kProgressionNames[6] = {"pop", "minor", "mode", "drone", "jazz", "walk"};
static const char* const kHarmonyNames[4] = {"1/2", "1", "2", "4"};

// How a control's value reads.
enum Format { kBlank, kNumber, kSigned, kCentred, kChoice, kNote, kLaneNumber, kTarget };
struct Spec {
    const char* title;
    i16 minimum, maximum, initial;
    u8 format;
    const char* const* choices;
};

static const char* const kLocks[] = {"kick", "acnt", "free"};
static const char* const kRoleChoices[] = {"off", "drum", "bass", "chord", "pad", "arp", "lead"};
static const char* const kOffOn[] = {"off", "on"};

// M2: the selected lane, slot by slot (slot 0 selects the lane).
enum LaneSlot { kRoleSlot = 1, kDens, kOct, kVel, kVary, kLock, kGate };
static const Spec kLaneSpecs[8] = {
    {"lane", 0, 7, 0, kLaneNumber, nullptr},   {"role", 0, 6, 0, kChoice, kRoleChoices},
    {"dens", 0, 100, 50, kCentred, nullptr},   {"oct", -2, 2, 0, kSigned, nullptr},
    {"vel", 0, 100, 80, kNumber, nullptr},     {"vary", 0, 100, 50, kNumber, nullptr},
    {"lock", 0, 2, 1, kChoice, kLocks},        {"gate", 5, 100, 60, kNumber, nullptr}};

// M4: where the selected lane plays (slot 0 selects the lane).
enum RouteSlot { kTargetSlot = 1, kXpose, kMute, kLow, kHigh };
static const Spec kRouteSpecs[8] = {
    {"lane", 0, 7, 0, kLaneNumber, nullptr}, {"to", 0, 7, 0, kTarget, nullptr},
    {"xpose", -24, 24, 0, kSigned, nullptr}, {"mute", 0, 1, 0, kChoice, kOffOn},
    {"low", 0, 127, 0, kNote, nullptr},      {"high", 0, 127, 127, kNote, nullptr},
    {"", 0, 0, 0, kBlank, nullptr},          {"", 0, 0, 0, kBlank, nullptr}};

// M3: the selected lane's role controls.
static const char* const kDrumMaps[] = {"xy", "gm"};
static const char* const kBassStyles[] = {"root", "fifth", "oct", "walk", "acid"};
static const char* const kMotifs[] = {"1", "2", "4"};
static const char* const kRanges2[] = {"1", "2"};
static const char* const kVoices[] = {"tri", "7th", "9th", "sus2", "sus4", "pwr"};
static const char* const kRhythms[] = {"hold", "stab", "off", "comp", "pulse"};
static const char* const kRetrig[] = {"chord", "bar"};
static const char* const kArpModes[] = {"up", "down", "updn", "rand", "conv", "order"};
static const char* const kArpRates[] = {"1/4", "1/8", "1/16"};
static const char* const kRanges3[] = {"1", "2", "3"};
static const char* const kTones[] = {"chord", "scale"};
static const char* const kContours[] = {"arch", "rise", "fall", "wave", "free"};

static const Spec kDetailSpecs[kRoles - 1][8] = {
    {{"kick", 0, 100, 60, kNumber, nullptr}, {"snare", 0, 100, 60, kNumber, nullptr},
     {"hats", 0, 100, 55, kNumber, nullptr}, {"perc", 0, 100, 30, kNumber, nullptr},
     {"fills", 0, 100, 50, kNumber, nullptr}, {"ghost", 0, 100, 25, kNumber, nullptr},
     {"human", 0, 100, 20, kNumber, nullptr}, {"map", 0, 1, 0, kChoice, kDrumMaps}},
    {{"style", 0, 4, 1, kChoice, kBassStyles}, {"rate", 0, 100, 40, kNumber, nullptr},
     {"appr", 0, 100, 40, kNumber, nullptr}, {"accent", 0, 100, 50, kNumber, nullptr},
     {"slide", 0, 100, 20, kNumber, nullptr}, {"rest", 0, 100, 15, kNumber, nullptr},
     {"range", 0, 1, 0, kChoice, kRanges2}, {"motif", 0, 2, 1, kChoice, kMotifs}},
    {{"voice", 0, 5, 0, kChoice, kVoices}, {"inv", 0, 3, 0, kNumber, nullptr},
     {"rhythm", 0, 4, 3, kChoice, kRhythms}, {"strum", 0, 100, 15, kNumber, nullptr},
     {"spread", 0, 100, 20, kNumber, nullptr}, {"move", 0, 100, 70, kNumber, nullptr},
     {"accent", 0, 100, 40, kNumber, nullptr}, {"push", 0, 100, 25, kNumber, nullptr}},
    {{"voice", 0, 5, 1, kChoice, kVoices}, {"spread", 0, 100, 50, kNumber, nullptr},
     {"move", 0, 100, 80, kNumber, nullptr}, {"retrig", 0, 1, 0, kChoice, kRetrig},
     {"color", 0, 100, 30, kNumber, nullptr}, {"double", 0, 1, 0, kChoice, kOffOn},
     {"swell", 0, 100, 60, kNumber, nullptr}, {"drift", 0, 100, 20, kNumber, nullptr}},
    {{"mode", 0, 5, 2, kChoice, kArpModes}, {"rate", 0, 2, 2, kChoice, kArpRates},
     {"range", 0, 2, 1, kChoice, kRanges3}, {"steps", 3, 8, 8, kNumber, nullptr},
     {"skip", 0, 100, 15, kNumber, nullptr}, {"accent", 0, 100, 50, kNumber, nullptr},
     {"tones", 0, 1, 0, kChoice, kTones}, {"jump", 0, 100, 10, kNumber, nullptr}},
    {{"contour", 0, 4, 0, kChoice, kContours}, {"rate", 0, 100, 45, kNumber, nullptr},
     {"range", 3, 14, 8, kNumber, nullptr}, {"rest", 0, 100, 35, kNumber, nullptr},
     {"motif", 0, 2, 1, kChoice, kMotifs}, {"repeat", 0, 100, 70, kNumber, nullptr},
     {"ctone", 0, 100, 70, kNumber, nullptr}, {"pent", 0, 1, 1, kChoice, kOffOn}}};

static const Spec kBlankSpec = {"", 0, 0, 0, kBlank, nullptr};

// The control of `page` at `slot` for a lane in `role`.
inline const Spec& spec(u32 page, u32 slot, u32 role) {
    if (slot >= 8) return kBlankSpec;
    if (page == kLane) return kLaneSpecs[slot];
    if (page == kRoute) return kRouteSpecs[slot];
    if (page == kDetail && role > kOff && role < kRoles) return kDetailSpecs[role - 1][slot];
    return kBlankSpec;
}

// A global control's value from its saved Q31 word: an enum's choice, as
// stock's stepped edits store it, or a percentage.
inline i32 global_value(u32 slot, i32 word) {
    const u32 q15 = word < 0 ? 0 : static_cast<u32>(word) >> 16;
    if (slot < kGlobals && kGlobalChoices[slot]) {
        const u32 choice = (q15 * kGlobalChoices[slot]) >> 15;
        return static_cast<i32>(choice < kGlobalChoices[slot] ? choice : kGlobalChoices[slot] - 1u);
    }
    return static_cast<i32>((q15 * 100u + 16384u) >> 15);
}

// Pitch class of scale degree `degree` (any integer) in semitones above C.
inline i32 floor_div(i32 a, i32 b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
inline i32 degree_semitones(i32 key, i32 scale, i32 degree) {
    const i32 octave = floor_div(degree, 7);
    return key + kScales[scale][degree - octave * 7] + 12 * octave;
}

// "Am7", "F#dim", "Cmaj7": the chord on `degree`, at most 7 characters.
inline void chord_name(char* out, i32 key, i32 scale, i32 degree, bool seventh) {
    const i32 r = degree_semitones(key, scale, degree);
    const i32 t3 = degree_semitones(key, scale, degree + 2) - r, t5 = degree_semitones(key, scale, degree + 4) - r;
    const i32 t7 = degree_semitones(key, scale, degree + 6) - r;
    const char* quality = t3 == 4 ? (t5 == 8 ? "aug" : "") : t5 == 6 ? "dim" : "m";
    if (seventh) {
        if (t3 == 4 && t5 == 7) quality = t7 == 11 ? "maj7" : "7";
        else if (t3 == 3 && t5 == 7) quality = t7 == 11 ? "mM7" : "m7";
        else if (t5 == 6) quality = t7 == 9 ? "dim7" : "m7b5";
    }
    u32 n = 0;
    for (const char* c = kNoteNames[((r % 12) + 12) % 12]; *c; ++c) out[n++] = *c;
    for (const char* c = quality; *c && n < 7; ++c) out[n++] = *c;
    out[n] = 0;
}

// "vi", "IV", "ii°" as "iio": the degree's numeral, at most 5 characters.
inline void numeral(char* out, i32 key, i32 scale, i32 degree) {
    static const char* const upper[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* const lower[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const i32 r = degree_semitones(key, scale, degree);
    const i32 t3 = degree_semitones(key, scale, degree + 2) - r, t5 = degree_semitones(key, scale, degree + 4) - r;
    const i32 d = ((degree % 7) + 7) % 7;
    u32 n = 0;
    for (const char* c = t3 == 4 ? upper[d] : lower[d]; *c; ++c) out[n++] = *c;
    if (t3 == 4 && t5 == 8) out[n++] = '+';
    if (t3 != 4 && t5 == 6) out[n++] = 'o';
    out[n] = 0;
}

// "C4", "F#2": MIDI note names.
inline void note_name(char* out, i32 note) {
    u32 n = 0;
    for (const char* c = kNoteNames[((note % 12) + 12) % 12]; *c; ++c) out[n++] = *c;
    i32 octave = floor_div(note, 12) - 1;
    if (octave < 0) { out[n++] = '-'; octave = -octave; }
    if (octave >= 10) out[n++] = static_cast<char>('0' + octave / 10);
    out[n++] = static_cast<char>('0' + octave % 10);
    out[n] = 0;
}

inline void number(char* out, i32 value, bool sign) {
    u32 n = 0;
    if (value < 0) { out[n++] = '-'; value = -value; }
    else if (sign && value > 0) out[n++] = '+';
    char reverse[8];
    u32 r = 0;
    do { reverse[r++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && r < 8);
    while (r) out[n++] = reverse[--r];
    out[n] = 0;
}

// A control's value as its page shows it, at most 7 characters.
inline void format(char* out, const Spec& s, i32 value) {
    switch (s.format) {
    case kNumber: number(out, value, false); break;
    case kSigned: number(out, value, true); break;
    case kCentred: number(out, value - 50, true); break;
    case kChoice: {
        u32 n = 0;
        const i32 i = value < s.minimum ? s.minimum : value > s.maximum ? s.maximum : value;
        for (const char* c = s.choices[i - s.minimum]; *c && n < 7; ++c) out[n++] = *c;
        out[n] = 0;
        break;
    }
    case kNote: note_name(out, value); break;
    case kLaneNumber: number(out, value + 1, false); break;
    case kTarget: out[0] = 't'; number(out + 1, value + 1, false); break;
    default: out[0] = 0;
    }
}
}
