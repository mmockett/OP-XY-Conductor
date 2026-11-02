#pragma once
// Conductor's generator: every lane's notes for one bar, a pure function of the
// song, the eight saved controls and the bar number, drawn from an integer
// hash. 16 steps a bar, 24 ticks a step (96 per quarter note). Probabilities
// are Q16 (65536 is certain). Firmware links no 64-bit helpers, so every
// product stays within 32 bits.
#include "specs.h"

namespace conductor {

// What a lane keeps, by its pages' slots.
struct Lane {
    i8 main[8];                 // kLaneSpecs
    i8 detail[kRoles - 1][8];   // kDetailSpecs, each role's own
    i8 route[8];                // kRouteSpecs
    u16 salt;
    i16 frozen_at;              // the phrase it holds, or -1
    u8 variation, fill;
};
// What a project keeps: the seed, the shift actions' state and the lanes.
struct Song {
    u16 seed;
    u8 variation, fill;
    i16 frozen_at;
    u16 reserved;
    Lane lanes[kLanes];
};
struct Globals { i32 v[kGlobals]; };

inline void set_role(Lane& lane, u32 role) {
    lane.main[kRoleSlot] = static_cast<i8>(role);
    lane.main[kLock] = role == kBass ? 0 : 1;
}
inline void default_song(Song& song) {
    static const u8 roles[kLanes] = {kDrum, kBass, kChord, kLead, kArp, kPad, kOff, kOff};
    song.seed = 0;
    song.variation = 0;
    song.fill = 50;
    song.frozen_at = -1;
    song.reserved = 0;
    for (u32 l = 0; l < kLanes; ++l) {
        Lane& lane = song.lanes[l];
        for (u32 s = 0; s < 8; ++s) {
            lane.main[s] = static_cast<i8>(kLaneSpecs[s].initial);
            lane.route[s] = static_cast<i8>(kRouteSpecs[s].initial);
            for (u32 r = 0; r + 1 < kRoles; ++r) lane.detail[r][s] = static_cast<i8>(kDetailSpecs[r][s].initial);
        }
        lane.route[kTargetSlot] = static_cast<i8>(l);
        lane.salt = 0;
        lane.frozen_at = -1;
        lane.variation = 0;
        lane.fill = 100;
        set_role(lane, roles[l]);
    }
}

// One note of a bar: its tick from the bar's start, lane, MIDI pitch,
// velocity, length in ticks and drum voice (0..7), or kMelodic.
const u8 kMelodic = 255;
struct Event { u16 tick, length; u8 lane, pitch, velocity, voice; };
const u32 kEventCapacity = 192;
struct Bar { u32 count; Event events[kEventCapacity]; };

// Integer hash: the only source of randomness.
inline u32 mix(u32 h, u32 v) {
    h = (h ^ v) * 0x9e3779b1u;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    return h ^ (h >> 13);
}
inline u32 finish(u32 h) {
    h = (h ^ (h >> 16)) * 0x27d4eb2du;
    return h ^ (h >> 15);
}
inline u32 hash(u32 a) { return finish(mix(0x811c9dc5u, a)); }
inline u32 hash(u32 a, u32 b) { return finish(mix(mix(0x811c9dc5u, a), b)); }
inline u32 hash(u32 a, u32 b, u32 c) { return finish(mix(mix(mix(0x811c9dc5u, a), b), c)); }
inline u32 hash(u32 a, u32 b, u32 c, u32 d) { return finish(mix(mix(mix(mix(0x811c9dc5u, a), b), c), d)); }
inline u32 hash(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) {
    return finish(mix(mix(mix(mix(mix(mix(0x811c9dc5u, a), b), c), d), e), f));
}
inline u32 seed_label(u32 seed) { return hash(0xc0de, seed) & 0xffff; }

// Q16 probabilities.
const i32 kCertain = 65536;
inline bool chance(u32 h, i32 q16) { return q16 > 0 && (q16 >= kCertain || static_cast<i32>(h >> 16) < q16); }
// numerator / denominator in Q16, for 0 <= numerator and 0 < denominator.
inline i32 q16(i32 numerator, i32 denominator) {
    u32 n = static_cast<u32>(numerator < 0 ? 0 : numerator), d = static_cast<u32>(denominator);
    if (n >= d) return kCertain;
    while (n > 0xffffu) { n >>= 1; d >>= 1; }
    return static_cast<i32>((n << 16) / d);
}
// h scaled to 0..n-1, for n <= 65536.
inline u32 pick(u32 h, u32 n) { return ((h >> 16) * n) >> 16; }
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// round(n / d) for d > 0, halves up; 2n + d within 32 bits.
inline i32 round_div(i32 n, i32 d) { return floor_div(2 * n + d, 2 * d); }

inline i32 phrase_of(i32 bar) { return floor_div(bar, kPhrase); }
inline i32 frozen_phrase(i32 p, i32 a, i32 b = -1) {
    if (a >= 0 && a < p) p = a;
    if (b >= 0 && b < p) p = b;
    return p;
}

// The phrase a slot last mutated in: evolution without stored state. Each
// phrase, a slot redraws with probability `prob`.
inline i32 epoch(u32 seed, u32 slot, i32 p, i32 prob) {
    if (prob <= 0) return 0;
    for (i32 q = p; q > 0 && q > p - 48; --q)
        if (chance(hash(seed, 0x77, slot, static_cast<u32>(q)), prob)) return q;
    return 0;
}

// The chord a step falls in.
struct Chord { i32 degree, index, position, start, length; bool seventh; };

// Chord progressions: Markov tables over scale degrees, or fixed shapes.
struct Weighted { i8 value; u8 weight; };
static const Weighted kPopTable[7][4] = {
    {{3, 3}, {4, 3}, {5, 3}, {1, 1}}, {{4, 4}, {3, 1}}, {{5, 3}, {3, 2}},
    {{4, 3}, {0, 2}, {5, 1}, {1, 1}}, {{0, 3}, {5, 3}, {3, 2}}, {{3, 4}, {4, 2}, {1, 2}}, {{0, 3}, {2, 1}}};
static const Weighted kMinorTable[7][4] = {
    {{5, 3}, {3, 3}, {6, 3}, {4, 1}}, {{4, 3}, {6, 1}}, {{6, 3}, {5, 2}, {3, 1}},
    {{4, 2}, {0, 2}, {6, 2}}, {{0, 4}, {5, 1}}, {{2, 3}, {6, 3}, {3, 2}}, {{0, 3}, {2, 2}, {5, 1}}};
static const Weighted kModalTable[4] = {{6, 3}, {3, 2}, {1, 2}, {4, 1}};
static const Weighted kWanderTable[4] = {{-2, 1}, {-1, 2}, {1, 2}, {2, 1}};
static const i8 kJazzLoops[5][4] = {{1, 4, 0, 0}, {1, 4, 0, 5}, {0, 5, 1, 4}, {2, 5, 1, 4}, {3, 6, 2, 5}};

inline i32 weighted(const Weighted* choices, u32 h) {
    u32 total = 0, n = 0;
    while (n < 4 && choices[n].weight) total += choices[n++].weight;
    i32 x = static_cast<i32>(pick(h, total));
    for (u32 i = 0; i < n; ++i) {
        x -= choices[i].weight;
        if (x < 0) return choices[i].value;
    }
    return choices[n - 1].value;
}

// The shared material of one bar's generation.
class World {
public:
    World(const Song& song, const Globals& globals) : song_(song), g_(globals.v), cached_phrase_(-1) {
        seed_ = hash(0xc0de, song.seed);
    }
    const Song& song() const { return song_; }
    const i32* g() const { return g_; }
    u32 seed() const { return seed_; }
    // Evolution: up to 35% a phrase.
    i32 evolve() const { return q16(g_[kEvolve] * 35, 10000); }
    i32 semitones(i32 degree) const { return degree_semitones(g_[kKey], g_[kScale], degree); }
    bool minor() const { return kScales[g_[kScale]][2] == 3; }

