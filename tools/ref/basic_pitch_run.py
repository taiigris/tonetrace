# Basic Pitch raw reference run: transcribes WAV files and writes pitch|start|end notes.
import sys, pathlib, soundfile as sf
from basic_pitch.inference import predict
from basic_pitch import ICASSP_2022_MODEL_PATH

def transcribe(wav, out):
    _, _, events = predict(str(wav), ICASSP_2022_MODEL_PATH)
    with open(out, "w") as f:
        for start, end, pitch, amp, _ in sorted(events, key=lambda e: (e[0], e[2])):
            f.write(f"{int(pitch)}|{start:.4f}|{end:.4f}\n")

mode = sys.argv[1]
if mode == "mixes":
    for wav in sorted(pathlib.Path(sys.argv[2]).glob("*.wav")):
        transcribe(wav, wav.with_suffix(".notes"))
elif mode == "track":
    src, outdir, bpm = sys.argv[2], pathlib.Path(sys.argv[3]), 134.0
    info = sf.info(src)
    for start in map(float, sys.argv[4:]):
        frames = int(20 * 60.0 / bpm * info.samplerate)
        data, rate = sf.read(src, start=int(start * info.samplerate), frames=frames, dtype="float32")
        wav = outdir / f"win-{start:.2f}.wav"
        sf.write(wav, data, rate)
        transcribe(wav, wav.with_suffix(".notes"))
        print(f"{wav.with_suffix('.notes')} {frames / rate:.4f}")
