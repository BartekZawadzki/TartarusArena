"""music_short.py — the short teaser (2026-10-02, ~1 min, from the operator's recording + the best 5v5 takes): music_v3's
instruments and theme on a new timeline: I0 0-4.8 (the roster, harp and pad) | the countdown 4.8-7.2 (drum ticks, a
riser) | FIGHT 7.2-9.6 (a stinger) | G1 9.6-28.8 | Tartarus 28.8-31.8 (a stinger) | G2 31.8-51.0 (the climax) | logo
51.0-59.0.

music_v3.py — teaser v3's score (operator 2026-10-02: game-style music under the gameplay, a different music on the
transitions). Two musics on one timeline, synthesised from scratch in numpy (no samples):
  * the gameplay music: a fantasy game theme in D minor, 100 BPM — harp arpeggios (Karplus-Strong), string pad, a
    flute (then horn, then both) on an 8-bar melody, frame drums and a shaker, low string spiccato; the last section a
    whole step up (E minor) with a choir. Each section fades out just before a title card.
  * the transition music: dark cinematic stingers on the cards — reverse swell, braam, taiko, choir, a bell shimmer;
    the logo gets the long one, resolving to D major.
Timeline (s): G0 0-7.2 | T1 7.2-10.2 | G1 10.2-27.0 | T2 27.0-30.0 | G2 30.0-49.2 | T3 49.2-52.2 | G3 52.2-71.4 |
T4 71.4-79.4 (logo). Run: python music_v3.py <out.wav>
"""
import sys
import wave

import numpy as np

SR = 48000
END = 59.0
N = int(SR * END)
BEAT = 0.6
BAR = 2.4
E8 = BEAT / 2
rng = np.random.default_rng(20261002)
GM = [np.zeros(N), np.zeros(N)]   # the gameplay music (L, R)
TR = [np.zeros(N), np.zeros(N)]   # the transition music

SECTIONS = {"G0": (0.0, 4.8), "G1": (9.6, 28.8), "G2": (31.8, 51.0)}
CARDS = [(7.2, 9.6), (28.8, 31.8)]
COUNT = (4.8, 5.4, 6.0)   # the countdown: 3, 2, 1
LOGO = (51.0, 59.0)


def tvec(dur):
    return np.arange(int(dur * SR)) / SR


def add(bus, sig, t0, gain=1.0, pan=0.0):
    i = int(round(t0 * SR))
    if i < 0:
        sig, i = sig[-i:], 0
    j = min(N, i + len(sig))
    if j <= i:
        return
    s = sig[: j - i] * gain
    bus[0][i:j] += s * np.sqrt(0.5 * (1 - pan))
    bus[1][i:j] += s * np.sqrt(0.5 * (1 + pan))


def fft_filter(x, lo=None, hi=None, order=2):
    X = np.fft.rfft(x)
    f = np.maximum(np.fft.rfftfreq(len(x), 1 / SR), 1e-3)
    g = np.ones_like(f)
    if hi is not None:
        g /= np.sqrt(1 + (f / hi) ** (2 * order))
    if lo is not None:
        g /= np.sqrt(1 + (lo / f) ** (2 * order))
    return np.fft.irfft(X * g, len(x))


def formant(x, vowel=((800, 120, 1.0), (1150, 160, 0.6), (2900, 220, 0.22))):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    g = sum(a / (1 + ((f - c) / b) ** 2) for c, b, a in vowel)
    return np.fft.irfft(X * g, len(x))


def env(dur, a=0.01, r=0.1, hold=None):
    t = tvec(dur)
    e = np.clip(t / max(a, 1e-4), 0, 1)
    hold = dur - r if hold is None else hold
    return e * np.clip(1 - (t - hold) / max(r, 1e-4), 0, 1)


def saw(freq, dur, cut=None, cents=0.0, phase=0.0, kmax=48, vib=0.0):
    t = tvec(dur)
    f = freq * 2 ** (cents / 1200)
    K = max(1, min(kmax, int(12000 / f)))
    fc = cut(t) if callable(cut) else cut
    ph = 2 * np.pi * f * (t + (vib * np.sin(2 * np.pi * 5.0 * t) / (2 * np.pi * 5.0) if vib else 0))
    out = np.zeros_like(t)
    for k in range(1, K + 1):
        a = 1.0 / k
        if fc is not None:
            a = a / np.sqrt(1 + (k * f / fc) ** 4)
        out += a * np.sin(k * ph + phase * k)
    return out


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# ------------------------------------------------------------------ instruments
def harp(freq, dur=1.6, bright=0.55, decay=0.9965):
    """Karplus-Strong: a noise burst in a delay line one period long, averaged and decayed every pass."""
    P = max(2, int(round(SR / freq)))
    n = int(dur * SR)
    buf = fft_filter(rng.uniform(-1, 1, P * 4), hi=freq * 6)[:P]
    out = np.zeros(n)
    k = 0
    while k < n:
        m = min(P, n - k)
        out[k:k + m] = buf[:m]
        buf = decay * (bright * buf + (1 - bright) * np.roll(buf, 1))
        k += P
    return out * np.clip(tvec(dur) / 0.002, 0, 1) * np.clip((dur - tvec(dur)) / 0.2, 0, 1)


