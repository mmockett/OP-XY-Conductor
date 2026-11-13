# OP-XY Conductor

A generative sequencer for the Teenage Engineering OP-XY, on the
[Open-XY SDK](https://github.com/etcetc000/open-xy). It sits on aux track 12
(CV) and plays eight lanes (drums, bass, chords, pads, arps and leads) live on
instrument tracks 1-8 while the sequencer runs.

![Conductor's four pages: M1 the song, M2 the lanes, M3 the selected lane, M4 the routes](docs/screens.png)

- **One song, eight parts.** Every lane follows one chord progression, set by
  the key, scale and progression, and one groove of kicks, backbeats and
  accents, so the parts fit together.
- **It evolves.** The song changes a little each four-bar phrase, as far as
  **evolve** allows. Freeze it when you like what you hear. Notes are a pure
  function of the song, the controls and the bar, so it replays the same until
  something changes.
- **It plays, it doesn't record.** Notes go to each track's machine as the
  sequencer plays; nothing is written into the tracks' steps. Stopped, the song
  waits at its start.

**Status:** v1.0.0. Conductor runs on an OP-XY on OS 1.2.1, in a firmware
built with the [Open-XY SDK](https://github.com/etcetc000/open-xy) at SDK 1.11
or later (see [Building](#building)).

## Using it

On track 12, Shift + M1 opens the engine list: choose Conductor, then press
play. M1-M4 open its four pages, named along the bottom of the screen. Press
any encoder to switch between a page's two rows.

Every page reads left to right in the same rows: each lane's role and style,
what it plays, and a wire to where it plays, under two columns: track (1-8)
and midi (channels 1-8). A track's box lights while a lane plays it. The midi
column stays dim until the SDK can send notes over MIDI (see Limits).

| Page | Encoders | Screen |
| --- | --- | --- |
| M1 conductor | key, scale, prog, dens / cmplx, hrate, swing, evolve | The phrase: its chords, the groove, and every lane's beats, a mark a beat sized by its notes; the phrase's letter, and how much the next will change |
| M2 lanes | lane, role, dens, oct / vel, vary, lock, gate | The bar playing, step by step, for every lane; drums at the height of their lowest voice |
| M3 lane | the selected lane's role controls: a bass's style, an arp's mode, a chord's voicing… | The selected lane alone: its voices down the left (kick to low tom) or its notes (named in the key), its bar across, over lines at the chord's notes, and one wire to its track |
| M4 route | lane, track, xpose, mute / low, high | Where each lane plays: every lane straight across to its wire and on to its track, the selected one bright; its range and transpose in the header |

M1's controls are saved, sequenced and lockable like any track's. Shift +
encoders 1-4 reroll, freeze, fill and vary: every lane on M1, the selected
lane on M2-M4; the header shows them on the row being edited. Freeze holds the
current phrase; turn it back to release it.

Lanes, roles, routes and the seed are saved with the project. The presets set
M1's controls: init (the defaults, C major), Pop (bright G major, busy),
Night (A minor, sparse, slow chords) and Jazz (F major, swung, rich chords).
The arp follows the song's density: it rests in a sparse song, lifts the last
two bars of each phrase in the middle, and plays every bar in a busy one.

## Building

Conductor needs the Open-XY SDK at version 1.11 or later, on its `staging`
branch: track control on aux track 12 (SDK 1.9), and with it notes played on
tracks 1-8, the machine's own encoder turns and sixteen words for its display
(SDK 1.11).

```sh
python ../open-xy/sdk.py test --machine .                          # records, preview and the host checks
python ../open-xy/sdk.py compile --machine . --out <new folder>    # the Blackfin compile
```

A firmware for the OP-XY comes from `sdk.py build`, with your own stock OS
([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md)).

## Development

```sh
python tools/artwork.py   # after changing a node's shape or slot
XY_FONT=/path/to/UniversNextPro-Light.ttf python tools/screens/screens.py --sdk ../open-xy   # docs/ screenshots
c++ -std=c++11 -O2 -I ../open-xy/sdk/include -I . tools/timing/timing.cpp -o /tmp/timing && /tmp/timing   # note timing on GUI ticks
python tools/timing/onsets.py recording.wav --bpm 120   # note timing on the device, from a recording
```

- `generator.h`: the generator, integer-only and within 32 bits.
- `specs.h`: the pages' controls and musical vocabulary.
- `control.cpp`: playback, encoders and saved data.
- `words.h`: what the control tells the display each tick, laid out by page.
- `display.cpp`: the pages.
- `tools/screens/`: renders the pages from the control and display code.
- `tools/timing/`: how late notes start on GUI ticks, on the host and from a
  recording of the device.

The screenshots need the OP-XY's screen font, which is never committed here.

## Limits

- Roles are set per lane: the OP-XY doesn't tell a machine which tracks hold
  drums.
- Notes start on the first GUI tick after their time, so each starts up to one
  tick late. On the host, at 60 ticks a second, that is 0-18 ms, 8 ms on
  average, never early, at any tempo; no tick starts more than 13 notes of the
  32 it may. A GUI stall of a second plays the notes it held back at once and
  drops past 32. On the device (120 bpm, recorded) notes started 0-17 ms
  after the grid, 5 ms at the median: one 60 Hz GUI tick, as on the host.
- M1-M4 arrive as key presses, at most one a GUI tick: two presses within
  one tick (about 17 ms) keep only the last.
- Lanes play instrument tracks 1-8 only. M4 shows MIDI channels 1-8 beside
  the tracks, but they can't be chosen yet: a machine's notes reach only
  tracks 1-8's own machines until the SDK can send them over MIDI.
- M3 shows the current bar, not the whole phrase.

## CPU

Conductor's engine renders silence: it plays the other tracks' machines. Its
own work (the song, its notes and its pages) runs once a GUI tick on the
OP-XY's GUI side, outside the audio block, so the audio block carries only its
silent voice.

## Licence

MIT. See [LICENSE](LICENSE).