    const i8* progression(i32 p) {
        if (p != cached_phrase_) {
            build(p, loop_);
            cached_phrase_ = p;
        }
        return loop_;
    }

    Chord chord_at(i32 absolute) {
        const i32 rate = kHarmonySteps[g_[kHarmony]];
        Chord c;
        c.index = floor_div(absolute, rate);
        c.position = ((c.index % 4) + 4) % 4;
        c.degree = progression(phrase_of(floor_div(c.index * rate, kSteps)))[c.position];
        c.start = c.index * rate;
        c.length = rate;
        c.seventh = g_[kProgression] == kJazz || g_[kComplexity] >= 65;
        return c;
    }

    // The shared rhythm every lane leans on: a Euclidean kick, a backbeat
    // and the accents between them, one bit per step.
    struct Skeleton { u32 kick, snare, accent; };
    Skeleton skeleton(i32 bar) const {
        const u32 s = hash(seed_, song_.variation, 0x5e1);
        const i32 p = frozen_phrase(phrase_of(bar), song_.frozen_at);
        const i32 prob = evolve() / 2;
        const u32 k = 2 + static_cast<u32>((g_[kDensity] * 4 + 50) / 100);
        u32 base = 0;
        for (u32 i = 0; i < kSteps; ++i)
            if ((i * k) % kSteps < k) base |= 1u << i;
        u32 kick = base;
        const u32 slots = static_cast<u32>(bar & 1) * kSteps;
        for (u32 i = 1; i < kSteps; ++i) {
            if (!(base >> i & 1)) continue;
            const i32 ep = epoch(s, slots + i, p, prob);
            if (chance(hash(s, 1, slots + i, static_cast<u32>(ep)), q16(g_[kComplexity] * 6, 1000))) {
                const u32 j = hash(s, 2, slots + i, static_cast<u32>(ep)) < 0x80000000u ? i - 1 : i + 1;
                if (j > 0 && j < kSteps && !(kick >> j & 1)) kick = (kick & ~(1u << i)) | (1u << j);
            }
        }
        Skeleton out;
        out.kick = kick;
        out.snare = (1u << 4) | (1u << 12);
        out.accent = kick | out.snare | 0x1111u;
        return out;
    }

private:
    void build(i32 p, i8* loop) const {
        const i32 style = g_[kProgression];
        const u32 s = hash(seed_, song_.variation, 0x9a0);
        const Weighted (*table)[4] = style == kMinor ? kMinorTable : kPopTable;
        const u32 u0 = hash(s, 0x9a1, 0);
        if (style == kJazz) {
            for (u32 i = 0; i < 4; ++i) loop[i] = kJazzLoops[pick(u0, 5)][i];
        } else if (style == kModal) {
            const i8 a = static_cast<i8>(weighted(kModalTable, u0));
            loop[0] = 0; loop[1] = a; loop[2] = 0; loop[3] = a;
        } else if (style == kDrone) {
            loop[0] = loop[1] = loop[2] = 0;
            loop[3] = g_[kComplexity] > 50 && chance(u0, q16(6, 10)) ? 3 : 0;
        } else if (style == kWander) {
            loop[0] = 0;
            for (u32 i = 1; i < 4; ++i)
                loop[i] = static_cast<i8>((((loop[i - 1] + weighted(kWanderTable, hash(s, 0x9a1, i))) % 7) + 7) % 7);
        } else {
            loop[0] = 0;
            for (u32 i = 1; i < 4; ++i) loop[i] = static_cast<i8>(weighted(table[loop[i - 1]], hash(s, 0x9a1, i)));
        }
        // Evolution swaps chords 2..4 now and then, every two phrases at most.
        if (style == kDrone) return;
        const i32 pp = frozen_phrase(floor_div(p, 2), song_.frozen_at >= 0 ? song_.frozen_at / 2 : -1);
        for (u32 i = 1; i < 4; ++i) {
            const i32 ep = epoch(s, 0x100 + i, pp, evolve() / 2);
            if (ep > 0) loop[i] = static_cast<i8>(weighted(table[((loop[i - 1] % 7) + 7) % 7], hash(s, 0x200 + i, static_cast<u32>(ep))));
        }
    }