def flute(freq, dur):
    t = tvec(dur)
    vib = 1 + 0.0045 * np.sin(2 * np.pi * 5.2 * t) * np.clip((t - 0.15) / 0.3, 0, 1)
    ph = 2 * np.pi * np.cumsum(freq * vib) / SR
    s = np.sin(ph) + 0.22 * np.sin(2 * ph) + 0.07 * np.sin(3 * ph)
    breath = fft_filter(rng.standard_normal(len(t)), lo=freq * 1.5, hi=freq * 5) * 0.06
    return (s + breath) * env(dur, a=0.07, r=0.12)


def horn(freq, dur):
    cut = lambda t: 350 + 1500 * (1 - np.exp(-t * 10)) * (0.85 + 0.15 * np.exp(-t * 2))
    s = sum(saw(freq, dur, cut, cents=c, phase=rng.uniform(0, 6.28), kmax=30, vib=0.003) for c in (-6, 0, 7)) / 3
    return np.tanh(1.4 * s) * env(dur, a=0.07, r=0.15)


def spiccato(freq, dur=0.27):
    t = tvec(dur)
    s = saw(freq, dur, 1500, kmax=36) + saw(freq, dur, 1500, cents=8, phase=1.3, kmax=36)
    return s * np.exp(-t * 6) * env(dur, a=0.008, r=0.05)


def pad(freqs, dur, cut=1300.0, a=0.5, r=0.6):
    out = 0
    for f in freqs:
        for c in (-12, -4, 5, 13):
            out = out + saw(f, dur, cut, cents=c, phase=rng.uniform(0, 6.28), kmax=34)
    return out * env(dur, a=a, r=r) / (len(freqs) * 4)


def choir(freqs, dur, a=0.6, r=0.8):
    raw = 0
    for f in freqs:
        for c in (-10, -3, 4, 11):
            raw = raw + saw(f, dur, None, cents=c, phase=rng.uniform(0, 6.28), kmax=60, vib=0.004)
    return formant(raw) * env(dur, a=a, r=r) / (len(freqs) * 4)


def taiko(f0=150.0, f1=46.0, dur=1.2, dec=3.5, click=0.3):
    t = tvec(dur)
    f = f1 + (f0 - f1) * np.exp(-t * 22)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * dec)
    nz = fft_filter(rng.standard_normal(len(t)), hi=1600) * np.exp(-t * 45)
    return np.tanh(1.5 * (body + click * nz))


def frame_drum(dur=0.4):
    t = tvec(dur)
    nz = fft_filter(rng.standard_normal(len(t)), lo=900, hi=4500) * np.exp(-t * 22)
    return 0.6 * taiko(230, 150, dur, dec=12, click=0.5) + 0.5 * nz


def shaker(dur=0.09):
    t = tvec(dur)
    return fft_filter(rng.standard_normal(len(t)), lo=5000, hi=10000) * np.exp(-t * 38) * np.clip(t / 0.004, 0, 1)


def crash(dur=3.0):
    t = tvec(dur)
    return fft_filter(rng.standard_normal(len(t)), lo=3500) * np.exp(-t * 1.6) * 0.6


def braam(root=36.71, dur=2.8):
    cut = lambda t: 380 + 2200 * np.clip(t / 0.08, 0, 1) * np.exp(-np.maximum(t - 0.08, 0) * 1.4)
    out = 0
    for f, g in zip((root, root * 2, root * 3, root * 4), (1.0, 0.9, 0.7, 0.5)):
        for c in (-9, 0, 8):
            out = out + g * saw(f, dur, cut, cents=c, phase=rng.uniform(0, 6.28), kmax=40)
    return np.tanh(out * 0.6) * env(dur, a=0.015, r=1.0)


def sub(f0=55.0, f1=30.0, dur=2.2):
    t = tvec(dur)
    f = f1 + (f0 - f1) * np.exp(-t * 4)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 1.6) * np.clip(t / 0.005, 0, 1)


def bell(freq, dur=2.0):
    t = tvec(dur)
    return sum(a * np.sin(2 * np.pi * freq * r * t) * np.exp(-t * d) for r, a, d in ((1, 1, 2.5), (2.76, 0.5, 4), (5.4, 0.25, 6), (8.9, 0.12, 9)))


