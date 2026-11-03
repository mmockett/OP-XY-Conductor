// Measures, on the host, how late Conductor's notes start when they start on
// GUI ticks. It runs the real control() with a clock: each GUI tick reads the
// transport at that moment, as the OP-XY's does, and every note the tick
// starts is compared with the time its tick in the song falls on.
//   timing [period_ms] [bpm]   prints a table over presets and tick periods
// What it leaves out: how often the transport's ticks update on the device,
// and stock's own note-on latency. tools/timing/onsets.py measures the
// device from a recording.
#include "control.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace conductor;

static opxy::control::Input input;
static opxy::control::Overlay overlay;
static ControlState state;
static Bar bar;

struct Result {
    u32 notes, dropped, most;  // notes started, dropped past note_capacity, most in one tick
    double mean_ms, max_ms, min_ms, p95_ms;
};

static double lateness[20000];

// Preset values (0..1) as stock stores them.
static void set_controls(const double v[kGlobals]) {
    for (u32 i = 0; i < kGlobals; ++i)
        input.values[11][i] = static_cast<i32>(static_cast<u32>(std::floor(v[i] * 32767 + 0.5)) << 16);
}

// Runs `seconds` of song at `bpm` with a GUI tick every `period_us`, plus
// up to `jitter_us` either way, and one tick of `stall_us` every 4 s.
static Result run(const double controls[kGlobals], u32 bpm, u32 period_us, u32 jitter_us, u32 stall_us, u32 seconds) {
    std::memset(&input, 0, sizeof input);
    std::memset(&state, 0, sizeof state);
    input.track = 11;
    input.transport.bpm_x10 = bpm * 10;
    set_controls(controls);
    control_reset(state);
    std::memset(&overlay, 0, sizeof overlay);
    control(state, input, overlay);  // stopped: takes in the controls

    // ticks96 per microsecond = bpm * 96 / 60e6.
    const double tick_us = 60e6 / (bpm * 96.0);
    Result r = {0, 0, 0, 0, 0, 1e9, 0};
    u32 count = 0, seed = 1;
    unsigned long long now_us = 0;
    u32 played_to = 0;  // ticks before this have had their notes
    bool first = true;
    while (now_us < static_cast<unsigned long long>(seconds) * 1000000u) {
        const u32 ticks = static_cast<u32>(std::floor(now_us / tick_us));
        std::memset(&overlay, 0, sizeof overlay);
        input.transport.playing = true;
        input.transport.ticks96 = ticks;
        control(state, input, overlay);
        // The notes due since the last tick, in the order control plays them.
        u32 due = 0;
        const u32 from = first ? ticks : played_to;
        for (u32 b = from / kBarTicks; b <= ticks / kBarTicks; ++b) {
            generate(state.song, state.globals, static_cast<i32>(b), bar);
            for (u32 i = 0; i < bar.count; ++i) {
                const Event& e = bar.events[i];
                const u32 t = b * kBarTicks + e.tick;
                if (t < from || t > ticks || state.song.lanes[e.lane].route[kMute]) continue;
                if (due++ >= opxy::control::note_capacity) continue;
                const double late = (now_us - t * tick_us) / 1000.0;
                if (count < sizeof lateness / sizeof lateness[0]) lateness[count++] = late;
            }
        }
        if (due != overlay.note_count + (due > opxy::control::note_capacity ? due - opxy::control::note_capacity : 0))
            std::printf("mismatch at tick %u: %u due, %u played\n", ticks, due, overlay.note_count);
        r.notes += overlay.note_count;
        r.dropped += due > overlay.note_count ? due - overlay.note_count : 0;
        r.most = overlay.note_count > r.most ? overlay.note_count : r.most;
        played_to = ticks + 1;
        first = false;
        seed = seed * 1664525u + 1013904223u;
        const i32 wobble = jitter_us ? static_cast<i32>(seed >> 8) % static_cast<i32>(2 * jitter_us + 1) - static_cast<i32>(jitter_us) : 0;
        const bool stall = stall_us && (now_us / 4000000u) != ((now_us + period_us) / 4000000u);
        now_us += stall ? stall_us : static_cast<u32>(static_cast<i32>(period_us) + wobble);
    }
    double sum = 0;
    for (u32 i = 0; i < count; ++i) {
        sum += lateness[i];
        r.max_ms = lateness[i] > r.max_ms ? lateness[i] : r.max_ms;
        r.min_ms = lateness[i] < r.min_ms ? lateness[i] : r.min_ms;
    }
    r.mean_ms = count ? sum / count : 0;
    // The 95th percentile, by counting.
    for (double edge = 0; count; edge += 0.1) {
        u32 below = 0;
        for (u32 i = 0; i < count; ++i) below += lateness[i] <= edge;
        if (below * 20 >= count * 19) {
            r.p95_ms = edge;
            break;
        }
    }
    return r;
}

