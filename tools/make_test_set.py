"""Generate a set of music-like test tracks with known tempo and key.

    python tools/make_test_set.py <output folder>

Writes 44.1 kHz stereo WAV files plus truth.json. Each track has drums, a
bass line on the chord roots, chords built from harmonic-rich tones and a
melody that resolves to the tonic, so the key is unambiguous to a listener.
Two deliberately hard cases are included: piano only (no drums) and drums
only (no key).
"""

import json
import pathlib
import sys

import numpy as np

RATE = 44100
SECONDS = 45.0
RNG = np.random.default_rng(1234)

NOTE_NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6,
              "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]  # natural minor; dominant chords add the leading tone


def hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


class Track:
    def __init__(self):
        self.left = np.zeros(int(RATE * SECONDS))
        self.right = np.zeros(int(RATE * SECONDS))

    def add(self, start, signal, gain=1.0, pan=0.0):
        i = int(start * RATE)
        if i >= len(self.left):
            return
        n = min(len(signal), len(self.left) - i)
        self.left[i:i + n] += signal[:n] * gain * (1.0 - pan)
        self.right[i:i + n] += signal[:n] * gain * (1.0 + pan)


def envelope(n, attack=0.005, decay=0.3, sustain=0.6, release=0.1):
    t = np.arange(n) / RATE
    length = n / RATE
    env = np.where(t < attack, t / attack, sustain + (1 - sustain) * np.exp(-(t - attack) / max(decay, 1e-3)))
    tail = t > length - release
    env[tail] *= np.clip((length - t[tail]) / release, 0, 1)
    return env


def tone(midi, seconds, harmonics=6, brightness=1.2, **env):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    f = hz(midi)
    s = np.zeros(n)
    for h in range(1, harmonics + 1):
        if f * h < RATE / 2:
            s += np.sin(2 * np.pi * f * h * t + RNG.uniform(0, 6.28)) / (h ** brightness)
    return s * envelope(n, **env)


def kick(level=1.0):
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    phase = 2 * np.pi * np.cumsum(45 + 110 * np.exp(-t * 28)) / RATE
    return np.sin(phase) * np.exp(-t * 9) * level


def snare():
    n = int(0.25 * RATE)
    t = np.arange(n) / RATE
    noise = RNG.uniform(-1, 1, n)
    return (0.7 * noise * np.exp(-t * 18) + 0.4 * np.sin(2 * np.pi * 185 * t) * np.exp(-t * 25))


def hat(open_hat=False):
    n = int((0.25 if open_hat else 0.06) * RATE)
    t = np.arange(n) / RATE
    noise = np.diff(RNG.uniform(-1, 1, n + 1))  # crude high-pass
    return noise * np.exp(-t * (12 if open_hat else 70)) * 0.5


def chord_notes(root_pc, quality, octave=4):
    root = 12 * (octave + 1) + root_pc
    third = 3 if quality in ("m", "m7") else 4
    notes = [root, root + third, root + 7]
    if quality == "7":
        notes.append(root + 10)
    if quality == "maj7":
        notes.append(root + 11)
    if quality == "m7":
        notes.append(root + 10)
    return notes


def progression(tonic_pc, minor, kind):
    """Returns [(root pitch class, quality)] per bar."""
    if minor:
        degrees = {"i-VI-VII-i": [(0, "m"), (8, ""), (10, ""), (0, "m")],
                   "i-iv-V-i": [(0, "m"), (5, "m"), (7, ""), (0, "m")],
                   "i-iv-VII-i": [(0, "m"), (5, "m"), (10, ""), (0, "m")]}[kind]
    else:
        degrees = {"I-V-vi-IV": [(0, ""), (7, ""), (9, "m"), (5, "")],
                   "I-IV-V-I": [(0, ""), (5, ""), (7, ""), (0, "")],
                   "I-vi-ii-V": [(0, "maj7"), (9, "m7"), (2, "m7"), (7, "7")],
                   "I-IV": [(0, "7"), (5, "7"), (0, "7"), (5, "7")]}[kind]
    return [((tonic_pc + d) % 12, q) for d, q in degrees]


