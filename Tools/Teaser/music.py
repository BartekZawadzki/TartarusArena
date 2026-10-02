"""music.py — the teaser's soundtrack, synthesised from scratch (no samples): a D-minor trailer cue at 120 BPM.
Sections (seconds, shared with the edit): 0-7 intro drone, 7 hit, 7-17 pulse, 17 hit, 17-29 drums, 29 hit,
29-43 drive, 43-47 build (montage), 47 the big hit and the logo, out at 55.
Run: python music.py <out.wav>
"""
import sys
import wave

import numpy as np

SR = 48000
DUR = 55.0
N = int(SR * DUR)
BEAT = 0.5
rng = np.random.default_rng(20261001)
L = np.zeros(N)
R = np.zeros(N)

HITS = [7.0, 17.0, 29.0]
BIG = 47.0
MONTAGE = (43.0, 47.0)


def add(sig, t0, gain=1.0, pan=0.0, side=None):
    i = int(round(t0 * SR))
    if i < 0:
        sig = sig[-i:]
        i = 0
    j = min(N, i + len(sig))
    if j <= i:
        return
    s = sig[: j - i] * gain
    if side is not None:
        L[i:j] += s * side[0]
        R[i:j] += s * side[1]
        return
    L[i:j] += s * np.sqrt(0.5 * (1 - pan))
    R[i:j] += s * np.sqrt(0.5 * (1 + pan))


def tvec(dur):
    return np.arange(int(dur * SR)) / SR


def fft_filter(x, lo=None, hi=None, order=2):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    g = np.ones_like(f)
    if hi is not None:
        g /= np.sqrt(1 + (f / hi) ** (2 * order))
    if lo is not None:
        g /= np.sqrt(1 + (lo / np.maximum(f, 1e-3)) ** (2 * order))
    return np.fft.irfft(X * g, n)


def sweep_filter(x, cut_fn, block=2048, lo=False):
    """A time-varying low (or high) pass: overlapping Hann blocks, each filtered at its own cutoff."""
    n = len(x)
    out = np.zeros(n + block)
    win = np.hanning(block)
    hop = block // 2
    f = np.fft.rfftfreq(block, 1 / SR)
    xp = np.concatenate([x, np.zeros(block)])
    for s in range(0, n, hop):
        seg = xp[s : s + block] * win
        fc = cut_fn((s + hop) / SR)
        g = 1 / np.sqrt(1 + (fc / np.maximum(f, 1e-3)) ** 4) if lo else 1 / np.sqrt(1 + (f / fc) ** 4)
        out[s : s + block] += np.fft.irfft(np.fft.rfft(seg) * g, block)
    return out[:n] / 1.0


def saw(freq, dur, cut=None, cents=0.0, phase=0.0, kmax=64):
    t = tvec(dur)
    f = freq * 2 ** (cents / 1200)
    K = max(1, min(kmax, int(14000 / f)))
    fc = cut(t) if callable(cut) else cut
    out = np.zeros_like(t)
    for k in range(1, K + 1):
        a = 1.0 / k
        if fc is not None:
            a = a / np.sqrt(1 + (k * f / fc) ** 4)
        out += a * np.sin(2 * np.pi * k * f * t + phase * k)
    return out


def env(dur, a=0.01, d=0.2, s=0.6, r=0.3, hold=None):
    t = tvec(dur)
    hold = dur - r if hold is None else hold
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    e = np.where(t > hold, e * np.clip(1 - (t - hold) / max(r, 1e-4), 0, 1), e)
    return e


# ---------------------------------------------------------------- instruments
def taiko(f0=150.0, f1=46.0, dur=1.4, dec=3.2, click=0.35):
    t = tvec(dur)
    f = f1 + (f0 - f1) * np.exp(-t * 22)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * dec)
    nz = fft_filter(rng.standard_normal(len(t)), hi=1600) * np.exp(-t * 45)
    return np.tanh(1.6 * (body + click * nz))


def tom(f0=260.0, f1=110.0, dur=0.6):
    return taiko(f0, f1, dur, dec=7.0, click=0.5) * 0.8


def snare(dur=0.45):
    t = tvec(dur)
    nz = fft_filter(rng.standard_normal(len(t)), lo=900, hi=6500) * np.exp(-t * 13)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 28)
    return 0.8 * nz + 0.5 * tone


def crash(dur=3.5):
    t = tvec(dur)
    nz = fft_filter(rng.standard_normal(len(t)), lo=3500) * np.exp(-t * 1.6)
    ring = sum(np.sin(2 * np.pi * f * t) for f in (4211, 5380, 6577, 7893)) * 0.04 * np.exp(-t * 2.2)
    return (nz + ring) * 0.6


def sub(f0=55.0, f1=30.0, dur=2.2):
    t = tvec(dur)
    f = f1 + (f0 - f1) * np.exp(-t * 4)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 1.6) * np.clip(t / 0.005, 0, 1)


