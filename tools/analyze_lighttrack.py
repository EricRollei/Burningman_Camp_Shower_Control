#!/usr/bin/env python3
"""
analyze_lighttrack.py  --  offline light-track generator for the Camp Shower music show.

Reads a song (raw .pcm as the Tough plays it, or any audio file which it will
convert with ffmpeg) and writes a compact ".lt" light-track: the per-frame Mode 6
features the Tough replays in sync with playback. All the DSP and beat timing happen
here, offline, so the Tough does zero FFT -- it just paints.

Audio format (must match SpeakerAudio on the Tough):
    44100 Hz, 16-bit signed little-endian, STEREO, headerless raw PCM.
    To make one by hand:
        ffmpeg -i song.mp3 -ar 44100 -ac 2 -f s16le -acodec pcm_s16le SONG.PCM

Usage:
    python analyze_lighttrack.py SONG.PCM                 # -> SONG.lt
    python analyze_lighttrack.py song.mp3 --emit-pcm      # -> SONG.PCM (for SD) + SONG.lt
    python analyze_lighttrack.py playlist/ --emit-pcm     # batch a folder
    python analyze_lighttrack.py song.mp3 --preview        # also write SONG.png to eyeball it

.lt binary format (little-endian), matched by the Tough reader:
    magic   : 4 bytes  "LTRK"
    version : uint8    = 1
    rate    : uint32   sample rate (44100)
    hop     : uint16   samples per frame (frame N covers sample N*hop)
    frames  : uint32   number of frames
    bpf     : uint8    bytes per frame = 5
    data    : frames * 5 bytes  [vol, hue, sway, kick, hat]  each 0..255
        vol  : brightness / volume            (0..255  ->  0..1)
        hue  : colour position (centroid)      (0..255  ->  0..1, Tough maps *260 deg)
        sway : dancer position                 (0..255  -> -1..+1)
        kick : kick envelope (hump punch)      (0..255  ->  0..1)
        hat  : hi-hat envelope (sparkle amount)(0..255  ->  0..1)
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import numpy as np

# ---- analysis parameters (44.1 kHz) ----------------------------------------
RATE        = 44100
WIN         = 2048          # FFT window
HOP         = 1024          # frame hop  -> 44100/1024 = 43.1 frames/sec
# onset detection bands (Hz): a narrow kick band rejects broadband bass rumble
KICK_LO, KICK_HI     = 50,   180
TREB_LO, TREB_HI     = 4000, 11000
# onset sensitivity (flux must exceed running average * this to count)
KICK_SENS   = 1.5
HAT_SENS    = 1.7
# minimum time between detected onsets (stops it firing every frame)
KICK_REFRACTORY_MS = 140     # caps kick rate ~430 BPM
HAT_REFRACTORY_MS  = 70
# envelope decay per frame (tuned for ~43 fps)
KICK_DECAY  = 0.82
HAT_DECAY   = 0.78
# sway
SWAY_BEATS  = 2.0           # beats per full left-right-left cycle
BPM_MIN, BPM_MAX = 75, 170  # fold tempo estimate into this range


def load_pcm(path):
    """Read headerless s16le stereo PCM -> float mono in [-1, 1]."""
    raw = np.fromfile(path, dtype='<i2')
    if raw.size == 0:
        raise ValueError(f"{path}: empty or unreadable PCM")
    if raw.size % 2:                       # stray sample -> drop it
        raw = raw[:-1]
    stereo = raw.reshape(-1, 2).astype(np.float32)
    mono = stereo.mean(axis=1) / 32768.0
    return mono


def find_ffmpeg():
    """Locate an ffmpeg binary: PATH first, then the imageio-ffmpeg bundle."""
    from shutil import which
    exe = which('ffmpeg')
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


def to_pcm_with_ffmpeg(src, dst):
    """Convert any audio file to the exact PCM the Tough plays (44.1k s16le stereo)."""
    exe = find_ffmpeg()
    if not exe:
        raise RuntimeError("ffmpeg not found -- install with:  <python> -m pip install imageio-ffmpeg")
    cmd = [exe, '-y', '-i', src, '-ar', str(RATE), '-ac', '2',
           '-f', 's16le', '-acodec', 'pcm_s16le', dst]
    subprocess.run(cmd, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def analyze(mono):
    """Run the frame-by-frame FFT analysis -> raw feature arrays."""
    window = np.hanning(WIN).astype(np.float32)
    freqs = np.fft.rfftfreq(WIN, 1.0 / RATE)
    kick_m = (freqs >= KICK_LO) & (freqs < KICK_HI)
    treb_m = (freqs >= TREB_LO) & (freqs < TREB_HI)

    n_frames = 1 + max(0, (len(mono) - WIN) // HOP)
    vol      = np.zeros(n_frames, np.float32)
    centroid = np.zeros(n_frames, np.float32)   # in Hz
    kick_e   = np.zeros(n_frames, np.float32)
    treb_e   = np.zeros(n_frames, np.float32)

    for i in range(n_frames):
        seg = mono[i * HOP: i * HOP + WIN]
        if len(seg) < WIN:
            seg = np.pad(seg, (0, WIN - len(seg)))
        vol[i] = np.sqrt(np.mean(seg * seg))            # RMS loudness
        mag = np.abs(np.fft.rfft(seg * window))
        s = mag.sum()
        centroid[i] = float((freqs * mag).sum() / s) if s > 1e-6 else 0.0
        kick_e[i] = mag[kick_m].sum()
        treb_e[i] = mag[treb_m].sum()

    return vol, centroid, kick_e, treb_e


def half_wave_flux(band):
    """Positive frame-to-frame change (rectified spectral flux) of a band's energy."""
    d = np.diff(band, prepend=band[:1])
    d[d < 0] = 0.0
    return d


