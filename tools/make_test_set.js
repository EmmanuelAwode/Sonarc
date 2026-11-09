// Music-like test tracks with known tempo and key, generated deterministically
// (seeded random numbers), so the exact same WAV bytes can be produced in Node
// (for Sonarc) and inside a web page (for browser-based analysers like Tunebat).
//
//   node tools/make_test_set.js <output folder>
//
// In a browser: SonarcTestSet.generate(spec) -> { name, bpm, key, wav: Uint8Array }

(function (root) {
    "use strict";

    const RATE = 44100;
    const SECONDS = 30;

    const NOTE = { C: 0, "C#": 1, Db: 1, D: 2, "D#": 3, Eb: 3, E: 4, F: 5, "F#": 6, Gb: 6, G: 7, "G#": 8, Ab: 8, A: 9, "A#": 10, Bb: 10, B: 11 };
    const MAJOR = [0, 2, 4, 5, 7, 9, 11];
    const MINOR = [0, 2, 3, 5, 7, 8, 10];

    function mulberry32(seed) {
        let a = seed >>> 0;
        return function () {
            a = (a + 0x6d2b79f5) >>> 0;
            let t = Math.imul(a ^ (a >>> 15), 1 | a);
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        };
    }

    const hz = (midi) => 440 * Math.pow(2, (midi - 69) / 12);

    function makeTrack() {
        const n = Math.floor(RATE * SECONDS);
        const left = new Float64Array(n), right = new Float64Array(n);
        return {
            left, right,
            add(start, signal, gain = 1, pan = 0) {
                const i = Math.floor(start * RATE);
                if (i >= n) return;
                const m = Math.min(signal.length, n - i);
                for (let k = 0; k < m; ++k) {
                    left[i + k] += signal[k] * gain * (1 - pan);
                    right[i + k] += signal[k] * gain * (1 + pan);
                }
            },
        };
    }

    function envelope(n, { attack = 0.005, decay = 0.3, sustain = 0.6, release = 0.1 } = {}) {
        const env = new Float64Array(n);
        const length = n / RATE;
        for (let i = 0; i < n; ++i) {
            const t = i / RATE;
            let e = t < attack ? t / attack : sustain + (1 - sustain) * Math.exp(-(t - attack) / Math.max(decay, 1e-3));
            if (t > length - release) e *= Math.min(1, Math.max(0, (length - t) / release));
            env[i] = e;
        }
        return env;
    }

    function tone(rng, midi, seconds, harmonics, brightness, env) {
        const n = Math.floor(seconds * RATE);
        const out = new Float64Array(n);
        const f = hz(midi);
        for (let h = 1; h <= harmonics; ++h) {
            if (f * h >= RATE / 2) break;
            const phase = rng() * 6.28, w = 2 * Math.PI * f * h / RATE, a = 1 / Math.pow(h, brightness);
            for (let i = 0; i < n; ++i) out[i] += Math.sin(w * i + phase) * a;
        }
        const e = envelope(n, env);
        for (let i = 0; i < n; ++i) out[i] *= e[i];
        return out;
    }

    function kick(level = 1) {
        const n = Math.floor(0.35 * RATE);
        const out = new Float64Array(n);
        let phase = 0;
        for (let i = 0; i < n; ++i) {
            const t = i / RATE;
            phase += 2 * Math.PI * (45 + 110 * Math.exp(-t * 28)) / RATE;
            out[i] = Math.sin(phase) * Math.exp(-t * 9) * level;
        }
        return out;
    }

    function snare(rng) {
        const n = Math.floor(0.25 * RATE);
        const out = new Float64Array(n);
        for (let i = 0; i < n; ++i) {
            const t = i / RATE;
            out[i] = 0.7 * (rng() * 2 - 1) * Math.exp(-t * 18) + 0.4 * Math.sin(2 * Math.PI * 185 * t) * Math.exp(-t * 25);
        }
        return out;
    }

    function hat(rng, open = false) {
        const n = Math.floor((open ? 0.25 : 0.06) * RATE);
        const out = new Float64Array(n);
        let previous = rng() * 2 - 1;
        for (let i = 0; i < n; ++i) {
            const x = rng() * 2 - 1;
            out[i] = (x - previous) * Math.exp(-(i / RATE) * (open ? 12 : 70)) * 0.5; // crude high-pass
            previous = x;
        }
        return out;
    }

    function chordNotes(rootPc, quality, octave = 4) {
        const root = 12 * (octave + 1) + rootPc;
        const third = quality === "m" || quality === "m7" ? 3 : 4;
        const notes = [root, root + third, root + 7];
        if (quality === "7" || quality === "m7") notes.push(root + 10);
        if (quality === "maj7") notes.push(root + 11);
        return notes;
    }

    function progression(tonicPc, minor, kind) {
        const table = minor
            ? { "i-VI-VII-i": [[0, "m"], [8, ""], [10, ""], [0, "m"]],
                "i-iv-V-i": [[0, "m"], [5, "m"], [7, ""], [0, "m"]],
                "i-iv-VII-i": [[0, "m"], [5, "m"], [10, ""], [0, "m"]] }
            : { "I-V-vi-IV": [[0, ""], [7, ""], [9, "m"], [5, ""]],
                "I-IV-V-I": [[0, ""], [5, ""], [7, ""], [0, ""]],
                "I-vi-ii-V": [[0, "maj7"], [9, "m7"], [2, "m7"], [7, "7"]],
                "I-IV": [[0, "7"], [5, "7"], [0, "7"], [5, "7"]] };
        return table[kind].map(([d, q]) => [(tonicPc + d) % 12, q]);
    }

    function render(spec, seed) {
        const rng = mulberry32(seed);
        const tr = makeTrack();
        const beat = 60 / spec.bpm;
        const perBar = spec.meter || 4;
        const bar = beat * perBar;
        const swing = spec.swing || 0;
        const tonicPc = NOTE[spec.tonic];
        const minor = spec.mode === "minor";
        const prog = spec.key === false ? [] : progression(tonicPc, minor, spec.progression);
        const scale = minor ? MINOR : MAJOR;
        const p = spec.drums;

        const bars = Math.floor(SECONDS / bar) + 1;
        for (let b = 0; b < bars; ++b) {
            const t0 = b * bar;
            for (let step = 0; step < perBar * 4 && p !== "none"; ++step) {
                let t = t0 + step * beat / 4;
                if (step % 2 === 1) t += swing * beat / 4;
                const q = step % 16;
                if (p === "four") {
                    if (q % 4 === 0) tr.add(t, kick());
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.5);
                    if (q % 4 === 2) tr.add(t, hat(rng, true), 0.35, 0.2);
                    if (q % 2 === 0) tr.add(t, hat(rng), 0.25, -0.2);
                } else if (p === "boombap") {
                    if ([0, 7, 10].includes(q)) tr.add(t, kick());
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.6);
                    if (q % 2 === 0) tr.add(t, hat(rng), 0.3, 0.1);
                } else if (p === "trap") {
                    if (q === 0 || q === 11) tr.add(t, kick());
                    if (q === 8) tr.add(t, snare(rng), 0.6);
                    tr.add(t, hat(rng), q % 2 ? 0.18 : 0.3, 0.1);
                } else if (p === "dnb") {
                    if (q === 0 || q === 10) tr.add(t, kick());
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.7);
                    if (q % 2 === 0) tr.add(t, hat(rng), 0.25, 0.15);
                } else if (p === "dembow") {
                    if (q % 4 === 0) tr.add(t, kick(0.9));
                    if ([3, 6, 11, 14].includes(q)) tr.add(t, snare(rng), 0.45);
                    if (q % 2 === 0) tr.add(t, hat(rng), 0.2);
                } else if (p === "funk") {
                    if ([0, 3, 10].includes(q)) tr.add(t, kick());
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.6);
                    tr.add(t, hat(rng), 0.22, 0.2);
                } else if (p === "afro") {
                    if ([0, 6, 8, 14].includes(q)) tr.add(t, kick(0.9));
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.4);
                    if ([2, 5, 7, 10, 13, 15].includes(q)) tr.add(t, hat(rng), 0.25, -0.15);
                } else if (p === "ballad") {
                    if (q === 0 || q === 8) tr.add(t, kick(0.7));
                    if (q === 4 || q === 12) tr.add(t, snare(rng), 0.35);
                    if (q % 4 === 0) tr.add(t, hat(rng), 0.15);
                } else if (p === "waltz") {
                    if (q === 0) tr.add(t, kick());
                    if (q === 4 || q === 8) tr.add(t, snare(rng), 0.3);
                    if (q % 2 === 0) tr.add(t, hat(rng), 0.2);
                }
            }

            if (prog.length === 0) continue;

            const [rootPc, quality] = prog[b % prog.length];
            const notes = chordNotes(rootPc, quality, 4);
            if (spec.piano) {
                for (let i = 0; i < perBar * 2; ++i) {
                    const note = notes[i % notes.length] + (i % 6 >= 3 ? 12 : 0);
                    tr.add(t0 + i * beat / 2, tone(rng, note, beat * 0.9, 8, 1.3, { decay: 0.25, sustain: 0.2 }), 0.12, 0.1);
                }
            } else {
                for (const note of notes)
                    tr.add(t0, tone(rng, note, bar * 0.98, 6, 1.4, { attack: 0.03, decay: 0.6, sustain: 0.5, release: 0.2 }), 0.05, 0.25);
            }

            const bassNote = 24 + rootPc + (rootPc < 4 ? 12 : 0);
            for (let k = 0; k < perBar; ++k)
                if (["four", "dnb", "trap"].includes(p) || k % 2 === 0)
                    tr.add(t0 + k * beat, tone(rng, bassNote, beat * 0.8, 4, 1.8, { decay: 0.2, sustain: 0.5 }), 0.22);

            if (spec.melody !== false) {
                const choices = [0, 2, 4, 0, 1, 3, 4, 2];
                for (let k = 0; k < perBar; ++k) {
                    const degree = b % 2 === 1 && k === perBar - 1 ? 0 : choices[Math.floor(rng() * choices.length)];
                    const midi = 60 + tonicPc + scale[degree % 7];
                    tr.add(t0 + k * beat, tone(rng, midi, beat * 0.85, 5, 1.1, { decay: 0.15, sustain: 0.4 }), 0.07, -0.3);
                }
            }
        }
        return tr;
    }

    function toWav(left, right) {
        let peak = 1e-9;
        for (let i = 0; i < left.length; ++i) peak = Math.max(peak, Math.abs(left[i]), Math.abs(right[i]));
        const gain = 0.7 / peak;
        const frames = left.length;
        const bytes = new Uint8Array(44 + frames * 4);
        const view = new DataView(bytes.buffer);
        const text = (offset, s) => { for (let i = 0; i < s.length; ++i) bytes[offset + i] = s.charCodeAt(i); };
        text(0, "RIFF"); view.setUint32(4, 36 + frames * 4, true); text(8, "WAVE");
        text(12, "fmt "); view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 2, true);
        view.setUint32(24, RATE, true); view.setUint32(28, RATE * 4, true); view.setUint16(32, 4, true); view.setUint16(34, 16, true);
        text(36, "data"); view.setUint32(40, frames * 4, true);
        for (let i = 0; i < frames; ++i) {
            view.setInt16(44 + i * 4, Math.round(Math.max(-1, Math.min(1, left[i] * gain)) * 32767), true);
            view.setInt16(46 + i * 4, Math.round(Math.max(-1, Math.min(1, right[i] * gain)) * 32767), true);
        }
        return bytes;
    }

    const SPECS = [
        { name: "01_house_124_Am", bpm: 124, tonic: "A", mode: "minor", progression: "i-VI-VII-i", drums: "four" },
        { name: "02_hiphop_90_Fm", bpm: 90, tonic: "F", mode: "minor", progression: "i-iv-VII-i", drums: "boombap", swing: 0.25 },
        { name: "03_trap_140_C#m", bpm: 140, tonic: "C#", mode: "minor", progression: "i-VI-VII-i", drums: "trap" },
        { name: "04_dnb_174_Gm", bpm: 174, tonic: "G", mode: "minor", progression: "i-iv-V-i", drums: "dnb" },
        { name: "05_pop_100_C", bpm: 100, tonic: "C", mode: "major", progression: "I-V-vi-IV", drums: "four" },
        { name: "06_techno_130_F#m", bpm: 130, tonic: "F#", mode: "minor", progression: "i-iv-V-i", drums: "four" },
        { name: "07_ballad_72_Eb", bpm: 72, tonic: "Eb", mode: "major", progression: "I-IV-V-I", drums: "ballad" },
        { name: "08_reggaeton_95_Dm", bpm: 95, tonic: "D", mode: "minor", progression: "i-VI-VII-i", drums: "dembow" },
        { name: "09_funk_110_E", bpm: 110, tonic: "E", mode: "major", progression: "I-IV", drums: "funk" },
        { name: "10_afrobeat_104_Bbm", bpm: 104, tonic: "Bb", mode: "minor", progression: "i-iv-VII-i", drums: "afro" },
        { name: "11_lofi_85_Ab", bpm: 85, tonic: "Ab", mode: "major", progression: "I-vi-ii-V", drums: "boombap", swing: 0.33 },
        { name: "12_edm_128_Bm", bpm: 128, tonic: "B", mode: "minor", progression: "i-VI-VII-i", drums: "four" },
        { name: "13_waltz_90_G", bpm: 90, tonic: "G", mode: "major", progression: "I-IV-V-I", drums: "waltz", meter: 3 },
        { name: "14_piano_only_120_D", bpm: 120, tonic: "D", mode: "major", progression: "I-IV-V-I", drums: "none", piano: true, melody: false },
        { name: "15_drums_only_120", bpm: 120, tonic: "C", mode: "major", progression: "I-IV-V-I", drums: "four", key: false },
    ];

    function generate(spec, index) {
        const track = render(spec, 1000 + index);
        return {
            name: spec.name + ".wav",
            bpm: spec.bpm,
            key: spec.key === false ? null : spec.tonic + " " + spec.mode,
            wav: toWav(track.left, track.right),
        };
    }

    const api = { SPECS, generate };
    if (typeof module !== "undefined" && module.exports) module.exports = api;
    else root.SonarcTestSet = api;

    if (typeof require !== "undefined" && typeof module !== "undefined" && require.main === module) {
        const fs = require("fs"), path = require("path");
        const out = process.argv[2] || "testset";
        fs.mkdirSync(out, { recursive: true });
        const truth = [];
        SPECS.forEach((spec, i) => {
            const t = generate(spec, i);
            fs.writeFileSync(path.join(out, t.name), t.wav);
            truth.push({ file: t.name, bpm: t.bpm, key: t.key });
            console.log("wrote", t.name);
        });
        fs.writeFileSync(path.join(out, "truth.json"), JSON.stringify(truth, null, 2));
    }
})(typeof window !== "undefined" ? window : globalThis);
