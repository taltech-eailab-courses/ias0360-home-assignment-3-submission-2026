"""Synthetic IMU recordings of air-written letters (test fixture / end-to-end simulator).

A letter is a single continuous polyline (no pen-up information on the device)
in the drawing plane X-Z of the estimator's world frame (X right, Z up, writer
looking along +Y). The hand follows the smoothed path with a minimum-jerk time
law, so the gesture starts and ends at rest. The sensor is tilted by a constant
mounting angle about a horizontal axis and wobbles about a world axis during the
gesture, so quaternion tracking is exercised. Raw counts are generated exactly as
the ICM-20948 would report them (+-2 g, +-1000 dps) with noise, gyro bias and
jittered timestamps.
"""
from __future__ import annotations

import numpy as np
from scipy.interpolate import CubicSpline, PchipInterpolator
from scipy.signal import butter, lfilter, lfilter_zi

from reference_trajectory import (ACCEL_COUNTS_PER_G, GESTURE_SAMPLES, GRAVITY,
                                  GYRO_COUNTS_PER_DPS, PREP_SAMPLES, SAMPLE_RATE_HZ)

LETTERS = {
    "H": [(0, 1), (0, 0), (0, .5), (.7, .5), (.7, 1), (.7, 0)],
    "E": [(.7, 1), (0, 1), (0, .5), (.6, .5), (0, .5), (0, 0), (.7, 0)],
    "L": [(0, 1), (0, 0), (.6, 0)],
    "T": [(0, 1), (.7, 1), (.35, 1), (.35, 0)],
    "A": [(0, 0), (.35, 1), (.7, 0), (.55, .35), (.15, .35)],
    "N": [(0, 0), (0, 1), (.7, 0), (.7, 1)],
    "V": [(0, 1), (.35, 0), (.7, 1)],
    "Z": [(0, 1), (.7, 1), (0, 0), (.7, 0)],
    "W": [(0, 1), (.2, 0), (.4, .6), (.6, 0), (.8, 1)],
    "U": [(0, 1), (0, .25), (.1, .05), (.35, 0), (.6, .05), (.7, .25), (.7, 1)],
}


def _ellipse(n=64, rx=.4, ry=.5):
    a = np.linspace(0, 2 * np.pi, n)
    return [(.4 + rx * np.sin(t), .5 + ry * np.cos(t)) for t in a]


LETTERS["O"] = _ellipse()


def letter_path(letter: str, height_m: float, samples: int = 800) -> np.ndarray:
    """Smoothed dense (samples, 2) path, bottom of the glyph at z = 0, height ``height_m``."""
    pts = np.asarray(LETTERS[letter.upper()], dtype=float)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    u = np.linspace(0, s[-1], samples)
    dense = np.stack([np.interp(u, s, pts[:, 0]), np.interp(u, s, pts[:, 1])], axis=1)
    # Gaussian smoothing with end-point reflection keeps the start/end fixed.
    sigma = max(1.0, 0.03 * samples)
    k = np.arange(-int(4 * sigma), int(4 * sigma) + 1)
    w = np.exp(-0.5 * (k / sigma) ** 2)
    w /= w.sum()
    pad = len(k) // 2
    out = np.empty_like(dense)
    for c in range(2):
        x = dense[:, c]
        padded = np.concatenate([2 * x[0] - x[1:pad + 1][::-1], x, 2 * x[-1] - x[-pad - 1:-1][::-1]])
        out[:, c] = np.convolve(padded, w, mode="valid")
    return out * height_m


def _minjerk(u):
    return 10 * u ** 3 - 15 * u ** 4 + 6 * u ** 5


def _rodrigues(axis, angle):
    axis = np.asarray(axis, dtype=float)
    axis = axis / np.linalg.norm(axis)
    K = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    return np.eye(3) + np.sin(angle) * K + (1 - np.cos(angle)) * K @ K