def pick_peaks(flux, thresh_pct, refractory_frames):
    """Sparse onsets: local maxima above a high percentile, spaced by a refractory gap."""
    onsets = np.zeros(len(flux), bool)
    thr = np.percentile(flux, thresh_pct)
    last = -10 ** 9
    for i in range(1, len(flux) - 1):
        if (flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]
                and (i - last) >= refractory_frames):
            onsets[i] = True
            last = i
    return onsets


def make_envelope(onsets, decay):
    env = np.zeros(len(onsets), np.float32)
    e = 0.0
    for i, on in enumerate(onsets):
        if on:
            e = 1.0
        env[i] = e
        e *= decay
    return env


def estimate_tempo(env):
    """Autocorrelation of the onset envelope -> dominant beat period (frames), BPM."""
    ft = HOP / RATE
    e = env - env.mean()
    ac = np.correlate(e, e, mode='full')[len(e) - 1:]
    lag_lo = max(1, int(round(60.0 / BPM_MAX / ft)))
    lag_hi = min(int(round(60.0 / BPM_MIN / ft)), len(ac) - 1)
    if lag_hi <= lag_lo:
        lag = int(round(60.0 / 110.0 / ft))
        return lag, 60.0 / (lag * ft)
    lag = lag_lo + int(np.argmax(ac[lag_lo:lag_hi + 1]))
    return lag, 60.0 / (lag * ft)


def beat_phase_offset(env, period):
    """Grid offset (0..period-1) whose beats best line up with onset energy."""
    n = len(env)
    best_off, best = 0, -1.0
    for off in range(period):
        s = float(env[np.arange(off, n, period)].sum())
        if s > best:
            best, best_off = s, off
    return best_off


