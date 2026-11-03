# Sweeps Basic Pitch decoding thresholds on one window; writes notes per setting.
import sys
from basic_pitch.inference import run_inference, Model
from basic_pitch.note_creation import model_output_to_notes
from basic_pitch import ICASSP_2022_MODEL_PATH
wav, outdir = sys.argv[1], sys.argv[2]
out = run_inference(wav, Model(ICASSP_2022_MODEL_PATH))
for onset in (0.5, 0.3, 0.2):
    for frame in (0.3, 0.2, 0.1):
        for melodia in (True, False):
            _, ev = model_output_to_notes(out, onset_thresh=onset, frame_thresh=frame, min_note_len=11, melodia_trick=melodia)
            name = f"{outdir}/sw-{onset}-{frame}-{int(melodia)}.notes"
            with open(name, "w") as f:
                for s, e, p, a, _ in ev: f.write(f"{int(p)}|{s:.4f}|{e:.4f}\n")
            print(name)