def simulate_character(letter: str, seed: int = 0, height_m: float = 0.12, tilt_deg: float = 20.0,
                       tilt_axis=(1.0, 0.5, 0.0), wobble_deg: float = 8.0, motion_start_s: float = 0.3,
                       motion_end_s: float = 2.5, noise: bool = True, jitter_us: int = 300,
                       gyro_bias_dps=(0.4, -0.3, 0.2), a_max_mps2: float = 3.0,
                       hw_dlpf_hz: float = 5.7, oversample: int = 4) -> dict:
    rng = np.random.default_rng(seed)
    n_total = PREP_SAMPLES + GESTURE_SAMPLES
    k = np.arange(n_total)
    # Each 30 Hz sample is the mean of `oversample` reads spaced 1/(30*oversample) s apart (as in
    # src/aw_imu.cpp); its timestamp is their centroid. Jitter moves a whole tick.
    t_tick = (k * 1_000_000 // SAMPLE_RATE_HZ).astype(np.int64)
    if jitter_us:
        t_tick[1:] += rng.integers(-jitter_us, jitter_us + 1, size=n_total - 1)
    sub = np.arange(oversample) * (1_000_000 // (SAMPLE_RATE_HZ * oversample))
    t_sub_us = t_tick[:, None] + sub[None, :]
    t_us = np.rint(t_sub_us.mean(axis=1)).astype(np.int64)
    t_s = (t_us - t_us[0]) * 1e-6
    tg = t_s - t_s[PREP_SAMPLES]  # gesture clock, negative during preparation
    tg_sub = (t_sub_us - t_us[PREP_SAMPLES]) * 1e-6

    path = letter_path(letter, height_m)
    seg = np.linalg.norm(np.diff(path, axis=0), axis=1)
    arc = np.concatenate([[0], np.cumsum(seg)])

    spl_x = CubicSpline(arc, path[:, 0])
    spl_z = CubicSpline(arc, path[:, 1])

    # Corner-aware time law: the hand slows down where the path turns sharply
    # (speed limited by a_max / curvature) and eases in/out at the ends.
    ds = np.diff(arc)
    ang = np.unwrap(np.arctan2(np.diff(path[:, 1]), np.diff(path[:, 0])))
    curv = np.abs(np.diff(ang)) / (0.5 * (ds[1:] + ds[:-1]))
    curv = np.concatenate([[curv[0]], curv, [curv[-1]]])
    k_s = np.convolve(curv, np.ones(15) / 15, mode="same")
    from scipy.ndimage import gaussian_filter1d, minimum_filter1d
    window = motion_end_s - motion_start_s

    def time_law(a_max):
        v = np.minimum(2.0, np.sqrt(a_max / np.maximum(k_s, 1e-6)))
        # widen the slow zones, then smooth so that dv/ds (acceleration) stays bounded
        v = gaussian_filter1d(minimum_filter1d(v, 61, mode="nearest"), 20, mode="nearest")
        # constant-acceleration start/stop: v = sqrt(2 a s) is zero at the ends
        v = np.minimum(v, np.sqrt(2 * a_max * np.minimum(arc, arc[-1] - arc)))
        v = np.maximum(v, 1e-6)
        return np.concatenate([[0], np.cumsum(2 * ds / (v[1:] + v[:-1]))])

    # Choose the gentlest acceleration limit for which the letter fits the motion window.
    a_max = a_max_mps2
    for _ in range(40):
        t_of_s = time_law(a_max)
        a_max *= (t_of_s[-1] / window) ** 2
    t_of_s = time_law(a_max)
    t_of_s *= window / t_of_s[-1]

    s_of_t = PchipInterpolator(t_of_s, arc, extrapolate=False)

    def pos_world(t):
        t = np.asarray(t, dtype=float)
        tt = np.clip(t - motion_start_s, 0.0, t_of_s[-1])
        s = s_of_t(tt)
        x = spl_x(s)
        z = spl_z(s)
        return np.stack([x - path[0, 0], np.zeros_like(x), z - path[0, 1]], axis=-1)

    h = 1e-3
    r_tilt = _rodrigues(tilt_axis, np.deg2rad(tilt_deg))  # horizontal axis: shortest-arc tilt
    assert abs(tilt_axis[2]) < 1e-12, "tilt axis must be horizontal"

    def r_ws(t):
        u = np.clip((t - motion_start_s) / (motion_end_s - motion_start_s), 0, 1)
        ang = np.deg2rad(wobble_deg) * np.sin(np.pi * u) ** 2
        return _rodrigues((0.3, 1.0, 0.4), ang) @ r_tilt

    # Sensor outputs on a 1 kHz grid, then the ICM-20948's on-chip low-pass
    # (DLPFCFG 6, ~5.7 Hz) before they are sampled at the jittered tick times.
    t_fine = np.arange(tg_sub.min() - 1.0, tg_sub.max() + 0.05, h)
    a_world = (pos_world(t_fine + h) - 2 * pos_world(t_fine) + pos_world(t_fine - h)) / (h * h)
    acc_f = np.empty((len(t_fine), 3))
    gyr_f = np.empty((len(t_fine), 3))
    for i, t in enumerate(t_fine):
        r = r_ws(t)
        acc_f[i] = r.T @ (a_world[i] + np.array([0, 0, GRAVITY]))
        om = r.T @ (r_ws(t + h) - r_ws(t - h)) / (2 * h)  # skew matrix of body rates
        gyr_f[i] = [om[2, 1], om[0, 2], om[1, 0]]
    if hw_dlpf_hz:
        b_hw, a_hw = butter(2, hw_dlpf_hz, fs=1.0 / h)
        for arr in (acc_f, gyr_f):
            for c in range(3):
                arr[:, c], _ = lfilter(b_hw, a_hw, arr[:, c], zi=lfilter_zi(b_hw, a_hw) * arr[0, c])
    def sample(arr):
        flat = np.stack([np.interp(tg_sub.ravel(), t_fine, arr[:, c]) for c in range(3)], axis=1)
        return flat.reshape(n_total, oversample, 3)

    acc_sub, gyr_sub = sample(acc_f), sample(gyr_f)
    if noise:  # independent noise on every read, reduced by the averaging
        acc_sub = acc_sub + rng.normal(0, 8.0 * GRAVITY / ACCEL_COUNTS_PER_G, acc_sub.shape)  # counts -> m/s^2
        gyr_sub = gyr_sub + rng.normal(0, np.deg2rad(2.0 / GYRO_COUNTS_PER_DPS), gyr_sub.shape)
    acc_s, gyr_s = acc_sub.mean(axis=1), gyr_sub.mean(axis=1)

    acc_counts = acc_s * ACCEL_COUNTS_PER_G / GRAVITY
    gyro_counts = np.rad2deg(gyr_s) * GYRO_COUNTS_PER_DPS + np.asarray(gyro_bias_dps) * GYRO_COUNTS_PER_DPS
    acc_counts = np.clip(np.round(acc_counts), -32768, 32767).astype(np.int16)
    gyro_counts = np.clip(np.round(gyro_counts), -32768, 32767).astype(np.int16)

    truth = pos_world(tg[PREP_SAMPLES:])
    return dict(t_us=t_us, acc=acc_counts, gyro=gyro_counts, n_prep=PREP_SAMPLES, truth_pos=truth,
                letter=letter)


def write_raw_csv(path, rec):
    """Same layout as the firmware's c<i>_raw.csv (quaternion columns left at identity)."""
    n = len(rec["t_us"])
    with open(path, "w") as f:
        f.write("t_us,phase,acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z,qw,qx,qy,qz\n")
        for i in range(n):
            phase = 0 if i < rec["n_prep"] else 1
            a, g = rec["acc"][i], rec["gyro"][i]
            f.write(f"{rec['t_us'][i]},{phase},{a[0]},{a[1]},{a[2]},{g[0]},{g[1]},{g[2]},1,0,0,0\n")