def build_lighttrack(mono):
    vol, centroid, kick_e, treb_e = analyze(mono)

    # --- volume -> brightness: normalise to the song's 95th percentile ---
    vpk = np.percentile(vol, 95) or 1.0
    vol_n = np.clip(vol / vpk, 0.0, 1.0)

    # --- centroid -> hue: auto-calibrate to this song's p5..p95 (full wheel) ---
    lo = np.percentile(centroid, 5)
    hi = np.percentile(centroid, 95)
    if hi - lo < 1.0:
        hi = lo + 1.0
    hue = np.clip((centroid - lo) / (hi - lo), 0.0, 1.0)

    # --- tempo via autocorrelation of an onset envelope (robust on dense music) ---
    kf = half_wave_flux(kick_e)
    tf = half_wave_flux(treb_e)
    env = kf / (kf.max() + 1e-9) + 0.3 * (tf / (tf.max() + 1e-9))
    period, bpm = estimate_tempo(env)
    offset = beat_phase_offset(env, period)
    n = len(vol)

    # --- steady beat grid -> sway ---
    beat_phase = (np.arange(n) - offset) / period          # elapsed beats
    sway = np.sin((beat_phase / SWAY_BEATS) * 2.0 * np.pi)
    sway_b = sway * 0.5 + 0.5

    # --- kick envelope: a pulse on each grid beat, amplitude from bass energy there ---
    bass_n = np.clip(kick_e / (np.percentile(kick_e, 95) + 1e-9), 0.0, 1.0)
    beats = set(int(b) for b in range(offset % period, n, period))
    kick = np.zeros(n, np.float32)
    e = 0.0
    for f in range(n):
        if f in beats:
            e = max(e, float(bass_n[f]))
        kick[f] = e
        e *= KICK_DECAY

    # --- hat envelope: sparse, selective treble transients ---
    hr = max(1, round(HAT_REFRACTORY_MS / 1000 * RATE / HOP))
    hat = make_envelope(pick_peaks(tf, 92, hr), HAT_DECAY)

    feats = {
        'vol':  np.clip(vol_n * 255, 0, 255).astype(np.uint8),
        'hue':  np.clip(hue   * 255, 0, 255).astype(np.uint8),
        'sway': np.clip(sway_b* 255, 0, 255).astype(np.uint8),
        'kick': np.clip(kick  * 255, 0, 255).astype(np.uint8),
        'hat':  np.clip(hat   * 255, 0, 255).astype(np.uint8),
    }
    stats = {'bpm': bpm, 'centroid_lo': lo, 'centroid_hi': hi,
             'frames': len(vol), 'dur': len(vol) * HOP / RATE,
             'period': int(period), 'offset': int(offset),
             'kick_pct': float(100 * (feats['kick'] > 51).mean()),
             'hat_pct':  float(100 * (feats['hat']  > 51).mean()),
             'vol_mean': float(feats['vol'].mean() / 255.0)}
    return feats, stats


def write_lt(path, feats):
    n = len(feats['vol'])
    data = np.stack([feats['vol'], feats['hue'], feats['sway'],
                     feats['kick'], feats['hat']], axis=1).tobytes()
    with open(path, 'wb') as f:
        f.write(b'LTRK')
        f.write(struct.pack('<BIHIB', 1, RATE, HOP, n, 5))
        f.write(data)


def preview_png(path, feats, stats):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        return None
    t = np.arange(stats['frames']) * HOP / RATE
    fig, ax = plt.subplots(5, 1, figsize=(11, 7), sharex=True)
    for a, key, lbl, c in zip(ax, ['vol', 'hue', 'sway', 'kick', 'hat'],
                              ['volume', 'hue', 'sway', 'kick', 'hat'],
                              ['k', 'purple', 'teal', 'crimson', 'orange']):
        a.plot(t, feats[key] / 255.0, c, lw=0.6)
        a.set_ylabel(lbl); a.set_ylim(-0.05, 1.05)
    ax[-1].set_xlabel('seconds')
    ax[0].set_title(f"{os.path.basename(path)}  |  {stats['dur']:.0f}s  "
                    f"~{stats['bpm']:.0f} BPM")
    fig.tight_layout(); fig.savefig(path, dpi=90); plt.close(fig)
    return path