# ------------------------------------------------------------------ the gameplay music
CH = {  # chord: bass, triad (octave 3)
    "Dm": (midi(38), (midi(50), midi(53), midi(57))),
    "Bb": (midi(34), (midi(46), midi(50), midi(53))),
    "C": (midi(36), (midi(48), midi(52), midi(55))),
}
PROG = ["Dm", "Bb", "C", "Dm", "Dm", "Bb", "C", "Dm"]
# the theme, 8 bars, (midi, length in 8ths); 0 = a rest
THEME = [
    [(69, 2), (74, 2), (77, 1), (76, 1), (74, 2)],
    [(77, 3), (79, 1), (77, 1), (74, 1), (70, 2)],
    [(72, 2), (76, 2), (79, 1), (77, 1), (76, 2)],
    [(74, 6), (0, 2)],
    [(69, 2), (74, 2), (77, 1), (79, 1), (81, 2)],
    [(82, 3), (81, 1), (79, 1), (77, 1), (74, 2)],
    [(76, 2), (79, 2), (84, 1), (82, 1), (81, 2)],
    [(81, 4), (77, 2), (74, 2)],
]


def section(name, t0, bars, intensity, shift=0, melody=None, melody_from=0):
    k = 2 ** (shift / 12)
    for b in range(bars):
        tb = t0 + b * BAR
        bass, tri = CH[PROG[b % 8]]
        r, th, fi = (x * k for x in tri)
        # the harp: an arpeggio in 8ths over two octaves
        for i, f in enumerate((r, fi, 2 * r, 2 * th, 2 * fi, 4 * r, 2 * fi, 2 * th)):
            add(GM, harp(f, 1.5), tb + i * E8, (0.16 if i % 2 == 0 else 0.12) * (0.8 + 0.2 * intensity), pan=-0.35 + 0.1 * (i % 3))
        # the string pad
        add(GM, pad([r, th * 2, fi, r * 2], BAR + 0.5, cut=900 + 700 * intensity, a=0.35, r=0.5), tb, 0.5 + 0.25 * intensity)
        if intensity >= 0.5:   # low strings in 8ths
            for i in range(8):
                f = bass * k * (2 if i in (2, 6) else 1)
                add(GM, spiccato(f), tb + i * E8, 0.12 + 0.06 * intensity * (1.3 if i % 4 == 0 else 1.0), pan=0.3)
        if intensity >= 0.85:   # the choir
            add(GM, choir([r * 2, th * 2, fi * 2], BAR + 0.4, a=0.4, r=0.5), tb, 0.55, pan=0.0)
        # drums
        if intensity > 0.2:
            add(GM, taiko(130, 50, 1.0, dec=4.5, click=0.2), tb, 0.45 * intensity)
            add(GM, taiko(130, 50, 1.0, dec=4.5, click=0.2), tb + 2 * BEAT, 0.35 * intensity)
            if intensity >= 0.5:
                add(GM, taiko(120, 48, 0.9, dec=5, click=0.2), tb + 2.5 * BEAT, 0.25 * intensity)
            for i in (1, 3):
                add(GM, frame_drum(), tb + i * BEAT, 0.2 + 0.15 * intensity, pan=0.15)
            for i in range(16):
                add(GM, shaker(), tb + i * E8 / 2, (0.05 if i % 2 else 0.035) * intensity, pan=0.45)
        if b == bars - 1 and intensity >= 0.5:   # a tom fill into the card
            for i in range(4):
                add(GM, taiko(260 - 30 * i, 120 - 10 * i, 0.5, dec=8, click=0.4), tb + 3 * BEAT + i * E8 / 2, 0.15 + 0.05 * i)
        # the melody
        if melody and b >= melody_from:
            t = tb
            for m, L in THEME[(b - melody_from) % 8]:
                d = L * E8
                if m:
                    f = midi(m) * k
                    if melody in ("flute", "both"):
                        add(GM, flute(f, d * 0.98), t, 0.2 if melody == "flute" else 0.13, pan=0.1)
                    if melody in ("horn", "both"):
                        add(GM, horn(f / 2 if melody == "horn" else f / 2, d * 0.98), t, 0.26, pan=-0.05)
                t += d


section("G0", 0.0, 2, 0.15)
section("G1", 9.6, 8, 0.6, melody="flute", melody_from=0)
section("G2", 31.8, 8, 0.95, shift=2, melody="both", melody_from=0)
# each gameplay section: in gently, out just before its card ("off on the transitions")
gate = np.zeros(N)
t = tvec(END)
for name, (a, b) in SECTIONS.items():
    fade_in = 1.6 if name == "G0" else 0.08
    g = np.clip((t - a) / fade_in, 0, 1) * np.clip((b - 0.05 - t) / 0.4, 0, 1)
    gate = np.maximum(gate, g)
GM = [GM[0] * gate, GM[1] * gate]


