# NMF prototype: harmonic templates per pitch, adapted per recording (shared over time), KL multiplicative updates.
import sys, numpy as np, librosa
SR, HOP, BPO, FMIN, NP = 22050, 256, 36, 27.5, 88
H_PARTIALS = 12
import os
CLIP_LO, CLIP_HI = float(os.environ.get('CLO', '0.25')), float(os.environ.get('CHI', '4'))
HARM = os.environ.get('HARM', '0') == '1'
REL = float(os.environ.get('REL', '0'))

def templates(nbins):
    W = np.zeros((nbins, NP))
    for p in range(NP):
        for h in range(1, H_PARTIALS + 1):
            b = 3 * p + int(round(BPO * np.log2(h)))
            for d, w in ((-1, .3), (0, 1.0), (1, .3)):
                if 0 <= b + d < nbins: W[b + d, p] += w / h
    return W / W.sum(0, keepdims=True)

def nmf(V, W, iters, adapt, sparsity):
    W0 = W.copy()
    H = np.full((W.shape[1], V.shape[1]), V.mean())
    mask = W > 0
    eps = 1e-12
    for i in range(iters):
        R = W @ H + eps
        H *= (W.T @ (V / R)) / (W.sum(0)[:, None] + sparsity)
        if adapt and i >= iters // 4:
            R = W @ H + eps
            W *= ((V / R) @ H.T) / (H.sum(1)[None, :] + eps)
            W *= mask
            W /= W.sum(0, keepdims=True) + eps
            W = np.clip(W, W0 * CLIP_LO, W0 * CLIP_HI)
            W /= W.sum(0, keepdims=True) + eps
    return W, H

def decode(H, on, off, minframes, fps):
    notes, ref = [], H.max()
    A = H / ref
    if REL > 0: A = np.where(H >= REL * H.max(0, keepdims=True), A, 0)
    for p in range(NP):
        active, start = False, 0
        for t, a in enumerate(np.append(A[p], 0)):
            if not active and a >= on: active, start = True, t
            elif active and a < off:
                if t - start >= minframes: notes.append((p + 21, start / fps, t / fps))
                active = False
    return notes

wav, out = sys.argv[1], sys.argv[2]
on, off, adapt, sp = float(sys.argv[3]), float(sys.argv[4]), sys.argv[5] == "1", float(sys.argv[6])
y, _ = librosa.load(wav, sr=SR, mono=True)
if HARM: y = librosa.effects.harmonic(y, margin=2.0)
V = np.abs(librosa.cqt(y, sr=SR, hop_length=HOP, fmin=FMIN, n_bins=int(BPO * np.log2(9500 / FMIN)), bins_per_octave=BPO, filter_scale=1.0))
W0 = templates(V.shape[0])
W, H = nmf(V, W0, 200, adapt, sp)
fps = SR / HOP
# median smoothing over 5 frames
from scipy.ndimage import median_filter
H = median_filter(H, size=(1, 5))
with open(out, "w") as f:
    for p, s, e in decode(H, on, off, int(0.115 * fps), fps): f.write(f"{p}|{s:.4f}|{e:.4f}\n")
