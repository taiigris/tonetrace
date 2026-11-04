# Tonetrace 5.1.0 - performance

Measured by `tonetrace_bench` on this Mac (Version 27.0.1 (Build 26A434)), Release build, universal binary running natively. The transcription itself (MuScriptor) runs on its own thread and is measured by `tonetrace_mstest` (about a third of the song length on Apple Silicon).

## Memory (one instance)

| Case | Measured | Target | |
|---|---|---|---|
| Instance with a 10 minute song, right after loading (decoder buffers not yet returned) | 194.7 MB | | |
| Empty instance (record buffers, note tables) | 6.1 MB | | |
| Instance with a 10 minute song loaded | 194.656 MB | < 250 MB | reached |

## Audio thread (48 kHz, 64-frame blocks, % of the block time)

| Case | Measured | Target | |
|---|---|---|---|
| Idle (pass-through), mean | 0.020 % | < 5 % | reached |
| Idle (pass-through), peak | 0.150 % | < 25 % | reached |
| Recording, mean | 0.022 % | < 5 % | reached |
| Recording, peak | 0.641 % | < 25 % | reached |
| Playing song + notes while they stream in, mean | 0.023 % | < 5 % | reached |
| Playing song + notes while they stream in, peak | 0.062 % | < 25 % | reached |
| Playing song + all notes (5 instruments), mean | 0.265 % | < 5 % | reached |
| Playing song + all notes (5 instruments), peak | 1.459 % | < 25 % | reached |

## Editor (1280 x 800, the demo song with 5 instruments, playing)

| Case | Measured | Target | |
|---|---|---|---|
| Full frame (median) | 4.303 ms | < 16 ms | reached |

All targets reached.