def make(spec):
    tr = Track()
    bpm = spec["bpm"]
    beat = 60.0 / bpm
    beats_per_bar = spec.get("meter", 4)
    bar = beat * beats_per_bar
    swing = spec.get("swing", 0.0)
    tonic_pc = NOTE_NAMES[spec["tonic"]]
    minor = spec["mode"] == "minor"
    prog = progression(tonic_pc, minor, spec["progression"]) if spec.get("key", True) else []
    scale = MINOR if minor else MAJOR
    pattern = spec.get("drums", "four")

    bars = int(SECONDS / bar) + 1
    for b in range(bars):
        t0 = b * bar
        # ---- drums
        for step in range(beats_per_bar * 4):  # 16ths
            t = t0 + step * beat / 4
            if step % 2 == 1:
                t += swing * beat / 4
            q = step % 16
            if pattern == "none":
                break
            if pattern == "four":
                if q % 4 == 0: tr.add(t, kick())
                if q in (4, 12): tr.add(t, snare(), 0.5)
                if q % 4 == 2: tr.add(t, hat(True), 0.35, 0.2)
                if q % 2 == 0: tr.add(t, hat(), 0.25, -0.2)
            elif pattern == "boombap":
                if q in (0, 7, 10): tr.add(t, kick())
                if q in (4, 12): tr.add(t, snare(), 0.6)
                if q % 2 == 0: tr.add(t, hat(), 0.3, 0.1)
            elif pattern == "trap":  # half-time: snare on beat 3
                if q in (0, 11): tr.add(t, kick())
                if q == 8: tr.add(t, snare(), 0.6)
                tr.add(t, hat(), 0.18 if q % 2 else 0.3, 0.1)
            elif pattern == "dnb":
                if q in (0, 10): tr.add(t, kick())
                if q in (4, 12): tr.add(t, snare(), 0.7)
                if q % 2 == 0: tr.add(t, hat(), 0.25, 0.15)
            elif pattern == "dembow":
                if q in (0, 4, 8, 12): tr.add(t, kick(0.9))
                if q in (3, 6, 11, 14): tr.add(t, snare(), 0.45)
                if q % 2 == 0: tr.add(t, hat(), 0.2)
            elif pattern == "funk":
                if q in (0, 3, 10): tr.add(t, kick())
                if q in (4, 12): tr.add(t, snare(), 0.6)
                tr.add(t, hat(), 0.22, 0.2)
            elif pattern == "afro":
                if q in (0, 6, 8, 14): tr.add(t, kick(0.9))
                if q in (4, 12): tr.add(t, snare(), 0.4)
                if q in (2, 5, 7, 10, 13, 15): tr.add(t, hat(), 0.25, -0.15)
            elif pattern == "ballad":
                if q in (0, 8): tr.add(t, kick(0.7))
                if q in (4, 12): tr.add(t, snare(), 0.35)
                if q % 4 == 0: tr.add(t, hat(), 0.15)
            elif pattern == "waltz":
                if q >= beats_per_bar * 4: break
                if q == 0: tr.add(t, kick())
                if q in (4, 8): tr.add(t, snare(), 0.3)
                if q % 2 == 0: tr.add(t, hat(), 0.2)

        if not prog:
            continue

        # ---- harmony
        root_pc, quality = prog[b % len(prog)]
        notes = chord_notes(root_pc, quality, spec.get("chord_octave", 4))
        if spec.get("piano", False):
            # arpeggiated piano: one chord note per 8th
            for i in range(beats_per_bar * 2):
                note = notes[i % len(notes)] + (12 if i % 6 >= 3 else 0)
                tr.add(t0 + i * beat / 2, tone(note, beat * 0.9, 8, 1.3, decay=0.25, sustain=0.2), 0.12, 0.1)
        else:
            for note in notes:
                tr.add(t0, tone(note, bar * 0.98, 6, 1.4, attack=0.03, decay=0.6, sustain=0.5, release=0.2), 0.05, 0.25)

        # bass on the root, rhythm per style
        bass_note = 12 * 2 + root_pc + (12 if root_pc < 4 else 0)
        for k in range(beats_per_bar):
            if spec["drums"] in ("four", "dnb", "trap") or k % 2 == 0:
                tr.add(t0 + k * beat, tone(bass_note, beat * 0.8, 4, 1.8, decay=0.2, sustain=0.5), 0.22)

        # melody: scale tones, landing on the tonic at the end of every second bar
        if spec.get("melody", True):
            for k in range(beats_per_bar):
                if b % 2 == 1 and k == beats_per_bar - 1:
                    degree = 0
                else:
                    degree = int(RNG.choice([0, 2, 4, 0, 1, 3, 4, 2]))
                midi = 12 * 5 + tonic_pc + scale[degree % 7]
                tr.add(t0 + k * beat, tone(midi, beat * 0.85, 5, 1.1, decay=0.15, sustain=0.4), 0.07, -0.3)

    mix = np.stack([tr.left, tr.right])
    mix *= 0.7 / max(1e-9, np.max(np.abs(mix)))
    return mix


