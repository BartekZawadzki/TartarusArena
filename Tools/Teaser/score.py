"""score.py — the action in every take, frame by frame: motion (YDIF, the frame-to-frame change) and the colour of
the abilities' effects (SATAVG, the mean saturation), smoothed over half a second; prints the best windows.
Run: python score.py <frames_dir> [take ...]   -> scores.json next to the frames
"""
import json
import os
import re
import subprocess
import sys

import numpy as np

FF = os.environ.get("FFMPEG", "ffmpeg")   # ffmpeg on the PATH, or its full path in FFMPEG
root = sys.argv[1]
takes = sys.argv[2:] or sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
out = {}
path = os.path.join(root, "..", "scores.json")
if os.path.exists(path):
    out = json.load(open(path))
for t in takes:
    pat = os.path.join(root, t, "MovieFrame%05d.png")
    cmd = [FF, "-v", "error", "-framerate", "30", "-i", pat, "-vf", "scale=320:180,signalstats,metadata=print:file=-", "-f", "null", "-"]
    txt = subprocess.run(cmd, capture_output=True, text=True).stdout
    sat = [float(x) for x in re.findall(r"lavfi\.signalstats\.SATAVG=([\d.]+)", txt)]
    ydif = [float(x) for x in re.findall(r"lavfi\.signalstats\.YDIF=([\d.]+)", txt)]
    n = min(len(sat), len(ydif))
    if n == 0:
        print(f"SCORE {t} no frames")
        continue
    s = np.array(sat[:n])
    d = np.array(ydif[:n])
    z = lambda a: (a - a.mean()) / (a.std() + 1e-6)
    k = np.ones(15) / 15
    score = np.convolve(z(d) * 0.6 + z(s) * 0.4, k, mode="same")
    out[t] = {"n": n, "score": [round(float(v), 3) for v in score], "ydif": [round(float(v), 2) for v in d], "sat": [round(float(v), 2) for v in s]}
    best = np.argsort(score)[::-1]
    picks = []
    for b in best:
        if all(abs(b - p) > 30 for p in picks):
            picks.append(int(b))
        if len(picks) == 4:
            break
    print(f"SCORE {t} n={n} best_s=" + ",".join(f"{p / 30:.1f}" for p in picks) + f" ydif_mean={d.mean():.1f} sat_mean={s.mean():.1f}")
json.dump(out, open(path, "w"))
