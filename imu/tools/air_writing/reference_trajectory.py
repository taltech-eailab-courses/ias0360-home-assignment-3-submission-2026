"""Python reference of the on-device trajectory estimator (src/position_estimator.cpp).

float64 + scipy implementation of the same pipeline, used to validate the
C++ code on identical recordings (test/host/test_estimator.cpp reads the
golden files produced by tools/air_writing/make_golden.py).

Pipeline
--------
1. counts -> m/s^2 and rad/s
2. stationary statistics over the last ``stationary_window`` preparation
   samples (gravity vector, gyro bias, rest check)
3. initial orientation q0 (sensor -> world, [w x y z]): shortest arc taking the
   measured gravity direction to world +Z
4. 6-axis Mahony filter over the gesture samples
5. world linear acceleration R(q) a - [0, 0, |g|]
6. causal Butterworth low-pass (scipy butter + sosfilt, zi = sosfilt_zi * x[0])
   over preparation + gesture - the same filtering as the validated notebook
   (notebooks/visualize_imu_data.ipynb ``estimate_trajectory``)
7. trapezoidal integration with the measured intervals, terminal-velocity
   drift correction (linear trend removal) when start and end are at rest,
   trapezoidal integration to position

With a fixed orientation (gyro = 0, kp = 0) the result equals the notebook's
estimate rotated by the constant R(q0); ``test_notebook_equivalence`` checks it.
"""
from __future__ import annotations

import dataclasses
from typing import Optional

import numpy as np
from scipy.signal import butter, sosfilt, sosfilt_zi

GRAVITY = 9.80665
ACCEL_COUNTS_PER_G = 16384.0
GYRO_COUNTS_PER_DPS = 32.8

SAMPLE_RATE_HZ = 30
PREP_SAMPLES = 60
GESTURE_SAMPLES = 90

FLAG_DRIFT_CORRECTED = 1 << 0
FLAG_START_AT_REST = 1 << 1
FLAG_END_AT_REST = 1 << 2
FLAG_TIMING_IRREGULAR = 1 << 3
FLAG_RESAMPLED = 1 << 4


@dataclasses.dataclass
class Params:
    counts_per_g: float = ACCEL_COUNTS_PER_G
    gyro_counts_per_dps: float = GYRO_COUNTS_PER_DPS
    acc_bias: tuple = (0.0, 0.0, 0.0)
    acc_scale: tuple = (1.0, 1.0, 1.0)
    lpf_cutoff_hz: float = 5.0
    mahony_kp: float = 0.5
    stationary_window: int = 30
    end_rest_window: int = 9
    accel_gate: tuple = (0.85, 1.15)
    gravity_range: tuple = (0.8 * GRAVITY, 1.2 * GRAVITY)
    rest_gyro_rms: float = 0.35
    rest_acc_rms: float = 0.5
    timing_tolerance: float = 0.5


def quat_from_gravity(u: np.ndarray) -> np.ndarray:
    if u[2] < -0.999999:
        return np.array([0.0, 1.0, 0.0, 0.0])
    q = np.array([1.0 + u[2], u[1], -u[0], 0.0])
    return q / np.linalg.norm(q)


def quat_to_matrix(q: np.ndarray) -> np.ndarray:
    q0, q1, q2, q3 = q
    return np.array([
        [1 - 2 * (q2 * q2 + q3 * q3), 2 * (q1 * q2 - q0 * q3), 2 * (q1 * q3 + q0 * q2)],
        [2 * (q1 * q2 + q0 * q3), 1 - 2 * (q1 * q1 + q3 * q3), 2 * (q2 * q3 - q0 * q1)],
        [2 * (q1 * q3 - q0 * q2), 2 * (q2 * q3 + q0 * q1), 1 - 2 * (q1 * q1 + q2 * q2)],
    ])


def mahony_update(q, w, a, g_ref, kp, dt, gate):
    w = np.array(w, dtype=np.float64)
    an = np.linalg.norm(a)
    if gate[0] * g_ref <= an <= gate[1] * g_ref and an > 0:
        u = a / an
        q0, q1, q2, q3 = q
        v = np.array([
            2 * (q1 * q3 - q0 * q2),
            2 * (q0 * q1 + q2 * q3),
            q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3,
        ])
        w = w + kp * np.cross(u, v)
    q0, q1, q2, q3 = q
    dq = 0.5 * np.array([
        -q1 * w[0] - q2 * w[1] - q3 * w[2],
        q0 * w[0] + q2 * w[2] - q3 * w[1],
        q0 * w[1] - q1 * w[2] + q3 * w[0],
        q0 * w[2] + q1 * w[1] - q2 * w[0],
    ])
    q = q + dq * dt
    return q / np.linalg.norm(q)