def braam(root=36.71, dur=3.6, open_to=2600.0, close_to=380.0):
    """The trailer horn: a stack of detuned saws, the filter bursting open and closing slowly, driven."""
    notes = [root, root * 2, root * 3, root * 4, root * 6]
    gains = [1.0, 0.9, 0.7, 0.55, 0.35]
    cut = lambda t: close_to + (open_to - close_to) * np.clip(t / 0.08, 0, 1) * np.exp(-np.maximum(t - 0.08, 0) * 1.4)
    out = 0
    for f, g in zip(notes, gains):
        for c in (-9.0, 0.0, 8.0):
            out = out + g * saw(f, dur, cut, cents=c, phase=rng.uniform(0, 6.28), kmax=48)
    out = np.tanh(out * 0.6)
    return out * env(dur, a=0.015, d=1.2, s=0.25, r=1.2)


def pluck(freq, dur=0.22):
    cut = lambda t: 350 + 4200 * np.exp(-t * 16)
    return saw(freq, dur, cut, kmax=40) * np.exp(-tvec(dur) * 9) * np.clip(tvec(dur) / 0.003, 0, 1)


def pad(freqs, dur, cut=1400.0, a=0.9, r=1.2):
    out = 0
    for f in freqs:
        for c in (-14, -5, 4, 13):
            out = out + saw(f, dur, cut, cents=c, phase=rng.uniform(0, 6.28), kmax=40)
    return out * env(dur, a=a, d=1.0, s=1.0, r=r) / (len(freqs) * 4)


def riser(dur, f_from=300.0, f_to=6000.0):
    t = tvec(dur)
    nz = rng.standard_normal(len(t))
    lp = sweep_filter(nz, lambda s: f_from * (f_to / f_from) ** (s / dur))
    tone = np.sin(2 * np.pi * np.cumsum(220 * (8 ** (t / dur))) / SR) * 0.25
    return (lp * 0.5 + tone) * (t / dur) ** 2.2


def whoosh(dur=0.9, peak=0.7):
    t = tvec(dur)
    nz = rng.standard_normal(len(t))
    w = sweep_filter(nz, lambda s: 400 + 5000 * np.exp(-((s - peak * dur) ** 2) / 0.04))
    return w * np.exp(-((t - peak * dur) ** 2) / 0.05)


# ---------------------------------------------------------------- the cue
D1, A1, D2, F2, A2, D3 = 36.71, 55.0, 73.42, 87.31, 110.0, 146.83
CHORDS = {  # root, third, fifth (one octave up for the ostinato)
    "Dm": (73.42, 87.31, 110.0),
    "Bb": (58.27, 73.42, 87.31),
    "F": (87.31, 110.0, 130.81),
    "C": (65.41, 82.41, 98.0),
}
PROG = ["Dm", "Bb", "F", "C"]


def chord_at(t):
    return PROG[int((t - 7.0) // 2.0) % 4]


# 0-7: the drone, air, a heartbeat, the reverse crash into the first hit
dr = (saw(D1, 8.0, 160, kmax=30) + saw(D1, 8.0, 160, cents=7, phase=1.0, kmax=30) + 0.6 * saw(A1, 8.0, 200, kmax=30))
add(dr * np.clip(tvec(8.0) / 5.0, 0, 1) ** 1.5 * np.clip((8.0 - tvec(8.0)) / 1.0, 0, 1), 0.0, 0.32)
air = fft_filter(rng.standard_normal(int(8 * SR)), lo=2500, hi=9000) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.25 * tvec(8.0)))
add(air * np.clip(tvec(8.0) / 4, 0, 1), 0.0, 0.02, side=(0.9, 0.5))
add(air[::-1] * np.clip(tvec(8.0) / 4, 0, 1), 0.0, 0.02, side=(0.5, 0.9))
for t0 in (2.5, 2.8, 4.5, 4.8):
    add(taiko(90, 38, 1.0, dec=5.0, click=0.05), t0, 0.35 if t0 in (2.5, 4.5) else 0.22)
add(riser(2.6, 200, 4000), 7.0 - 2.6, 0.18)

# the hits: braam + taiko + sub + crash, a reverse crash before each
for h in HITS + [BIG]:
    big = h == BIG
    add(crash(1.8)[::-1], h - 1.8, 0.22 if not big else 0.32)
    add(braam(D1, 4.5 if big else 3.6), h, 0.55 if big else 0.42)
    add(taiko(160, 44, 1.6), h, 0.9)
    add(taiko(120, 40, 1.8), h + 0.02, 0.6, pan=-0.3)
    add(sub(60, 28, 3.0 if big else 2.2), h, 0.75 if big else 0.6)
    add(crash(4.5 if big else 3.0), h, 0.3 if big else 0.2)
    add(whoosh(0.9, 0.85), h - 0.8, 0.18)

