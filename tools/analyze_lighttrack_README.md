# analyze_lighttrack.py — auto show generator for LightShow

A small offline (PC-side) tool that listens to a song and writes the per-song data your
`LightShow` engine already uses — tempo (`beatMs`), a mood palette, and a section-by-section
cue timeline — as a ready-to-paste C++ snippet. The goal is to give any song a decent show
*without* hand-authoring it, so hand-crafted songs (Purple Rain, Africa) stay the showpieces
and everything else still gets bespoke-feeling choreography.

It does **not** replace the engine or the effects — it only feeds them. All the DSP happens
on the PC; the Tough does nothing new.

## What it produces

For an input song it writes, next to it (or into `--outdir`):

- **`SONG.PCM`** *(with `--emit-pcm`)* — 44100 Hz, 16-bit, stereo, headerless PCM: exactly the
  format `SpeakerAudio` plays. This is the file that goes on the SD as `/CH<N>.PCM`.
- **`SONG_show.h`** *(with `--show`)* — a pasteable `constexpr LightShow::Cue kSong[] = {…};`
  array plus a commented `// SHOW(<channel>, "name", kSong, endMs, beatMs, phaseMs,
  primaryHue, secondaryHue, accentHue, saturation)` line with every field pre-filled.
- `SONG.png` *(with `--preview`)* — a plot of the extracted features, just for eyeballing.
- `SONG.lt` *(always)* — a compact per-frame feature track (volume, hue, sway, kick, hat).
  Not needed for the Show workflow; it exists in case we later add Eric's Mode 6 "dance"
  look as one more `Effect`.

## What it auto-derives (the stuff you used to do by hand)

- **Tempo → `beatMs` / `beatPhaseMs`** — autocorrelation of an onset envelope over a
  75–170 BPM range, then a phase-aligned beat grid. Robust on dense material (it's what got
  Teardrop from a garbage 144 BPM down to its real ~78).
- **Mood → palette** — `primaryHue/secondaryHue/accentHue` + `saturation` from the song's
  mean spectral centroid (bass-heavy/dark → warm hues, bright/airy → cool) and overall energy.
- **Structure → cues** — the volume envelope is coarsened into ~1 s bins, split into
  low/mid/peak energy sections (min ~8 s each), and each section is assigned an effect from a
  tier pool (rotated for variety), ending with a `Finale` + `FadeOut`. Intensity scales with
  energy.

All of it is just constants in the generated snippet — tune anything to taste.

## Requirements

Any Python 3 with:

```
pip install numpy imageio-ffmpeg matplotlib
```

`imageio-ffmpeg` bundles its own ffmpeg binary (used to decode MP3/M4A/etc.), so nothing has
to be on your PATH. `matplotlib` is only for `--preview`. If you feed it a raw `.pcm` it skips
ffmpeg entirely.

## Running it

```
# one song: emit the SD PCM + the pasteable show header + a preview plot
python analyze_lighttrack.py "some song.mp3" --emit-pcm --show --preview --outdir out

# a whole folder at once
python analyze_lighttrack.py path/to/playlist --emit-pcm --show --outdir out
```

It prints one summary line per song: frame count, duration, detected BPM, centroid range,
kick/hat activity, and where it wrote the files.

## Adding a generated song to the firmware (example: channel 3)

1. **`Config.h`** — set `LED_STRIP_COUNT` to **120** (the strip is now 120× WS2812B, 5 V; the
   old value was 300). `FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>` is already correct.
2. Copy `song.PCM` onto the SD as **`/CH3.PCM`**.
3. Open `song_show.h`, paste the `kSong[]` array next to the other cue arrays in
   `LightShow.cpp`.
4. Add the generated `SHOW(...)` row to `kShows[]`, setting the channel to **3** (and
   optionally a label in `MUSIC_CHANNEL_NAMES[3]`).
5. Build, flash, turn the music knob to position 3, and play.

Because the cue `startMs`, `beatMs`, and `beatPhaseMs` are all in song-time from 0, they line
up with the engine's `songMs` exactly like the hand-authored shows do.

If you'd rather `#include` the generated headers (like `PurpleRainTiming.h`) instead of
pasting, say so and the generator can emit standalone headers (`#pragma once` + the include) —
you'd still add the one `SHOW(...)` row to `kShows[]`.

## Known v1 limitations

- **Effect selection is heuristic** (energy tier → a tasteful pool). Sensible, but not as
  bespoke as a hand-authored show. Planned refinement: fold in brightness/centroid and kick
  density so the effect matches the *character* of a section, not just its loudness.
- **Constant `beatMs`** — fine for steady tempos, matches how `Show` already works. For songs
  whose tempo wanders (live recordings), a drift-tracking beat map like `PurpleRainTiming.h`
  can be generated instead; not implemented yet.

Best first test: generate a show for a song that *isn't* hand-authored, drop it on a spare
channel, and see how the auto-choreography feels — then the heuristics get tuned against real
feedback.