# ------------------------------------------------------------------ the transition music (the cards)
def stinger(t0, length=3.0, final=False):
    add(TR, crash(1.3)[::-1], t0 - 1.3, 0.22)
    swell = fft_filter(rng.standard_normal(int(1.3 * SR)), lo=300, hi=6000) * np.linspace(0, 1, int(1.3 * SR)) ** 3
    add(TR, swell, t0 - 1.3, 0.05)
    add(TR, braam(36.71, 4.5 if final else 2.8), t0, 0.5 if final else 0.4)
    add(TR, taiko(160, 44, 1.6), t0, 0.85)
    add(TR, taiko(120, 40, 1.8), t0 + 0.02, 0.55, pan=-0.3)
    add(TR, sub(60, 28, 3.0 if final else 2.2), t0, 0.7)
    add(TR, crash(4.0 if final else 2.6), t0, 0.22)
    notes = [midi(50), midi(54), midi(57), midi(62)] if final else [midi(50), midi(53), midi(57), midi(62)]
    add(TR, choir(notes, length - 0.2, a=0.5 if not final else 1.2, r=0.9 if not final else 2.5), t0 + 0.05, 0.75)
    for i, f in enumerate((midi(74), midi(81), midi(86))):
        add(TR, bell(f, 2.5), t0 + 0.12 + i * 0.09, 0.05, pan=-0.3 + 0.3 * i)
    add(TR, fft_filter(rng.standard_normal(int(length * SR)), hi=180) * env(length, a=0.3, r=0.6), t0, 0.15)   # a low drone under the card


# the countdown: a drum and a bell on each number, a riser into FIGHT
for i, tc in enumerate(COUNT):
    add(TR, taiko(150 + 25 * i, 60, 0.9, dec=5, click=0.4), tc, 0.55 + 0.1 * i)
    add(TR, bell(midi(74 + 5 * i), 1.2), tc, 0.07)
add(TR, fft_filter(rng.standard_normal(int(2.4 * SR)), lo=400, hi=9000) * np.linspace(0, 1, int(2.4 * SR)) ** 2.5, COUNT[0], 0.12)
add(TR, fft_filter(rng.standard_normal(int(2.4 * SR)), hi=160) * env(2.4, a=0.4, r=0.1), COUNT[0], 0.18)
for a, b in CARDS:
    stinger(a, b - a)
stinger(LOGO[0], LOGO[1] - LOGO[0], final=True)
add(TR, taiko(110, 38, 2.5, dec=1.8), LOGO[0] + 2.4, 0.45)   # the subtitle lands
for i, f in enumerate((midi(74), midi(78), midi(81))):
    add(TR, bell(f, 3.0), LOGO[0] + 2.45 + i * 0.12, 0.06)


# ------------------------------------------------------------------ mix
def reverb(x, secs=2.6, mix=0.22, seed=1):
    r = np.random.default_rng(seed)
    tt = tvec(secs)
    ir = r.standard_normal(len(tt)) * np.exp(-tt * 2.6)
    ir[: int(0.015 * SR)] = 0
    ir = fft_filter(ir, hi=6000)
    ir /= np.sqrt(np.sum(ir ** 2))
    nfft = 1 << (len(x) + len(ir) - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nfft) * np.fft.rfft(ir, nfft), nfft)[: len(x)]
    return x + mix * y


def eq(x):
    X = np.fft.rfft(x)
    f = np.maximum(np.fft.rfftfreq(len(x), 1 / SR), 1e-3)
    db = -4.0 / (1 + (f / 90.0) ** 2) + 2.5 * np.exp(-((np.log2(f / 3000.0)) ** 2) / 1.2)
    return np.fft.irfft(X * 10 ** (db / 20), len(x))


L = reverb(GM[0], mix=0.25, seed=1) * 1.0 + reverb(TR[0], mix=0.3, seed=3)
R = reverb(GM[1], mix=0.25, seed=2) * 1.0 + reverb(TR[1], mix=0.3, seed=4)
L, R = eq(fft_filter(L, lo=28)), eq(fft_filter(R, lo=28))
tail = np.clip((END - tvec(END)) / 2.5, 0, 1)
L, R = L * tail, R * tail
pk = max(np.max(np.abs(L)), np.max(np.abs(R)))
L = np.tanh(L / pk * 1.3) / np.tanh(1.3) * 0.89
R = np.tanh(R / pk * 1.3) / np.tanh(1.3) * 0.89
out = sys.argv[1] if len(sys.argv) > 1 else "teaser_music_short.wav"
pcm = (np.stack([L, R], axis=1) * 32767).astype(np.int16)
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(pcm.tobytes())
print(f"MUSIC OK {out} secs={END} peak={np.max(np.abs(pcm)) / 32767:.2f}")