# ------------------------------------------------------------------ #
#  Show generator: auto-emit a Colton-style Show{} + Cue[] per song    #
#  Effect names MUST match LightShow::Effect in LightShow.h exactly.   #
# ------------------------------------------------------------------ #
EFFECTS_LOW  = ['Glow', 'Wave', 'Sunrise', 'BeatBreathing']
EFFECTS_MED  = ['FrequencyWaves', 'RhythmRipples', 'BeatPulse', 'SpectrumBands']
EFFECTS_HIGH = ['PurpleStorm', 'BeatChase', 'DiscoBlocks', 'MirrorBall', 'KickChase']
TIER_NAMES   = ['low', 'mid', 'peak']


def _ident(base):
    """Filename -> C++ identifier:  k + CamelCase alnum."""
    parts = ''.join(c if c.isalnum() else ' ' for c in base).split()
    name = ''.join(p[:1].upper() + p[1:] for p in parts) or 'Song'
    if name[0].isdigit():
        name = 'S' + name
    return 'k' + name


def _hue_from_centroid(cent_norm):
    """0..1 centroid -> FastLED hue: bassy/dark = warm (16), bright = cool (190)."""
    return int(round(16 + max(0.0, min(1.0, cent_norm)) * (190 - 16))) & 0xFF


def derive_palette(feats):
    """Auto mood palette (primary/secondary/accent hue + saturation) from the audio."""
    cent_norm = float(feats['hue'].mean()) / 255.0
    energy = float(feats['vol'].mean()) / 255.0
    base = _hue_from_centroid(cent_norm)
    return base, (base + 24) & 0xFF, (base + 210) & 0xFF, min(255, int(round(215 + energy * 40)))


