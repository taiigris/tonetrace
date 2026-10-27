# Tonetrace 4.3.0 - performance

Measured by `tonetrace_bench` on this Mac (Version 27.0.1 (Build 26A434)), Release build, universal binary running natively.

## Memory per instance

| Sample rate | prepared (idle) | recording 30 s | recording 120 s (buffer full) | after the analysis of 120 s |
|---|---|---|---|---|
| 44100 Hz | 0.5 MB | 10.9 MB | 37.4 MB | 102.0 MB |
| 48000 Hz | 0.1 MB | 10.7 MB | 44.2 MB | 47.7 MB |
| 96000 Hz | 0.0 MB | 16.2 MB | 65.1 MB | 63.9 MB |
| 192000 Hz | 0.0 MB | 26.8 MB | 108.5 MB | 124.6 MB |

The capture buffer stays at the host rate (one mono float per sample: the audio thread only copies) but is committed lazily, so memory grows with the recording and an empty or idle instance costs only a few MB. Down-sampling in the worker would save the rate-dependent part at 96/192 kHz, but would make the audio thread write into an extra ring and the preview resample on the fly; the measured cost above does not justify that.

## Audio thread (48 kHz, 64 frames per block = 1.33 ms budget)

| Scenario | mean | p99.9 | max |
|---|---|---|---|
| Idle (no Record, no Preview) | 0.0034 % | 0.0063 % | 0.0156 % |
| Record (mono mix into the capture buffer) | 0.0060 % | 0.0094 % | 0.0156 % |
| Preview: recording + 16 synth voices + limiter | 0.4526 % | 1.4969 % | 2.1344 % |
| Preview synced to the host | 0.5602 % | 1.4594 % | 2.1094 % |

| Target | measured | limit | |
|---|---|---|---|
| Idle: mean (only the pass-through bookkeeping) | 0.003 % | < 0.05 % | reached |
| Record / Preview: mean | 0.453 % | < 2 % | reached |
| Record / Preview: peak (best of 3 trials) | 2.134 % | < 10 % | reached |

## Analysis worker (60 s recording at 48 kHz)

- live spectrogram + live HPSS: 2809 frames (2805 with the harmonic part, delay 4 frames = 85 ms), busy time 0.0018 s per audio second, slowest 50 ms chunk 0.20 ms
- live pitch lines (multi-resolution peaks + tracking): 2797 frames, busy time 0.0027 s per audio second
- pitch lines of the final analysis on their own: 0.19 s for 60 s of audio (2848 tracks, tuning +0.4 ct)
- final analysis after Stop (spectrogram, HPSS, peaks, key, stored audio, pitch lines): 0.57 s for 60 s of audio, real-time factor 0.0094
- Quick Grab on its own thread: 0.01 s for 60 s of audio (progress reported up to 0 %; the UI thread stays free)

| Target | measured | limit | |
|---|---|---|---|
| Live analysis (spectrogram + live HPSS): real-time factor | 0.002  | < 0.1  | reached |
| Live pitch lines: real-time factor | 0.003  | < 0.1  | reached |
| Final analysis: real-time factor | 0.009  | < 0.1  | reached |
| Quick Grab: real-time factor | 0.000  | < 0.1  | reached |

## Editor (1100 x 800, 120 s recording)

- frame with a cold tile cache (first draw): 35.53 ms; scrolling a cached map (whole window, median of 60): 5.67 ms, map draw only 0.55 ms; after a Gain / Range change (all visible tiles re-rendered): 19.91 ms
- one 128 px glow tile (2x, band 685 pt high): 0.290 ms
- one 128 px spectrum tile (2x, 1275 pt high, drums and overtones dimmed): 0.362 ms
- one idle refresh tick: 0.005 ms CPU -> 0.002 % of a core at 5 Hz (the timer is off while the window is hidden)
- while recording 10 s (refresh + whole-window draw of a growing map every 0.5 s): median 6.44 ms, slowest 21.60 ms (the first frame after Record, when every tile is cold)

| Target | measured | limit | |
|---|---|---|---|
| Frame time, 1100x800 with 120 s recording (cached tiles) | 5.672 ms | < 8 ms | reached |
| Frame time while recording (median) | 6.439 ms | < 8 ms | reached |
| Editor idle CPU | 0.002 % | < 1 % | reached |

All targets reached.