SPECS = [
    {"name": "01_house_124_Am", "bpm": 124, "tonic": "A", "mode": "minor", "progression": "i-VI-VII-i", "drums": "four"},
    {"name": "02_hiphop_90_Fm", "bpm": 90, "tonic": "F", "mode": "minor", "progression": "i-iv-VII-i", "drums": "boombap", "swing": 0.25},
    {"name": "03_trap_140_C#m", "bpm": 140, "tonic": "C#", "mode": "minor", "progression": "i-VI-VII-i", "drums": "trap"},
    {"name": "04_dnb_174_Gm", "bpm": 174, "tonic": "G", "mode": "minor", "progression": "i-iv-V-i", "drums": "dnb"},
    {"name": "05_pop_100_C", "bpm": 100, "tonic": "C", "mode": "major", "progression": "I-V-vi-IV", "drums": "four"},
    {"name": "06_techno_130_F#m", "bpm": 130, "tonic": "F#", "mode": "minor", "progression": "i-iv-V-i", "drums": "four"},
    {"name": "07_ballad_72_Eb", "bpm": 72, "tonic": "Eb", "mode": "major", "progression": "I-IV-V-I", "drums": "ballad"},
    {"name": "08_reggaeton_95_Dm", "bpm": 95, "tonic": "D", "mode": "minor", "progression": "i-VI-VII-i", "drums": "dembow"},
    {"name": "09_funk_110_E", "bpm": 110, "tonic": "E", "mode": "major", "progression": "I-IV", "drums": "funk"},
    {"name": "10_afrobeat_104_Bbm", "bpm": 104, "tonic": "Bb", "mode": "minor", "progression": "i-iv-VII-i", "drums": "afro"},
    {"name": "11_lofi_85_Ab", "bpm": 85, "tonic": "Ab", "mode": "major", "progression": "I-vi-ii-V", "drums": "boombap", "swing": 0.33},
    {"name": "12_edm_128_Bm", "bpm": 128, "tonic": "B", "mode": "minor", "progression": "i-VI-VII-i", "drums": "four"},
    {"name": "13_waltz_90_G", "bpm": 90, "tonic": "G", "mode": "major", "progression": "I-IV-V-I", "drums": "waltz", "meter": 3},
    {"name": "14_piano_only_120_D", "bpm": 120, "tonic": "D", "mode": "major", "progression": "I-IV-V-I", "drums": "none", "piano": True, "melody": False},
    {"name": "15_drums_only_120", "bpm": 120, "tonic": "C", "mode": "major", "progression": "I-IV-V-I", "drums": "four", "key": False},
]


def write_wav(path, mix):
    import wave
    pcm = (np.clip(mix.T, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "testset")
    out.mkdir(parents=True, exist_ok=True)
    truth = []
    for spec in SPECS:
        write_wav(out / (spec["name"] + ".wav"), make(spec))
        has_key = spec.get("key", True)
        truth.append({"file": spec["name"] + ".wav", "bpm": spec["bpm"],
                      "key": f'{spec["tonic"]} {spec["mode"]}' if has_key else None})
        print("wrote", spec["name"])
    (out / "truth.json").write_text(json.dumps(truth, indent=2))


if __name__ == "__main__":
    main()