def estimate_trajectory(t_us, acc_counts, gyro_counts, n_prep=PREP_SAMPLES,
                        params: Optional[Params] = None, correct_drift=True) -> dict:
    """Returns a dict with the same intermediate arrays as ``EstimatorResult``."""
    p = params or Params()
    t_us = np.asarray(t_us, dtype=np.int64)
    n = len(t_us)
    if n < n_prep + 3 or n_prep < 2:
        raise ValueError("too few samples")
    if np.any(np.diff(t_us) <= 0):
        raise ValueError("timestamps must increase")
    n_g = n - n_prep

    t_s = (t_us - t_us[0]) * 1e-6
    span = t_s[-1]
    fs = (n - 1) / span
    mean_dt = span / (n - 1)
    dts = np.diff(t_us) * 1e-6
    max_dt_error = float(np.max(np.abs(dts - mean_dt)) / mean_dt)
    flags = FLAG_TIMING_IRREGULAR if max_dt_error > p.timing_tolerance else 0

    acc = (np.asarray(acc_counts, dtype=np.float64) * GRAVITY / p.counts_per_g
           - np.asarray(p.acc_bias)) * np.asarray(p.acc_scale)
    gyr = np.asarray(gyro_counts, dtype=np.float64) * (np.pi / 180.0) / p.gyro_counts_per_dps

    win = min(p.stationary_window, n_prep)
    w0 = n_prep - win
    acc_stat = acc[w0:n_prep].mean(axis=0)
    gyro_bias = gyr[w0:n_prep].mean(axis=0)
    prep_acc_rms = float(np.sqrt(np.mean(np.sum((acc[w0:n_prep] - acc_stat) ** 2, axis=1))))
    prep_gyro_rms = float(np.sqrt(np.mean(np.sum((gyr[w0:n_prep] - gyro_bias) ** 2, axis=1))))
    g_ref = float(np.linalg.norm(acc_stat))
    if not (p.gravity_range[0] <= g_ref <= p.gravity_range[1]):
        raise ValueError(f"stationary |a| = {g_ref:.3f} m/s^2 outside the plausible range")
    if prep_gyro_rms < p.rest_gyro_rms and prep_acc_rms < p.rest_acc_rms:
        flags |= FLAG_START_AT_REST

    q = quat_from_gravity(acc_stat / g_ref)
    quats = np.empty((n, 4))
    acc_world = np.empty((n, 3))
    g_world = np.array([0.0, 0.0, g_ref])
    for i in range(n):
        if i > n_prep:
            dt = (t_us[i] - t_us[i - 1]) * 1e-6
            q = mahony_update(q, gyr[i] - gyro_bias, acc[i], g_ref, p.mahony_kp, dt, p.accel_gate)
        quats[i] = q
        acc_world[i] = quat_to_matrix(q) @ acc[i] - g_world

    sos = butter(4, p.lpf_cutoff_hz, btype="lowpass", fs=fs, output="sos")
    acc_filt = np.empty_like(acc_world)
    for c in range(3):
        zi = sosfilt_zi(sos) * acc_world[0, c]
        acc_filt[:, c], _ = sosfilt(sos, acc_world[:, c], zi=zi)

    tau = (t_us[n_prep:] - t_us[n_prep]) * 1e-6
    a_g = acc_filt[n_prep:]
    vel = np.zeros((n_g, 3))
    for j in range(1, n_g):
        vel[j] = vel[j - 1] + 0.5 * (a_g[j] + a_g[j - 1]) * (tau[j] - tau[j - 1])

    ew = min(p.end_rest_window, n_g)
    end_gyro_rms = float(np.sqrt(np.mean(np.sum((gyr[-ew:] - gyro_bias) ** 2, axis=1))))
    end_acc_rms = float(np.sqrt(np.mean(np.sum(acc_filt[-ew:] ** 2, axis=1))))
    if end_gyro_rms < p.rest_gyro_rms and end_acc_rms < p.rest_acc_rms:
        flags |= FLAG_END_AT_REST

    need = FLAG_START_AT_REST | FLAG_END_AT_REST
    if correct_drift and (flags & need) == need:
        vel = vel - (tau / tau[-1])[:, None] * vel[-1].copy()
        flags |= FLAG_DRIFT_CORRECTED

    pos = np.zeros((n_g, 3))
    for j in range(1, n_g):
        pos[j] = pos[j - 1] + 0.5 * (vel[j] + vel[j - 1]) * (tau[j] - tau[j - 1])

    return dict(
        flags=flags, fs_hz=fs, g_ref=g_ref, acc_stationary=acc_stat, gyro_bias=gyro_bias,
        prep_gyro_rms=prep_gyro_rms, prep_acc_rms=prep_acc_rms,
        end_gyro_rms=end_gyro_rms, end_acc_rms=end_acc_rms, max_dt_error=max_dt_error,
        t_s=t_s, q=quats, acc_world=acc_world, acc_filt=acc_filt,
        tau_s=tau, vel=vel, pos=pos, n_prep=n_prep,
    )


def resample_positions(pos, tau, m):
    """Linear time resampling to ``m`` points (mirrors resample_positions in C++)."""
    t = np.linspace(0.0, tau[-1], m)
    return np.stack([np.interp(t, tau, pos[:, c]) for c in range(3)], axis=1)


def load_raw_csv(path):
    """Reads ``c<i>_raw.csv`` written by the firmware.

    Columns: t_us,phase,ax,ay,az,gx,gy,gz,qw,qx,qy,qz (phase 0 = preparation).
    Returns t_us, acc_counts, gyro_counts, n_prep, quaternions.
    """
    data = np.loadtxt(path, delimiter=",", skiprows=1)
    n_prep = int(np.sum(data[:, 1] == 0))
    return (data[:, 0].astype(np.int64), data[:, 2:5], data[:, 5:8], n_prep, data[:, 8:12])


if __name__ == "__main__":
    import argparse
    import json

    ap = argparse.ArgumentParser(description="Run the reference estimator on a raw CSV from the SD card")
    ap.add_argument("raw_csv")
    ap.add_argument("--out", help="write positions (t,x,y,z) as CSV")
    args = ap.parse_args()
    t, a, g, npre, _ = load_raw_csv(args.raw_csv)
    r = estimate_trajectory(t, a, g, npre)
    print(json.dumps({k: float(v) for k, v in r.items() if np.isscalar(v)}, indent=2))
    if args.out:
        np.savetxt(args.out, np.column_stack([r["tau_s"], r["pos"]]), delimiter=",",
                   header="t_s,x_m,y_m,z_m", comments="")
