# Tonetrace 4.0.0 - performance

Measured by `tonetrace_bench` on this Mac (Version 27.0.1 (Build 26A434)), Release build, universal binary running natively.

## Memory per instance

| Sample rate | prepared (idle) | recording 30 s | recording 120 s (buffer full) | after the analysis of 120 s |
|---|---|---|---|---|
| 44100 Hz | 0.2 MB | 10.6 MB | 40.5 MB | 91.1 MB |
| 48000 Hz | 0.0 MB | 10.7 MB | 37.9 MB | 56.0 MB |
| 96000 Hz | 0.0 MB | 16.2 MB | 64.5 MB | 66.7 MB |
| 192000 Hz | 0.0 MB | 29.0 MB | 107.8 MB | 100.3 MB |

The capture buffer stays at the host rate (one mono float per sample: the audio thread only copies) but is committed lazily, so memory grows with the recording and an empty or idle instance costs only a few MB. Down-sampling in the worker would save the rate-dependent part at 96/192 kHz, but would make the audio thread write into an extra ring and the preview resample on the fly; the measured cost above does not justify that.

## Audio thread (48 kHz, 64 frames per block = 1.33 ms budget)

| Scenario | mean | p99.9 | max |
|---|---|---|---|
| Idle (no Record, no Preview) | 0.0036 % | 0.0125 % | 0.0156 % |
| Record (mono mix into the capture buffer) | 0.0060 % | 0.0125 % | 0.0156 % |
| Preview: recording + 16 synth voices + limiter | 0.4578 % | 1.2875 % | 1.9625 % |
| Preview synced to the host | 0.5467 % | 1.4594 % | 1.9031 % |

| Target | measured | limit | |
|---|---|---|---|
| Idle: mean (only the pass-through bookkeeping) | 0.004 % | < 0.05 % | reached |
| Record / Preview: mean | 0.458 % | < 2 % | reached |
| Record / Preview: peak (best of 3 trials) | 1.962 % | < 10 % | reached |

## Analysis worker (60 s recording at 48 kHz)

- live spectrogram + live HPSS: 2809 frames (2805 with the harmonic part, delay 4 frames = 85 ms), busy time 0.0018 s per audio second, slowest 50 ms chunk 0.16 ms
- final analysis after Stop (spectrogram, HPSS, peaks, key, stored audio): 0.21 s for 60 s of audio, real-time factor 0.0035
- Quick Grab on its own thread: 0.01 s for 60 s of audio (progress reported up to 0 %; the UI thread stays free)

| Target | measured | limit | |
|---|---|---|---|
| Live analysis (spectrogram + live HPSS): real-time factor | 0.002  | < 0.1  | reached |
| Final analysis: real-time factor | 0.004  | < 0.1  | reached |
| Quick Grab: real-time factor | 0.000  | < 0.1  | reached |

## Editor (1100 x 640, 120 s recording)

- frame with a cold tile cache (first draw): 26.13 ms; scrolling a cached map (whole window, median of 60): 4.54 ms, map draw only 0.33 ms; after a Gain / Range change (all visible tiles re-rendered): 12.76 ms
- one 128 px heatmap tile (full note range): 0.083 ms
- one idle refresh tick: 0.004 ms CPU -> 0.002 % of a core at 5 Hz (the timer is off while the window is hidden)
- while recording 10 s (refresh + whole-window draw of a growing map every 0.5 s): median 4.69 ms, slowest 14.81 ms (the first frame after Record, when every tile is cold)

| Target | measured | limit | |
|---|---|---|---|
| Frame time, 1100x640 with 120 s recording (cached tiles) | 4.535 ms | < 8 ms | reached |
| Frame time while recording (median) | 4.685 ms | < 8 ms | reached |
| Editor idle CPU | 0.002 % | < 1 % | reached |

All targets reached.