# 7-43: the ostinato (16ths on the chord), quieter under the hits, rising
t = 7.0
step = BEAT / 4
pattern = [0, 0, 2, 0, 1, 0, 2, 0, 0, 0, 2, 0, 1, 2, 1, 0]
i = 0
while t < MONTAGE[0] - 1e-6:
    c = CHORDS[chord_at(t)]
    note = c[pattern[i % 16]] * 2
    lvl = 0.10 + 0.10 * np.clip((t - 7) / 36, 0, 1)
    acc = 1.25 if i % 4 == 0 else 1.0
    add(pluck(note), t, lvl * acc, pan=-0.35)
    if t >= 29.0:   # the drive: the octave up on the other side
        add(pluck(note * 2, 0.16), t, lvl * 0.55 * acc, pan=0.4)
    t += step
    i += 1

# 17-47: the string pad, one chord a bar
t = 17.0
while t < 47.0 - 1e-6:
    c = CHORDS[chord_at(t)]
    add(pad([c[0], c[1] * 2, c[2] * 2, c[0] * 4], 2.4, cut=900 + 900 * np.clip((t - 17) / 26, 0, 1), a=0.3, r=0.6), t, 0.55, pan=0.0)
    t += 2.0
add(pad([D1 * 2, A1 * 2, D2 * 2, A2 * 2, D3 * 2], 8.0, cut=1600, a=0.2, r=3.0), BIG, 0.6)

# drums
for bar_t in np.arange(7.0, 17.0, 2.0):     # the pulse: beat 1 and 3
    if bar_t not in HITS:
        add(taiko(), bar_t, 0.55)
    add(taiko(130, 50, 1.0), bar_t + 1.0, 0.3)
for bar_t in np.arange(17.0, 43.0, 2.0):    # the groove
    for b, g in ((0, 0.75), (1.5, 0.45), (2, 0.6), (3, 0.5)):
        if b == 0 and bar_t in HITS:
            continue
        add(taiko(), bar_t + b * BEAT, g)
    for b in (1, 3):
        add(snare(), bar_t + b * BEAT, 0.35, pan=0.15)
    if bar_t >= 29.0:                         # the drive: toms on the last beat
        for k in range(4):
            add(tom(300 - k * 35, 120 - k * 12), bar_t + 3 * BEAT + k * BEAT / 4, 0.32 + 0.05 * k, pan=-0.5 + 0.33 * k)
# 43-47: the montage: a hit on every beat, toms in 16ths swelling, the riser
for k in range(8):
    tb = MONTAGE[0] + k * BEAT
    add(taiko(170, 46, 1.0), tb, 0.6 + 0.04 * k)
    add(snare(), tb + BEAT / 2, 0.25 + 0.03 * k)
for k in range(32):
    tb = MONTAGE[0] + k * BEAT / 4
    add(tom(320, 130, 0.35), tb, 0.08 + 0.3 * (k / 31) ** 2, pan=(-0.6 if k % 2 else 0.6))
add(riser(4.0, 250, 9000), MONTAGE[0], 0.32)
add(crash(2.0)[::-1], BIG - 2.0, 0.3)
# the last word: a low boom when the subtitle lands, and the tail
add(taiko(110, 38, 2.5, dec=1.8), 50.0, 0.55)
add(sub(50, 26, 3.0), 50.0, 0.5)

# ---------------------------------------------------------------- mix: reverb, master
def reverb(x, secs=2.4, mix=0.2, seed=1):
    r = np.random.default_rng(seed)
    t = tvec(secs)
    ir = r.standard_normal(len(t)) * np.exp(-t * 2.8)
    ir[: int(0.012 * SR)] = 0
    ir = fft_filter(ir, hi=5500)
    ir /= np.sqrt(np.sum(ir ** 2))
    n = len(x) + len(ir)
    nfft = 1 << (n - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nfft) * np.fft.rfft(ir, nfft), nfft)[: len(x)]
    return x + mix * y


def eq(x):
    """The master EQ: less sub (the drums and the braams pile up below 100 Hz), more presence (2-5 kHz)."""
    X = np.fft.rfft(x)
    f = np.maximum(np.fft.rfftfreq(len(x), 1 / SR), 1e-3)
    db = -5.0 / (1 + (f / 90.0) ** 2) + 3.0 * np.exp(-((np.log2(f / 3000.0)) ** 2) / 1.2)
    return np.fft.irfft(X * 10 ** (db / 20), len(x))


L = eq(L)
R = eq(R)
L = reverb(L, seed=1)
R = reverb(R, seed=2)
L = fft_filter(L, lo=28)
R = fft_filter(R, lo=28)
fade = np.clip((DUR - tvec(DUR)) / 2.0, 0, 1)
L *= fade
R *= fade
pk = max(np.max(np.abs(L)), np.max(np.abs(R)))
L = np.tanh(L / pk * 1.4) / np.tanh(1.4) * 0.89
R = np.tanh(R / pk * 1.4) / np.tanh(1.4) * 0.89
out = sys.argv[1] if len(sys.argv) > 1 else "teaser_music.wav"
pcm = (np.stack([L, R], axis=1) * 32767).astype(np.int16)
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(pcm.tobytes())
print(f"MUSIC OK {out} secs={DUR} peak={np.max(np.abs(pcm)) / 32767:.2f} rms={np.sqrt(np.mean((pcm / 32767.0) ** 2)):.3f}")