int main(int argc, char** argv) {
    // key, scale, prog, dens, cmplx, hrate, swing, evolve: definition.json's presets, then everything at once.
    static const double presets[][kGlobals] = {
        {0.0417, 0.0714, 0.0833, 0.5, 0.4, 0.375, 0, 0.3},
        {0.7917, 0.2143, 0.25, 0.35, 0.55, 0.625, 0.3, 0.2},
        {0.4583, 0.0714, 0.75, 0.6, 0.75, 0.375, 0.55, 0.4},
        {0.0417, 0.0714, 0.0833, 1, 1, 0.125, 0.5, 1},
    };
    static const char* names[] = {"pop", "night", "jazz", "busiest"};
    const double only_ms = argc > 1 ? std::atof(argv[1]) : 0;
    const u32 only_bpm = argc > 2 ? static_cast<u32>(std::atoi(argv[2])) : 0;
    static const double periods_ms[] = {8.333, 16.667, 33.333};
    static const u32 tempos[] = {90, 120, 180};
    std::printf("GUI ticks every period +/- 10%%, 60 s each\n");
    std::printf("%-8s %5s %7s %6s %8s %8s %8s %8s %8s %5s\n", "preset", "bpm", "tick ms", "notes", "mean ms", "p95 ms",
                "max ms", "spread", "dropped", "most");
    for (u32 p = 0; p < 4; ++p)
        for (double period : periods_ms) {
            if (only_ms && std::fabs(period - only_ms) > 0.5) continue;
            for (u32 bpm : tempos) {
                if (only_bpm && bpm != only_bpm) continue;
                // A tenth of wobble, as a real tick has, keeps the tick from locking to the beat.
                const Result r = run(presets[p], bpm, static_cast<u32>(period * 1000), static_cast<u32>(period * 100), 0, 60);
                std::printf("%-8s %5u %7.1f %6u %8.2f %8.2f %8.2f %8.2f %8u %5u\n", names[p], bpm, period, r.notes,
                            r.mean_ms, r.p95_ms, r.max_ms, r.max_ms - r.min_ms, r.dropped, r.most);
            }
        }
    // A tick that wobbles by a quarter of its period, and one that stalls.
    std::printf("\nirregular ticks, 120 bpm, 60 s\n");
    std::printf("%-8s %-22s %6s %8s %8s %8s %8s %5s\n", "preset", "ticks", "notes", "mean ms", "p95 ms", "max ms", "dropped",
                "most");
    for (u32 p = 0; p < 4; ++p) {
        const Result a = run(presets[p], 120, 16667, 4167, 0, 60);
        const Result b = run(presets[p], 120, 16667, 0, 250000, 60);
        const Result c = run(presets[p], 120, 16667, 0, 1000000, 60);
        std::printf("%-8s %-22s %6u %8.2f %8.2f %8.2f %8u %5u\n", names[p], "16.7 ms +/- 4.2", a.notes, a.mean_ms, a.p95_ms,
                    a.max_ms, a.dropped, a.most);
        std::printf("%-8s %-22s %6u %8.2f %8.2f %8.2f %8u %5u\n", names[p], "16.7 ms, 250 ms /4 s", b.notes, b.mean_ms,
                    b.p95_ms, b.max_ms, b.dropped, b.most);
        std::printf("%-8s %-22s %6u %8.2f %8.2f %8.2f %8u %5u\n", names[p], "16.7 ms, 1 s /4 s", c.notes, c.mean_ms,
                    c.p95_ms, c.max_ms, c.dropped, c.most);
    }
    return 0;
}
