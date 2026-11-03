#!/usr/bin/env python3
"""Measures how late Conductor's notes start on the device, from a recording.

Record the OP-XY playing one drum lane with its notes on the grid (M3 human
0, M1 swing 0, every other lane muted on M4) at a known tempo, then:

    python tools/timing/onsets.py recording.wav --bpm 120

Each hit should fall on a sixteenth. The script finds the hits, fits the
sixteenth grid to them (the recording's own latency drops out), and prints
how far after its grid line each hit starts. Notes start on GUI ticks, so the
spread from earliest to latest is about one GUI tick, plus how often the
transport's position updates. `--selftest` checks the script on clicks it
makes with a known lateness.
"""
import argparse
import array
import math
import random
import sys
import tempfile
import wave
from pathlib import Path

HOP_S = 0.00025  # the envelope's resolution: a quarter of a millisecond
GAP_S = 0.045    # hits closer than this are one hit
BEFORE_S = (0.030, 0.003)  # a hit must jump over the envelope of this window before it
JUMP = 2.0       # by this factor


def read(path):
    """Mono samples in -1..1 and the sample rate, from 16, 24 or 32-bit PCM."""
    with wave.open(str(path), "rb") as w:
        rate, channels, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.readframes(w.getnframes())
    if width == 2:
        data, scale = array.array("h", frames), 32768.0
    elif width == 3:
        data = array.array("i", (int.from_bytes(frames[i:i + 3], "little", signed=True) for i in range(0, len(frames), 3)))
        scale = 8388608.0
    elif width == 4:
        data, scale = array.array("i", frames), 2147483648.0
    else:
        sys.exit(f"{path}: {8 * width}-bit samples; export 16, 24 or 32-bit PCM WAV")
    mono = [sum(data[i:i + channels]) / (channels * scale) for i in range(0, len(data), channels)]
    return mono, rate


def onsets(samples, rate):
    """Hit times in seconds: where the envelope jumps to twice what it was over
    the 30 ms before, and past a tenth of the loudest hit. A plain threshold
    re-arms at a low drum's zero crossings and counts one hit several times."""
    hop = max(1, int(rate * HOP_S))
    env = [max(abs(s) for s in samples[i:i + hop]) for i in range(0, len(samples) - hop, hop)]
    loud = sorted(env)[int(len(env) * 0.999)]
    rise = 0.1 * loud
    far, near = (round(s / HOP_S) for s in BEFORE_S)
    times, last = [], -1.0
    for i in range(far, len(env)):
        t = i * hop / rate
        if env[i] < rise or t - last < GAP_S:
            continue
        if env[i] >= JUMP * max(env[i - far:i - near]):
            times.append(t)
            last = t
    return times


def lateness(times, step):
    """Each hit's time after its grid line, in seconds, for a grid of `step`
    whose phase puts the earliest hits on their lines."""
    phases = sorted(t % step for t in times)
    # The hits cluster just after the grid line; the widest empty stretch of
    # the step, going round, ends where the cluster starts.
    gaps = [(phases[(i + 1) % len(phases)] - phases[i]) % step for i in range(len(phases))]
    widest = max(range(len(gaps)), key=gaps.__getitem__)
    start = phases[(widest + 1) % len(phases)]
    return [(t - start) % step for t in times]


def measure(times, bpm):
    """Lateness over the grid at `bpm`, nudged within 0.05% for the recorder's clock."""
    best = None
    for ppm in range(-500, 501, 10):
        step = 15.0 / bpm * (1 + ppm * 1e-6)
        late = sorted(lateness(times, step))
        spread = late[int(len(late) * 0.98)]
        if best is None or spread < best[0]:
            best = (spread, late, ppm)
    return best[1], best[2]


def report(late, ppm, bpm):
    ms = [1000 * x for x in late]
    n = len(ms)
    print(f"{n} hits at {bpm} bpm (recorder clock {ppm:+d} ppm)")
    print(f"late after the grid: mean {sum(ms) / n:.1f} ms, median {ms[n // 2]:.1f}, "
          f"95% {ms[int(n * 0.95)]:.1f}, max {ms[-1]:.1f}")
    print(f"spread (about one GUI tick): {ms[int(n * 0.98)] - ms[0]:.1f} ms; a sixteenth is {15000 / bpm:.0f} ms")
    top = max(ms[-1], 1)
    for lo in range(0, int(top) + 2, 2):
        count = sum(lo <= m < lo + 2 for m in ms)
        print(f"  {lo:3d}-{lo + 2:<3d} ms {'#' * round(60 * count / n)}")
    if ms[int(n * 0.95)] > 7500 / bpm:
        print("Hits spread over half a sixteenth: check the lane's human, the swing and that one lane plays.")


def selftest():
    """Drum-like hits on sixteenths at 120 bpm, each late by up to one 60 Hz
    tick, after an 11 ms recording latency: clicks, and low kicks ringing for
    a tenth of a second, which a plain threshold counts several times. The
    script must find each hit once, and a 16.7 ms spread."""
    rate, bpm, tick = 48000, 120, 1 / 60
    random.seed(1)
    step = 15.0 / bpm
    truth, samples = [], [0.0] * int(rate * 30)
    for k in range(int(29 / step)):
        if random.random() < 0.6:
            late = random.uniform(0, tick)
            truth.append(late)
            start = int((0.5 + 0.011 + k * step + late) * rate)
            level = random.uniform(0.6, 1.0)
            if k % 4 == 0:  # a kick: 55 Hz, ringing
                for j in range(int(0.11 * rate)):
                    samples[start + j] += level * math.sin(2 * math.pi * 55 * j / rate) * math.exp(-j / (0.04 * rate))
            else:
                for j in range(int(0.03 * rate)):
                    samples[start + j] += level * math.sin(j * 0.2) * math.exp(-j / (0.004 * rate))
    for i in range(len(samples)):
        samples[i] += random.gauss(0, 0.002)
    path = Path(tempfile.mkdtemp()) / "clicks.wav"
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(array.array("h", (int(max(-1, min(1, s)) * 32767) for s in samples)).tobytes())
    samples, rate = read(path)
    found = onsets(samples, rate)
    late, ppm = measure(found, bpm)
    report(late, ppm, bpm)
    spread = late[int(len(late) * 0.98)] - late[0]
    ok = len(found) == len(truth) and abs(spread - (max(truth) - min(truth))) < 0.0015
    print("selftest", "ok" if ok else f"FAIL: {len(found)} of {len(truth)} hits, spread {1000 * spread:.1f} ms")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wav", nargs="?")
    parser.add_argument("--bpm", type=float, default=120)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.wav:
        parser.error("give a WAV recording, or --selftest")
    samples, rate = read(args.wav)
    times = onsets(samples, rate)
    if len(times) < 20:
        sys.exit(f"found {len(times)} hits; record at least 20 seconds of one drum lane")
    late, ppm = measure(times, args.bpm)
    report(late, ppm, args.bpm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