def detect_sections(feats, min_seconds=8.0):
    """Coarsen the volume envelope into contiguous low/mid/peak energy sections."""
    fps = RATE / HOP
    binlen = max(1, int(round(fps)))                     # ~1 s bins
    v = feats['vol'].astype(np.float32) / 255.0
    nb = max(1, len(v) // binlen)
    vb = v[:nb * binlen].reshape(nb, binlen).mean(axis=1)
    lo, hi = np.percentile(vb, 33), np.percentile(vb, 66)
    level = np.where(vb > hi, 2, np.where(vb > lo, 1, 0))
    runs, s = [], 0
    for i in range(1, nb + 1):
        if i == nb or level[i] != level[s]:
            runs.append([s, i, int(level[s])])
            s = i
    min_bins = max(1, int(round(min_seconds)))
    merged = []
    for r in runs:
        if merged and (r[1] - r[0]) < min_bins:
            merged[-1][1] = r[1]                          # fold short run into previous
        else:
            merged.append(r)
    return merged, binlen / fps                          # sections, seconds-per-bin (~1.0)


def generate_show(base, feats, stats):
    """Build a ready-to-paste Cue[] + SHOW(...) row for Colton's LightShow engine."""
    prim, sec, acc, sat = derive_palette(feats)
    beat_ms = max(1, int(round(stats['period'] * HOP / RATE * 1000)))
    phase_ms = int(round(stats['offset'] * HOP / RATE * 1000)) % beat_ms
    end_ms = int(round(stats['dur'] * 1000))
    sections, spb = detect_sections(feats)

    tiers, counters, cues = [EFFECTS_LOW, EFFECTS_MED, EFFECTS_HIGH], [0, 0, 0], []
    for k, (sb, _eb, lvl) in enumerate(sections):
        pool = tiers[lvl]
        eff = pool[counters[lvl] % len(pool)]
        counters[lvl] += 1
        intensity = int(min(146, 52 + lvl * 30 + round(stats['vol_mean'] * 20)))
        cues.append((int(round(sb * spb * 1000)), eff, intensity, f"auto {TIER_NAMES[lvl]} {k + 1}"))
    if end_ms > 8000:
        cues.append((max(0, end_ms - 6000), 'Finale', 130, "auto finale"))
    cues.append((max(0, end_ms - 2500), 'FadeOut', 96, "auto fade"))

    ident = _ident(base)
    lines = [f"constexpr LightShow::Cue {ident}[] = {{"]
    for sms, eff, inten, nm in cues:
        lines.append(f'    {{{sms}, LightShow::Effect::{eff}, {inten}, "{nm}"}},')
    lines.append("};")
    show_line = (f'// SHOW(<channel>, "{base}", {ident}, {end_ms}, {beat_ms}, '
                 f'{phase_ms}, {prim}, {sec}, {acc}, {sat})')
    header = ("// Auto-generated by analyze_lighttrack.py -- paste the Cue[] near the other\n"
              "// cue arrays in LightShow.cpp, then add the SHOW(...) row to kShows[]\n"
              "// (assign a free channel). Palette + tempo are auto-derived; tune to taste.\n\n"
              + "\n".join(lines) + "\n\n" + show_line + "\n")
    return header, beat_ms, len(cues)


def process(src, emit_pcm=False, preview=False, outdir=None, show=False):
    base = os.path.splitext(os.path.basename(src))[0]
    outdir = outdir or os.path.dirname(os.path.abspath(src))
    os.makedirs(outdir, exist_ok=True)
    is_pcm = src.lower().endswith('.pcm')

    tmp = None
    if is_pcm:
        pcm_path = src
    else:
        pcm_path = os.path.join(outdir, base + '.PCM') if emit_pcm \
            else (tmp := tempfile.NamedTemporaryFile(suffix='.pcm', delete=False).name)
        to_pcm_with_ffmpeg(src, pcm_path)

    mono = load_pcm(pcm_path)
    feats, stats = build_lighttrack(mono)
    lt_path = os.path.join(outdir, base + '.lt')
    write_lt(lt_path, feats)

    msg = (f"{base}: {stats['frames']} frames, {stats['dur']:.0f}s, "
           f"~{stats['bpm']:.0f} BPM, centroid {stats['centroid_lo']:.0f}-"
           f"{stats['centroid_hi']:.0f} Hz, kicks {stats['kick_pct']:.0f}%, "
           f"hats {stats['hat_pct']:.0f}%, vol_mean {stats['vol_mean']:.2f} -> {lt_path} "
           f"({os.path.getsize(lt_path)//1024} KB)")
    if preview:
        p = preview_png(os.path.join(outdir, base + '.png'), feats, stats)
        if p:
            msg += f"  [preview {os.path.basename(p)}]"
    if show:
        hdr, beat_ms, ncues = generate_show(base, feats, stats)
        show_path = os.path.join(outdir, base + '_show.h')
        with open(show_path, 'w') as f:
            f.write(hdr)
        msg += f"  [show {os.path.basename(show_path)}: {ncues} cues, beat {beat_ms}ms]"
    if tmp and os.path.exists(tmp):
        os.unlink(tmp)
    print(msg)


def main():
    ap = argparse.ArgumentParser(description="Generate .lt light-tracks for the shower music show.")
    ap.add_argument('input', help='a .pcm/.mp3/.wav file, or a folder to batch')
    ap.add_argument('--emit-pcm', action='store_true',
                    help='also write SONG.PCM (the file that goes on the SD card)')
    ap.add_argument('--preview', action='store_true', help='write a SONG.png feature plot')
    ap.add_argument('--show', action='store_true',
                    help='write SONG_show.h: an auto Cue[] + SHOW() row for LightShow.cpp')
    ap.add_argument('--outdir', default=None, help='output directory (default: alongside input)')
    args = ap.parse_args()

    if os.path.isdir(args.input):
        exts = ('.pcm', '.mp3', '.wav', '.flac', '.m4a', '.aac', '.ogg')
        files = sorted(f for f in os.listdir(args.input) if f.lower().endswith(exts))
        if not files:
            print("no audio files found in folder"); sys.exit(1)
        for f in files:
            process(os.path.join(args.input, f), args.emit_pcm, args.preview, args.outdir, args.show)
    else:
        process(args.input, args.emit_pcm, args.preview, args.outdir, args.show)


if __name__ == '__main__':
    main()