    const Song& song_;
    const i32* g_;
    u32 seed_;
    i32 cached_phrase_;
    i8 loop_[4];
};

// Voicings: up to eight notes.
struct Voicing { u32 count; i32 notes[8]; };

inline void voice_degrees(const Chord& c, u32 voice, Voicing& out) {
    static const i8 shapes[6][4] = {{0, 2, 4, -1}, {0, 2, 4, 6}, {0, 2, 6, 8}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 4, 7, -1}};
    out.count = 0;
    for (u32 i = 0; i < 4; ++i)
        if (shapes[voice][i] >= 0) out.notes[out.count++] = c.degree + shapes[voice][i];
}
// Each pitch class stacked upward from `low`, starting at inversion `inv`.
inline void build_voicing(const i32* semis, u32 n, u32 inv, i32 low, Voicing& out) {
    i32 previous = low - 1;
    out.count = n;
    for (u32 i = 0; i < n; ++i) {
        const i32 pc = semis[(i + inv) % n];
        const i32 note = previous + 1 + (((pc - (previous + 1)) % 12) + 12) % 12;
        out.notes[i] = note;
        previous = note;
    }
}
inline void sort(i32* v, u32 n) {
    for (u32 i = 1; i < n; ++i)
        for (u32 j = i; j > 0 && v[j - 1] > v[j]; --j) { const i32 t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
}
inline i32 distance(const Voicing& previous, const Voicing& v) {
    i32 sum = 0;
    for (u32 i = 0; i < v.count; ++i) {
        i32 best = 99;
        for (u32 j = 0; j < previous.count; ++j) {
            const i32 d = v.notes[i] > previous.notes[j] ? v.notes[i] - previous.notes[j] : previous.notes[j] - v.notes[i];
            if (d < best) best = d;
        }
        sum += best;
    }
    return sum;
}
// The voicing nearest the previous one, weighed against staying near `center`.
inline void pick_voicing(const i32* semis, u32 n, const Voicing* previous, i32 center, u32 inv, i32 move,
                         Voicing& out) {
    if (!previous || move < 5) {
        build_voicing(semis, n, inv % n, center - 7, out);
        return;
    }
    static const i32 lows[3] = {-12, -7, -2};
    i32 best = -1;
    for (u32 i = 0; i < n; ++i) {
        for (u32 l = 0; l < 3; ++l) {
            Voicing v;
            build_voicing(semis, n, i, center + lows[l], v);
            i32 sum = 0;
            for (u32 k = 0; k < n; ++k) sum += v.notes[k];
            const i32 off = sum - center * static_cast<i32>(n);
            const i32 score = distance(*previous, v) * move * 2 * static_cast<i32>(n) + (off < 0 ? -off : off) * 100;
            if (best < 0 || score < best) {
                best = score;
                out = v;
            }
        }
    }
}

// One lane's view of one bar.
struct Context {
    World* world;
    const Lane* lane;
    u32 index, role;
    i32 bar, p, key;
    u32 ls;
    i32 prob;
    const i8* d;
    World::Skeleton skeleton;
    i32 energy, complexity;  // 0..100
    Bar* out;
    // Memoised epochs by slot.
    u32 memo_slot[64];
    i32 memo_epoch[64];
    u32 memo_count;

    i32 key_of(i32 b) const {
        return frozen_phrase(phrase_of(b), world->song().frozen_at, lane->frozen_at) * kPhrase + (((b % 4) + 4) % 4);
    }
    i32 chord_key(const Chord& c) const {
        return key_of(floor_div(c.start, kSteps)) * 2 + (((c.start % 16) + 16) % 16 >= 8 ? 1 : 0);
    }
    u32 u(u32 slot, u32 salt) {
        i32 ep = -1;
        for (u32 i = 0; i < memo_count; ++i)
            if (memo_slot[i] == slot) { ep = memo_epoch[i]; break; }
        if (ep < 0) {
            ep = epoch(ls, slot, p, prob);
            if (memo_count < 64) {
                memo_slot[memo_count] = slot;
                memo_epoch[memo_count++] = ep;
            }
        }
        return hash(ls, salt, slot, static_cast<u32>(ep));
    }
    Chord chord_at(i32 step) { return world->chord_at(bar * static_cast<i32>(kSteps) + step); }
    bool accent(u32 s) const { return skeleton.accent >> s & 1; }
};

inline i32 fold(i32 n, i32 lo, i32 hi) {
    if (lo > hi) return -1;
    while (n < lo) n += 12;
    while (n > hi) n -= 12;
    return n < lo || n < 0 || n > 127 ? -1 : n;
}

// Adds a note at `step` (plus `offset` ticks and swing), with velocity and
// length in hundredths.
inline void emit(Context& c, i32 step, i32 offset, i32 pitch, i32 velocity100, i32 length100, u8 voice = kMelodic) {
    const Lane& lane = *c.lane;
    i32 tick = step * static_cast<i32>(kTicks) + offset +
               (step % 2 == 1 ? (c.world->g()[kSwing] * 10 + 50) / 100 : 0);
    tick = clampi(tick, 0, kBarTicks - 1);
    if (voice == kMelodic) pitch += lane.main[kOct] * 12;
    pitch = fold(pitch + lane.route[kXpose], lane.route[kLow], lane.route[kHigh]);
    if (pitch < 0 || c.out->count >= kEventCapacity) return;
    const i32 length = round_div(length100, 100);
    const i32 velocity = round_div(velocity100 * (3500 + 65 * lane.main[kVel]), 1000000);
    Event& e = c.out->events[c.out->count++];
    e.tick = static_cast<u16>(tick);
    e.length = static_cast<u16>(clampi(length, 2, 4 * kBarTicks));
    e.lane = static_cast<u8>(c.index);
    e.pitch = static_cast<u8>(pitch);
    e.velocity = static_cast<u8>(clampi(velocity, 1, 127));
    e.voice = voice;
}
// Hundredths of a tick for `halves` half steps at the lane's gate.
inline i32 gate100(const Context& c, i32 halves) { return halves * 12 * c.lane->main[kGate]; }

// Drum voices: kick, snare, clap, rim, closed and open hat, high and low tom.
enum Voice { kKick, kSnare, kClap, kRim, kHat, kOpen, kTom, kFloor };
// XY follows a kit's keys from note 53; GM is General MIDI's.
static const u8 kDrumNotes[2][8] = {{53, 54, 58, 59, 61, 62, 55, 57}, {36, 38, 39, 37, 42, 46, 50, 45}};

inline void drums(Context& c) {
    struct Hit { u8 step, voice; i16 velocity; };
    Hit hits[80];
    u32 count = 0;
    const i8* d = c.d;
    const u32 base = static_cast<u32>(c.bar & 1) * kSteps;
    const i32 kick = d[0], snare = d[1], perc = d[3], E = c.energy, C = c.complexity;
    const i32 hats = d[2] * (4000 + 60 * E);  // millionths
    const u32 perc_k = 3 + static_cast<u32>((perc * 3 + 50) / 100);
    const u32 rotation = pick(c.u(base, 8), kSteps);
    for (u32 s = 0; s < kSteps; ++s) {
        const u32 slot = base + s;
        const bool on_kick = c.skeleton.kick >> s & 1;
        Hit h = {static_cast<u8>(s), 0, 0};
        if (on_kick && kick > 5 && (s == 0 || chance(c.u(slot, 1), q16(3500 + 65 * kick, 10000)))) {
            h.voice = kKick; h.velocity = s == 0 ? 120 : 104; hits[count++] = h;
        } else if (!on_kick && s % 2 == 0 &&
                   chance(c.u(slot, 2), q16((kick > 50 ? kick - 50 : 0) * 4 * (50 + C), 1000000))) {
            h.voice = kKick; h.velocity = 88; hits[count++] = h;
        }
        if ((c.skeleton.snare >> s & 1) && snare > 10) {
            h.voice = kSnare; h.velocity = 110; hits[count++] = h;
            if (s == 12 && chance(c.u(slot, 5), q16(C * 6, 1000))) { h.voice = kClap; h.velocity = 96; hits[count++] = h; }
        } else if ((s == 7 || s == 10 || s == 15) && chance(c.u(slot, 3), q16(snare * C, 20000))) {
            h.voice = kSnare; h.velocity = 84; hits[count++] = h;
        } else if (!c.accent(s) && s % 2 == 1 && chance(c.u(slot, 4), q16(d[5] * 4, 1000))) {
            h.voice = kSnare; h.velocity = 34; hits[count++] = h;
        }
        bool hat = false;
        if (hats >= 650000) hat = s % 2 == 0 || chance(c.u(slot, 6), q16((hats - 650000) * 5, 2000000));
        else if (hats >= 400000) hat = s % 2 == 0;
        else if (hats >= 150000) hat = s % 4 == 2;
        if (hat) {
            const bool open = (s == 14 || s == 6) &&
                              chance(c.u(slot, 7), q16(C * hats * 5 + 50000000, 1000000000));
            h.voice = open ? kOpen : kHat;
            h.velocity = s % 4 == 0 ? 92 : s % 2 == 0 ? 74 : 54;
            hits[count++] = h;
        }
        const u32 ps = (s + kSteps - rotation) % kSteps;
        if (perc > 5 && (ps * perc_k) % kSteps < perc_k && chance(c.u(slot, 9), q16(perc, 100))) {
            h.voice = c.u(slot, 10) < 0x80000000u ? kRim : kTom;
            h.velocity = 70;
            hits[count++] = h;
        }
    }
    // Fills: the end of each phrase rolls toms and snares, bigger every 8 bars.
    const bool fill_bar = ((c.bar % 4) + 4) % 4 == 3, big = ((c.bar % 8) + 8) % 8 == 7;
    const i32 f = fill_bar ? c.world->song().fill * c.lane->fill * d[4] * (big ? 10 : 6) : 0;  // 1e7ths
    if (f * 50 > 10000000) {
        const u32 start = kSteps - static_cast<u32>(3 + round_div(f * 9, 10000000));
        u32 kept = 0;
        for (u32 i = 0; i < count; ++i)
            if (hits[i].step < start || hits[i].voice == kKick) hits[kept++] = hits[i];
        count = kept;
        for (u32 s = start; s < kSteps && count < 80; ++s) {
            if (!chance(hash(c.ls, 0x11, s, static_cast<u32>(c.p)), q16(35 * 10000000 + 60 * f, 1000000000))) continue;
            const i32 t = static_cast<i32>(s - start), span = static_cast<i32>(kSteps - start);
            Hit h = {static_cast<u8>(s), static_cast<u8>(t * 100 < 33 * span ? kTom : t * 100 < 66 * span ? kFloor : kSnare),
                     static_cast<i16>(70 + round_div(t * 45, span))};
            hits[count++] = h;
        }
    }
    const u8* notes = kDrumNotes[d[7] ? 1 : 0];
    for (u32 i = 0; i < count; ++i) {
        const Hit& h = hits[i];
        // Humanise: jitter in -1..1, Q16.
        const i32 jitter = static_cast<i32>(hash(c.ls, 0x12, base + h.step, static_cast<u32>(c.key)) >> 15) - 65536;
        const i32 offset = round_div(jitter * d[6] * 4, 100 * 65536);
        const i32 velocity100 = h.velocity * 100 + round_div(jitter * d[6] * 25, 65536);
        emit(c, h.step, offset > 0 ? offset : 0, notes[h.voice], velocity100, 600, h.voice);
    }
}

inline i32 bass_note(const World& w, i32 degree) {
    const i32 pc = ((w.semitones(degree) % 12) + 12) % 12;
    return 36 + pc - (pc >= 7 ? 12 : 0);
}

inline void bass(Context& c) {
    const i8* d = c.d;
    World& w = *c.world;
    const i32 style = d[0], E = c.energy, C = c.complexity;
    const i32 motif = d[7] == 0 ? 1 : d[7] == 1 ? 2 : 4;
    const u32 base = static_cast<u32>(((c.bar % motif) + motif) % motif) * kSteps;
    const i32 lock = c.lane->main[kLock];
    const u32 free = 3 + static_cast<u32>((E * 5 + 50) / 100);
    u32 on[kSteps], count = 0;
    for (u32 s = 0; s < kSteps; ++s) {
        const u32 slot = base + s;
        bool hit;
        if (style == 3) hit = s % 4 == 0;
        else if (style == 4) hit = s == 0 || chance(c.u(slot, 1), q16(60 + E, 200));
        else {
            hit = lock == 0 ? (c.skeleton.kick >> s & 1) : lock == 1 ? c.accent(s) : (s * free) % kSteps < free;
            if (!hit && s % 2 == 0 && chance(c.u(slot, 1), q16(d[1] * E * 6, 100000))) hit = true;
            if (!hit && d[1] > 60 && s % 2 == 1 && chance(c.u(slot, 2), q16((d[1] - 60) * E, 10000))) hit = true;
        }
        if (hit && s != 0 && chance(c.u(slot, 3), q16(d[5] * 8, 1000))) hit = false;
        if (hit) on[count++] = s;
    }
    for (u32 n = 0; n < count; ++n) {
        const i32 s = static_cast<i32>(on[n]);
        const u32 slot = base + on[n];
        const Chord chord = c.chord_at(s);
        const i32 next_s = n + 1 < count ? static_cast<i32>(on[n + 1]) : kSteps;
        const Chord next = w.chord_at(c.bar * static_cast<i32>(kSteps) + next_s);
        const i32 root = bass_note(w, chord.degree);
        i32 pitch = root;
        i32 velocity = c.accent(on[n]) || s % 4 == 0 ? 10000 + 20 * d[3] : 8200 - 15 * d[3];
        switch (style) {
        case 0: if (s % 4 != 0 && chance(c.u(slot, 4), q16(C * 3, 1000))) pitch += 12; break;
        case 1: if (n % 2 == 1) pitch = root + w.semitones(chord.degree + 4) - w.semitones(chord.degree); break;
        case 2: if (n % 2 == 1) pitch += 12; break;
        case 3: {
            static const i32 walk[4] = {0, 2, 4, 5};
            const i32 beat = ((c.bar * static_cast<i32>(kSteps) + s - chord.start) / 4) % 4;
            if (beat > 0) pitch = root + w.semitones(chord.degree + walk[beat]) - w.semitones(chord.degree);
            break;
        }
        default: {
            static const i32 steps[7] = {0, 0, 0, 2, 4, 7, -1};
            const i32 dg = steps[pick(c.u(slot, 4), 7)];
            pitch = root + w.semitones(chord.degree + dg) - w.semitones(chord.degree);
            if (chance(c.u(slot, 5), q16(3, 10))) pitch += 12;
            velocity = chance(c.u(slot, 6), q16(d[3], 100)) ? 12200 : 7800;
        }
        }
        if (d[6] == 1 && s % 4 != 0 && chance(c.u(slot, 8), q16(1, 4))) pitch += 12;
        if (next.degree != chord.degree && next_s - s <= 4 && chance(c.u(slot, 7), q16(d[2], 100)))
            pitch = bass_note(w, next.degree) - 1;
        emit(c, s, 0, pitch, velocity, (next_s - s) * 24 * c.lane->main[kGate] + d[4] * 24);
    }
}

// Walks from the first chord of the progression's cycle, so voice leading
// needs no memory of what played before. `extra` adds the pad's colour tone.
inline void lead_voicing(Context& c, const Chord& chord, u32 voice, i32 center, u32 inv, i32 spread, i32 move,
                         i32 colour, Voicing& v) {
    Voicing previous;
    previous.count = 0;
    for (i32 index = chord.index - chord.position; index <= chord.index; ++index) {
        const Chord ch = index == chord.index ? chord : c.world->chord_at(index * chord.length);
        Voicing degrees;
        voice_degrees(ch, voice, degrees);
        if (colour >= 0 && chance(hash(c.ls, 0x30, static_cast<u32>(c.chord_key(ch))), q16(colour, 100)))
            degrees.notes[degrees.count++] = ch.degree + 8;
        i32 semis[8];
        for (u32 i = 0; i < degrees.count; ++i) semis[i] = c.world->semitones(degrees.notes[i]);
        pick_voicing(semis, degrees.count, previous.count ? &previous : nullptr, center, inv, move, v);
        if (v.count >= 3 && chance(hash(c.ls, 0x40, static_cast<u32>(c.chord_key(ch))), q16(spread, 100))) {
            v.notes[v.count - 2] -= 12;
            sort(v.notes, v.count);
        }
        previous = v;
    }
}

// Whether bar `b` pushes its next chord onto its last eighth.
inline bool pushed(Context& c, i32 b, i32 push) {
    if (b < 0 || push <= 0 || !chance(hash(c.ls, 0x20, static_cast<u32>(c.key_of(b))), q16(push, 100))) return false;
    const i32 next = (b + 1) * static_cast<i32>(kSteps);
    return c.world->chord_at(next).start == next;
}

inline void chords(Context& c) {
    const i8* d = c.d;
    World& w = *c.world;
    const i32 rhythm = d[2], E = c.energy;
    const i32 bar_start = c.bar * static_cast<i32>(kSteps);
    struct Hit { bool on; i32 halves; Chord chord; };
    Hit hits[kSteps];
    for (u32 s = 0; s < kSteps; ++s) {
        const Chord chord = c.chord_at(static_cast<i32>(s));
        const bool start = bar_start + static_cast<i32>(s) == chord.start;
        Hit h = {false, 2, chord};
        if (rhythm == 0) { h.on = start; h.halves = 2 * chord.length; }
        else if (rhythm == 1) h.on = c.accent(s) && chance(c.u(s, 1), q16(4500 + 55 * E, 10000));
        else if (rhythm == 2) h.on = s % 4 == 2 && chance(c.u(s, 1), q16(50 + E, 100));
        else if (rhythm == 3) {
            const i32 weight = s % 4 == 0 ? 55 : s % 2 == 0 ? 40 : 18;
            h.on = start || chance(c.u(s, 1), q16(weight * (40 + E), 10000));
            h.halves = 3;
        } else { h.on = s % 2 == 0; h.halves = 4; }
        hits[s] = h;
    }
    if (pushed(c, c.bar - 1, d[7]) && w.chord_at(bar_start).start == bar_start) hits[0].on = false;
    if (pushed(c, c.bar, d[7])) {
        const Chord next = w.chord_at(bar_start + kSteps);
        hits[14].on = true;
        hits[14].halves = rhythm == 0 ? 2 * (next.length + 2) : 3;
        hits[14].chord = next;
        hits[15].on = false;
    }
    const i32 strum = (d[3] * 5 + 50) / 100;
    for (u32 s = 0; s < kSteps; ++s) {
        if (!hits[s].on) continue;
        Voicing v;
        lead_voicing(c, hits[s].chord, static_cast<u32>(d[0]), 57, static_cast<u32>(d[1]), d[4], d[5], -1, v);
        const i32 velocity = c.accent(s) ? 9200 + 30 * d[6] : 7600 - 15 * d[6];
        for (u32 i = 0; i < v.count; ++i)
            emit(c, static_cast<i32>(s), static_cast<i32>(i) * strum, v.notes[i], velocity, gate100(c, hits[s].halves));
    }
}

inline void pads(Context& c) {
    const i8* d = c.d;
    World& w = *c.world;
    const bool per_bar = d[3] == 1;
    const i32 bar_start = c.bar * static_cast<i32>(kSteps);
    for (u32 s = 0; s < kSteps; ++s) {
        const i32 absolute = bar_start + static_cast<i32>(s);
        const Chord chord = c.chord_at(static_cast<i32>(s));
        if (!(absolute == chord.start || (per_bar && s == 0))) continue;
        const i32 chord_end = chord.start + chord.length, bar_end = bar_start + kSteps;
        const i32 end = per_bar && bar_end < chord_end ? bar_end : chord_end;
        Voicing v;
        lead_voicing(c, chord, static_cast<u32>(d[0]), 60, 0, d[1], d[2], d[4], v);
        if (per_bar && absolute != chord.start && chance(hash(c.ls, 0x31, static_cast<u32>(c.key)), q16(d[7], 100)))
            v.notes[v.count - 1] += w.semitones(chord.degree + 1) - w.semitones(chord.degree);
        if (d[5] == 1 && v.count < 8) v.notes[v.count++] = v.notes[0] + 12;
        for (u32 i = 0; i < v.count; ++i)
            emit(c, static_cast<i32>(s), 0, v.notes[i], 5000 + 50 * d[6], ((end - absolute) * 24 - 2) * 100);
    }
}

inline void arps(Context& c) {
    const i8* d = c.d;
    World& w = *c.world;
    const i32 mode = d[0], every = d[1] == 0 ? 4 : d[1] == 1 ? 2 : 1, octaves = d[2] + 1, E = c.energy;
    for (i32 s = 0; s < static_cast<i32>(kSteps); s += every) {
        const Chord chord = c.chord_at(s);
        i32 semis[7];
        u32 n = 0;
        if (d[6] == 1) for (i32 i = 0; i < 7; ++i) semis[n++] = w.semitones(chord.degree + i);
        else {
            semis[n++] = w.semitones(chord.degree);
            semis[n++] = w.semitones(chord.degree + 2);
            semis[n++] = w.semitones(chord.degree + 4);
            if (chord.seventh) semis[n++] = w.semitones(chord.degree + 6);
        }
        Voicing one;
        build_voicing(semis, n, 0, 55, one);
        i32 played[24], sorted[24], list[48];
        u32 count = 0, length = 0;
        for (i32 o = 0; o < octaves; ++o)
            for (u32 i = 0; i < one.count; ++i) played[count++] = one.notes[i] + 12 * o;
        for (u32 i = 0; i < count; ++i) sorted[i] = played[i];
        sort(sorted, count);
        if (mode == 0) for (u32 i = 0; i < count; ++i) list[length++] = sorted[i];
        else if (mode == 1) for (u32 i = count; i-- > 0;) list[length++] = sorted[i];
        else if (mode == 2) {
            for (u32 i = 0; i < count; ++i) list[length++] = sorted[i];
            for (u32 i = count - 1; i-- > 1;) list[length++] = sorted[i];
        } else if (mode == 4) {
            for (i32 i = 0, j = static_cast<i32>(count) - 1; i <= j; ++i, --j) {
                list[length++] = sorted[i];
                if (i != j) list[length++] = sorted[j];
            }
        } else for (u32 i = 0; i < count; ++i) list[length++] = played[i];
        const i32 step_count = floor_div(c.bar * static_cast<i32>(kSteps) + s, every);
        const u32 slot = static_cast<u32>(s);
        i32 note = mode == 3 ? list[pick(c.u(slot, 3), length)]
                             : list[static_cast<u32>(((step_count % d[3]) + d[3]) % d[3]) % length];
        if (chance(c.u(slot, 1), q16(d[4] * (120 - E), 10000))) continue;
        if (chance(c.u(slot, 2), q16(d[7], 100))) note += 12;
        const i32 velocity = c.accent(slot) ? 8000 + 40 * d[5] : 7200 - 10 * d[5];
        emit(c, s, 0, note, velocity, gate100(c, 2 * every));
    }
}

// sin of tenths of a degree, Q10.
inline i32 sine_q10(i32 tenths) {
    static const u16 quarter[10] = {0, 178, 350, 512, 658, 784, 887, 962, 1008, 1024};
    tenths = ((tenths % 3600) + 3600) % 3600;
    const bool negative = tenths >= 1800;
    if (negative) tenths -= 1800;
    if (tenths > 900) tenths = 1800 - tenths;
    const i32 i = tenths / 100, r = tenths % 100;
    const i32 v = i >= 9 ? 1024 : quarter[i] + (quarter[i + 1] - quarter[i]) * r / 100;
    return negative ? -v : v;
}

inline void leads(Context& c) {
    const i8* d = c.d;
    World& w = *c.world;
    const i32 motif = d[4] == 0 ? 1 : d[4] == 1 ? 2 : 4;
    const i32 instance = floor_div(c.key, motif);
    const i32 variant = chance(hash(c.ls, 0x50, static_cast<u32>(instance)), q16(d[5], 100)) ? 0 : instance + 1;
    const i32 span = motif * static_cast<i32>(kSteps), E = c.energy, C = c.complexity;
    const bool minor = w.minor();
    struct Note { i32 step, pitch, velocity; };
    Note notes[kSteps];
    u32 count = 0;
    for (i32 s = 0; s < static_cast<i32>(kSteps); ++s) {
        const i32 m = (((c.bar % motif) + motif) % motif) * static_cast<i32>(kSteps) + s;
        const u32 slot = static_cast<u32>(m + variant * 64);
        i32 weight = s % 4 == 0 ? 1400 : s % 2 == 0 ? 1000 : 5 * (C + 30);  // thousandths
        if (m >= span - 2) weight = weight * 3 / 10;
        const i32 prob = q16(d[1] * (300 + 7 * E), 100000) * (weight * (1000 - 7 * d[3]) / 1000) / 1000;
        if (!chance(c.u(slot, 1), prob)) continue;
        i32 contour;  // Q10
        switch (d[0]) {
        case 0: contour = sine_q10(m * 1800 / span); break;
        case 1: contour = m * 1024 / span; break;
        case 2: contour = 1024 - m * 1024 / span; break;
        case 3: contour = 512 + sine_q10(m * 7200 / span) / 2; break;
        default: contour = static_cast<i32>(hash(c.ls, 0x51, static_cast<u32>(m / 4), static_cast<u32>(variant)) >> 22);
        }
        const i32 jitter = static_cast<i32>(c.u(slot, 2) >> 15) - 65536;
        i32 degree = ((contour * d[2] + 512) >> 10) - 2 + round_div(jitter * C * 15, 1000 * 65536);
        const Chord chord = c.chord_at(s);
        if (c.accent(static_cast<u32>(s)) && chance(c.u(slot, 3), q16(d[6], 100))) {
            i32 best = degree, best_distance = 99;
            for (i32 k = -3; k <= 3; ++k) {
                const i32 candidate = ((degree + k) % 7 + 7) % 7;
                for (i32 t = 0; t < 3; ++t) {
                    const i32 tone = ((chord.degree + 2 * t) % 7 + 7) % 7;
                    const i32 a = k < 0 ? -k : k;
                    if (candidate == tone && a < best_distance) { best = degree + k; best_distance = a; }
                }
            }
            degree = best;
        }
        const i32 pc = ((degree % 7) + 7) % 7;
        if (d[7] == 1 && (minor ? (pc == 1 || pc == 5) : (pc == 3 || pc == 6))) degree += 1;
        Note note = {s, 60 + w.semitones(degree), s % 4 == 0 ? 9200 : 7600};
        notes[count++] = note;
    }
    for (u32 i = 0; i < count; ++i) {
        const i32 next = i + 1 < count ? notes[i + 1].step : (notes[i].step + 4 < 16 ? notes[i].step + 4 : 16);
        emit(c, notes[i].step, 0, notes[i].pitch, notes[i].velocity, gate100(c, 2 * (next - notes[i].step)));
    }
}

// Every lane's notes for bar `index`, in tick order.
inline void generate(const Song& song, const Globals& globals, i32 index, Bar& out) {
    World world(song, globals);
    out.count = 0;
    const World::Skeleton skeleton = world.skeleton(index);
    for (u32 l = 0; l < kLanes; ++l) {
        const Lane& lane = song.lanes[l];
        const u32 role = static_cast<u32>(lane.main[kRoleSlot]);
        if (role == kOff || role >= kRoles) continue;
        Context c;
        c.world = &world;
        c.lane = &lane;
        c.index = l;
        c.role = role;
        c.bar = index;
        c.p = frozen_phrase(phrase_of(index), song.frozen_at, lane.frozen_at);
        c.key = c.key_of(index);
        c.ls = hash(world.seed(), song.variation, l, lane.salt, lane.variation, role);
        const i32 prob = world.evolve() * lane.main[kVary] / 50;
        c.prob = prob < q16(9, 10) ? prob : q16(9, 10);
        c.d = lane.detail[role - 1];
        c.skeleton = skeleton;
        c.energy = clampi(globals.v[kDensity] + lane.main[kDens] - 50, 0, 100);
        c.complexity = globals.v[kComplexity];
        c.out = &out;
        c.memo_count = 0;
        switch (role) {
        case kDrum: drums(c); break;
        case kBass: bass(c); break;
        case kChord: chords(c); break;
        case kPad: pads(c); break;
        case kArp: arps(c); break;
        default: leads(c);
        }
    }
    // Stable insertion sort by tick.
    for (u32 i = 1; i < out.count; ++i)
        for (u32 j = i; j > 0 && out.events[j - 1].tick > out.events[j].tick; --j) {
            const Event t = out.events[j];
            out.events[j] = out.events[j - 1];
            out.events[j - 1] = t;
        }
}
}
