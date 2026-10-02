// music_short.py from music_v3.py: the same instruments and theme on the short teaser's timeline (~59 s)
const fs = require('fs');
const dir = process.argv[2];
let s = fs.readFileSync(dir + '/music_v3.py', 'utf8');
function rep(a, b) { if (!s.includes(a)) { console.log('MISSING: ' + a.slice(0, 70)); process.exitCode = 1; return; } s = s.replace(a, b); }
rep('"""music_v3.py', '"""music_short.py — the short teaser (2026-10-02, ~1 min, from the operator\'s recording + the best 5v5 takes): music_v3\'s\ninstruments and theme on a new timeline: I0 0-4.8 (the roster, harp and pad) | the countdown 4.8-7.2 (drum ticks, a\nriser) | FIGHT 7.2-9.6 (a stinger) | G1 9.6-28.8 | Tartarus 28.8-31.8 (a stinger) | G2 31.8-51.0 (the climax) | logo\n51.0-59.0.\n\nmusic_v3.py');
rep('END = 79.4', 'END = 59.0');
rep('SECTIONS = {"G0": (0.0, 7.2), "G1": (10.2, 27.0), "G2": (30.0, 49.2), "G3": (52.2, 71.4)}', 'SECTIONS = {"G0": (0.0, 4.8), "G1": (9.6, 28.8), "G2": (31.8, 51.0)}');
rep('CARDS = [(7.2, 10.2), (27.0, 30.0), (49.2, 52.2)]', 'CARDS = [(7.2, 9.6), (28.8, 31.8)]\nCOUNT = (4.8, 5.4, 6.0)   # the countdown: 3, 2, 1');
rep('LOGO = (71.4, 79.4)', 'LOGO = (51.0, 59.0)');
rep('section("G0", 0.0, 3, 0.15)\nsection("G1", 10.2, 7, 0.45, melody="flute", melody_from=0)\nsection("G2", 30.0, 8, 0.7, melody="horn", melody_from=0)\nsection("G3", 52.2, 8, 0.95, shift=2, melody="both", melody_from=0)',
    'section("G0", 0.0, 2, 0.15)\nsection("G1", 9.6, 8, 0.6, melody="flute", melody_from=0)\nsection("G2", 31.8, 8, 0.95, shift=2, melody="both", melody_from=0)');
rep('fade_in = 3.0 if name == "G0" else 0.08', 'fade_in = 1.6 if name == "G0" else 0.08');
rep('for a, b in CARDS:', '# the countdown: a drum and a bell on each number, a riser into FIGHT\nfor i, tc in enumerate(COUNT):\n    add(TR, taiko(150 + 25 * i, 60, 0.9, dec=5, click=0.4), tc, 0.55 + 0.1 * i)\n    add(TR, bell(midi(74 + 5 * i), 1.2), tc, 0.07)\nadd(TR, fft_filter(rng.standard_normal(int(2.4 * SR)), lo=400, hi=9000) * np.linspace(0, 1, int(2.4 * SR)) ** 2.5, COUNT[0], 0.12)\nadd(TR, fft_filter(rng.standard_normal(int(2.4 * SR)), hi=160) * env(2.4, a=0.4, r=0.1), COUNT[0], 0.18)\nfor a, b in CARDS:');
rep('out = sys.argv[1] if len(sys.argv) > 1 else "teaser_music_v3.wav"', 'out = sys.argv[1] if len(sys.argv) > 1 else "teaser_music_short.wav"');
fs.writeFileSync(dir + '/music_short.py', s);
console.log('ok');
