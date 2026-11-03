#include <opxy/track_control.hpp>
#include "generator.h"
#include "words.h"

namespace conductor {
// Conductor plays every lane's notes on instrument tracks 1..8 from aux track
// 12 (CV). M1 is its eight saved controls; M2..M4 are its own pages, whose
// encoders it takes. Shift + encoders 1..4 reroll, freeze, fill and vary: every
// lane on M1, the selected lane on M2..M4.
const u32 kSaved = 0x434e4431;  // the saved data's version: "CND1"
const u32 kFlashUs = 120000;    // how long a lane shows that it played

struct ControlState {
    u32 saved;  // kSaved once `song` holds a song
    Song song;
    u8 page, lane, playing, dirty;
    u32 next_tick;  // the first song tick not yet played
    i32 bar;        // the bar in `current`
    u32 generated;  // 1 once `current` holds it
    Globals globals;
    u32 flash_us[kLanes];
    Bar current;
    i32 phrase_at;      // the phrase `phrase` describes
    u32 phrase_stale;   // 1 when the song or the controls changed since
    PhraseCells phrase; // M1's beats and groove
};

inline void control_reset(ControlState& state) {
    if (state.saved != kSaved) {
        default_song(state.song);
        state.saved = kSaved;
    }
    state.page = kConduct;
    state.lane = 0;
    state.playing = 0;
    state.dirty = 1;
    state.next_tick = 0;
    state.bar = 0;
    state.generated = 0;
    state.phrase_at = 0;
    state.phrase_stale = 1;
    for (u32 l = 0; l < kLanes; ++l) state.flash_us[l] = 0;
}

inline u32 control_save(const ControlState& state, u8* data, u32 capacity) {
    const u32 bytes = 4 + sizeof(Song);
    if (capacity < bytes) return 0;
    for (u32 i = 0; i < 4; ++i) data[i] = static_cast<u8>(kSaved >> (8 * i));
    const u8* song = reinterpret_cast<const u8*>(&state.song);
    for (u32 i = 0; i < sizeof(Song); ++i) data[4 + i] = song[i];
    return bytes;
}

inline void control_load(ControlState& state, const u8* data, u32 bytes) {
    u32 version = 0;
    for (u32 i = 0; i < 4 && i < bytes; ++i) version |= static_cast<u32>(data[i]) << (8 * i);
    if (bytes == 4 + sizeof(Song) && version == kSaved) {
        u8* song = reinterpret_cast<u8*>(&state.song);
        for (u32 i = 0; i < sizeof(Song); ++i) song[i] = data[4 + i];
    } else {
        default_song(state.song);
    }
    state.saved = kSaved;
    state.dirty = 1;
    state.phrase_stale = 1;
}

inline Lane& selected(ControlState& state) { return state.song.lanes[state.lane]; }

inline i8 stepped(i8 value, const Spec& s, i32 direction) {
    return static_cast<i8>(clampi(value + direction, s.minimum, s.maximum));
}

// Shift + encoder: every lane on M1, the selected lane elsewhere.
inline void act(ControlState& state, u32 action, i32 direction) {
    const bool all = state.page == kConduct;
    Lane& lane = selected(state);
    Song& song = state.song;
    const i16 phrase = static_cast<i16>(phrase_of(state.bar));
    switch (action) {
    case 0:
        if (all) song.seed = static_cast<u16>(song.seed + direction);
        else lane.salt = static_cast<u16>(lane.salt + direction);
        break;
    case 1:
        if (all) song.frozen_at = direction > 0 ? phrase : -1;
        else lane.frozen_at = direction > 0 ? phrase : -1;
        break;
    case 2:
        if (all) song.fill = static_cast<u8>(clampi(song.fill + 5 * direction, 0, 100));
        else lane.fill = static_cast<u8>(clampi(lane.fill + 5 * direction, 0, 100));
        break;
    default:
        if (all) song.variation = static_cast<u8>(clampi(song.variation + direction, 0, 3));
        else lane.variation = static_cast<u8>(clampi(lane.variation + direction, 0, 3));
    }
}

inline void turn(ControlState& state, u32 slot, i32 direction, bool shift) {
    state.dirty = 1;
    state.phrase_stale = 1;
    if (shift) {
        act(state, slot % 4, direction);
        return;
    }
    Lane& lane = selected(state);
    const u32 role = static_cast<u32>(lane.main[kRoleSlot]);
    if ((state.page == kLane || state.page == kRoute) && slot == 0) {
        state.lane = static_cast<u8>(clampi(state.lane + direction, 0, kLanes - 1));
        return;
    }
    const Spec& s = spec(state.page, slot, role);
    if (s.format == kBlank) return;
    if (state.page == kLane && slot == kRoleSlot) set_role(lane, static_cast<u32>(stepped(lane.main[slot], s, direction)));
    else if (state.page == kLane) lane.main[slot] = stepped(lane.main[slot], s, direction);
    else if (state.page == kRoute) lane.route[slot] = stepped(lane.route[slot], s, direction);
    else lane.detail[role - 1][slot] = stepped(lane.detail[role - 1][slot], s, direction);
}

inline void ensure(ControlState& state, i32 bar) {
    if (state.generated && !state.dirty && state.bar == bar) return;
    generate(state.song, state.globals, bar, state.current);
    state.bar = bar;
    state.generated = 1;
    state.dirty = 0;
}

inline void play(ControlState& state, const Event& e, u32 tick_us, opxy::control::Overlay& overlay) {
    const Lane& lane = state.song.lanes[e.lane];
    if (lane.route[kMute]) return;
    const u32 us = e.length * tick_us;
    opxy::control::play(overlay, static_cast<u32>(lane.route[kTargetSlot]), e.pitch, e.velocity,
                        us < opxy::control::longest_note_us ? us : opxy::control::longest_note_us);
    state.flash_us[e.lane] = kFlashUs;
}

// Plays every note whose tick has come since the last call.
inline void advance(ControlState& state, const opxy::Transport& transport, opxy::control::Overlay& overlay) {
    const u32 now = transport.ticks96;
    if (!transport.playing) {
        state.playing = 0;
        return;
    }
    if (!state.playing || now < state.next_tick || now - state.next_tick > kBarTicks) state.next_tick = now;
    state.playing = 1;
    const u32 tick_us = 600000000u / ((transport.bpm_x10 ? transport.bpm_x10 : 1200u) * 96u);
    while (state.next_tick <= now) {
        const i32 bar = static_cast<i32>(state.next_tick / kBarTicks);
        ensure(state, bar);
        const u32 start = static_cast<u32>(bar) * kBarTicks, last = start + kBarTicks - 1;
        const u32 end = now < last ? now : last;
        for (u32 i = 0; i < state.current.count; ++i) {
            const u32 t = start + state.current.events[i].tick;
            if (t >= state.next_tick && t <= end) play(state, state.current.events[i], tick_us, overlay);
        }
        state.next_tick = end + 1;
    }
}

inline void control(ControlState& state, const opxy::control::Input& input, opxy::control::Overlay& overlay) {
    if (state.dirty) state.phrase_stale = 1;
    Globals globals;
    bool changed = false;
    for (u32 i = 0; i < kGlobals; ++i) {
        globals.v[i] = global_value(i, input.values[input.track][i]);
        changed = changed || globals.v[i] != state.globals.v[i];
    }
    if (changed) {
        state.globals = globals;
        state.dirty = 1;
        state.phrase_stale = 1;
    }
    if (input.page_key >= 1 && input.page_key <= kPages) state.page = static_cast<u8>(input.page_key - 1);
    for (u32 i = 0; i < input.turn_count; ++i) turn(state, input.turns[i].slot, input.turns[i].direction, input.shift != 0);
    for (u32 l = 0; l < kLanes; ++l)
        state.flash_us[l] = state.flash_us[l] > input.elapsed_us ? state.flash_us[l] - input.elapsed_us : 0;

    advance(state, input.transport, overlay);
    // Stopped, the song waits at its start whatever the transport's ticks
    // read, so nothing moves or evolves until play.
    const Position at = {input.transport.playing ? input.transport.ticks96 : 0u, input.transport.playing != 0};
    const i32 bar = static_cast<i32>(at.tick / kBarTicks);
    // M1 draws the whole phrase: four bars' generation, borrowing `current`,
    // once a phrase or after a change.
    if (state.page == kConduct && (state.phrase_stale || state.phrase_at != phrase_of(bar))) {
        phrase_cells(state.song, state.globals, phrase_of(bar), state.current, state.phrase);
        state.phrase_at = phrase_of(bar);
        state.phrase_stale = 0;
        state.generated = 0;
    }
    ensure(state, bar);
    overlay.own_turns = state.page != kConduct || input.shift;

    Words w;
    words(state.song, state.page, state.lane, state.current, state.bar, at, input.shift != 0, state.flash_us,
          state.globals, state.phrase, w);
    for (u32 i = 0; i < kWords; ++i) overlay.display[i] = static_cast<i32>(w.v[i]);
}
}
