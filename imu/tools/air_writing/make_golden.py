"""Generates golden recordings + float64 reference results for test/host/test_estimator.cpp.

    python tools/air_writing/make_golden.py            # (re)generate test/host/golden/*
"""
from __future__ import annotations

import pathlib

import numpy as np

import reference_trajectory as ref
from simulate_recording import simulate_character, write_raw_csv

OUT = pathlib.Path(__file__).resolve().parents[2] / "test" / "host" / "golden"

CASES = {
    # name: (simulate kwargs, estimator Params kwargs)
    "H_tilt_wobble": (dict(letter="H", seed=11), dict()),
    "O_tilt_wobble": (dict(letter="O", seed=12), dict()),
    "E_no_gyro_feedback": (dict(letter="E", seed=13), dict(mahony_kp=0.0)),
    "W_cutoff_8hz": (dict(letter="W", seed=14), dict(lpf_cutoff_hz=8.0)),
    # all-zero motion: positions must stay ~0
    "stationary": (dict(letter="L", seed=15, height_m=1e-9, wobble_deg=0.0), dict()),
    # gesture still moving at the end of the window: no drift correction expected
    "T_not_at_rest": (dict(letter="T", seed=16, motion_end_s=3.6, wobble_deg=60.0), dict()),
    # large timing jitter (+-9 ms) around the 30 Hz schedule
    "A_jitter": (dict(letter="A", seed=17, jitter_us=9000), dict()),
}


def write_expected(path, rec, params, res):
    with open(path, "w") as f:
        f.write(f"P {params.mahony_kp!r} {params.lpf_cutoff_hz!r}\n")
        for k in ("flags", "fs_hz", "g_ref", "prep_gyro_rms", "prep_acc_rms", "end_gyro_rms",
                  "end_acc_rms", "max_dt_error"):
            f.write(f"S {k} {float(res[k])!r}\n")
        for i in range(len(res["t_s"])):
            row = [res["t_s"][i], *res["q"][i], *res["acc_world"][i], *res["acc_filt"][i]]
            f.write("A " + " ".join(repr(float(x)) for x in row) + "\n")
        for j in range(len(res["tau_s"])):
            row = [res["tau_s"][j], *res["vel"][j], *res["pos"][j]]
            f.write("G " + " ".join(repr(float(x)) for x in row) + "\n")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (sim_kw, par_kw) in CASES.items():
        sim_kw = dict(sim_kw)
        letter = sim_kw.pop("letter")
        rec = simulate_character(letter, **sim_kw)
        params = ref.Params(**par_kw)
        res = ref.estimate_trajectory(rec["t_us"], rec["acc"], rec["gyro"], rec["n_prep"], params)
        write_raw_csv(OUT / f"{name}.raw.csv", rec)
        write_expected(OUT / f"{name}.expected.txt", rec, params, res)
        print(f"{name:22s} flags={res['flags']} max|pos|={np.abs(res['pos']).max():.4f} m")


if __name__ == "__main__":
    main()
