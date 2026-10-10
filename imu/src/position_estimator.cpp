#include "position_estimator.hpp"

#include <cmath>
#include <cstdlib>

// Cutoff of the acceleration low-pass; override with -DAW_LPF_CUTOFF_HZ=<hz>.
#ifndef AW_LPF_CUTOFF_HZ
#define AW_LPF_CUTOFF_HZ 10
#endif

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMetresPerSecSqPerCount = 9.80665f / 16384.0f;   // +-2 g range
constexpr float kRadPerSecPerCount = kPi / 180.0f / 32.8f;       // +-1000 dps range
constexpr float kLpfTimeConstant = 1.0f / (2.0f * kPi * static_cast<float>(AW_LPF_CUTOFF_HZ));
constexpr float kTiltCorrectionGain = 0.05f;    // weak: the accelerometer also sees the hand's acceleration
constexpr float kTiltTolerance = 0.1f;          // only correct while |a| is within 10 % of gravity
constexpr float kStillGyro = 0.04f;             // rad/s
constexpr float kStillAccel = 0.15f;            // m/s^2, linear acceleration
constexpr int kCalibGyroTolerance = 100;        // raw counts (about 3 dps) from the first sample of the window
constexpr int kCalibAccelTolerance = 600;       // raw counts (about 0.04 g)
}  // namespace

PositionEstimator::PositionEstimator() { reset(); }

void PositionEstimator::reset() {
    calib_count_ = 0;
    calib_seen_ = 0;
    gravity_ = 9.80665f;
    q_[0] = 1.0f; q_[1] = q_[2] = q_[3] = 0.0f;
    has_prev_ = false;
    prev_t_us_ = 0;
    still_count_ = 0;
    for (int i = 0; i < 3; i++) {
        accel_sum_[i] = 0;
        gyro_sum_[i] = 0;
        gyro_bias_[i] = 0.0f;
        accel_filtered_[i] = 0.0f;
        velocity_[i] = 0.0f;
        position_[i] = 0.0f;
    }
}

void PositionEstimator::finishCalibration() {
    const float n = static_cast<float>(calib_count_);
    float f[3];
    for (int i = 0; i < 3; i++) {
        f[i] = static_cast<float>(accel_sum_[i]) / n;
        gyro_bias_[i] = static_cast<float>(gyro_sum_[i]) / n * kRadPerSecPerCount;
    }
    const float norm = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    gravity_ = norm * kMetresPerSecSqPerCount;
    const float ux = f[0] / norm, uy = f[1] / norm, uz = f[2] / norm;

    // World frame: Z = measured "up" (the accelerometer reads +g along the direction opposite to gravity, so
    // this works for any mounting, e.g. +g on the sensor Z axis with the screen down or -g with it up).
    // X = the sensor X axis projected onto the horizontal plane, Y = Z x X. The heading therefore follows the
    // board and cannot flip with tiny tilt noise (a "smallest rotation" would, when the board is face up).
    const float u[3] = {ux, uy, uz};
    float xw[3];
    float xn = 0.0f;
    for (int axis = 0; axis < 2 && xn < 0.2f; axis++) {   // sensor Y only if sensor X is (nearly) vertical
        const float d = u[axis];
        for (int i = 0; i < 3; i++) xw[i] = (i == axis ? 1.0f : 0.0f) - d * u[i];
        xn = std::sqrt(xw[0] * xw[0] + xw[1] * xw[1] + xw[2] * xw[2]);
    }
    for (int i = 0; i < 3; i++) xw[i] /= xn;
    const float yw[3] = {uy * xw[2] - uz * xw[1], uz * xw[0] - ux * xw[2], ux * xw[1] - uy * xw[0]};

    // Rows of the body -> world rotation matrix are the world axes in sensor coordinates.
    const float r00 = xw[0], r01 = xw[1], r02 = xw[2];
    const float r10 = yw[0], r11 = yw[1], r12 = yw[2];
    const float r20 = ux, r21 = uy, r22 = uz;
    const float tr = r00 + r11 + r22;
    if (tr > 0.0f) {
        const float t = std::sqrt(tr + 1.0f) * 2.0f;
        q_[0] = 0.25f * t; q_[1] = (r21 - r12) / t; q_[2] = (r02 - r20) / t; q_[3] = (r10 - r01) / t;
    } else if (r00 > r11 && r00 > r22) {
        const float t = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q_[0] = (r21 - r12) / t; q_[1] = 0.25f * t; q_[2] = (r01 + r10) / t; q_[3] = (r02 + r20) / t;
    } else if (r11 > r22) {
        const float t = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q_[0] = (r02 - r20) / t; q_[1] = (r01 + r10) / t; q_[2] = 0.25f * t; q_[3] = (r12 + r21) / t;
    } else {
        const float t = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q_[0] = (r10 - r01) / t; q_[1] = (r02 + r20) / t; q_[2] = (r12 + r21) / t; q_[3] = 0.25f * t;
    }
}

PositionPoint PositionEstimator::update(const ImuSample& s) {
    PositionPoint p;
    p.t_us = s.t_us;
    const float dt = has_prev_ ? static_cast<float>(s.t_us - prev_t_us_) * 1e-6f : 0.0f;
    has_prev_ = true;
    prev_t_us_ = s.t_us;

    if (calib_count_ < kCalibrationSamples) {
        calib_seen_++;
        bool moved = false;
        if (calib_count_ > 0) {
            for (int i = 0; i < 3; i++) {
                if (std::abs(s.gyro[i] - calib_ref_gyro_[i]) > kCalibGyroTolerance ||
                    std::abs(s.accel[i] - calib_ref_accel_[i]) > kCalibAccelTolerance) {
                    moved = true;
                }
            }
        }
        if (moved) {   // restart the window with this sample
            calib_count_ = 0;
            for (int i = 0; i < 3; i++) { accel_sum_[i] = 0; gyro_sum_[i] = 0; }
        }
        if (calib_count_ == 0) {
            for (int i = 0; i < 3; i++) { calib_ref_accel_[i] = s.accel[i]; calib_ref_gyro_[i] = s.gyro[i]; }
        }
        for (int i = 0; i < 3; i++) {
            accel_sum_[i] += s.accel[i];
            gyro_sum_[i] += s.gyro[i];
        }
        calib_count_++;
        if (calib_count_ == kCalibrationSamples || (calib_seen_ >= kMaxCalibrationSamples && calib_count_ >= 5)) {
            finishCalibration();
            calib_count_ = kCalibrationSamples;   // done
        }
        p.x = p.y = p.z = 0.0f;
        return p;
    }

    float a[3], w[3];
    for (int i = 0; i < 3; i++) {
        a[i] = s.accel[i] * kMetresPerSecSqPerCount;
        w[i] = s.gyro[i] * kRadPerSecPerCount - gyro_bias_[i];
    }
    const float a_norm = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float q0 = q_[0], q1 = q_[1], q2 = q_[2], q3 = q_[3];

    // Tilt correction: cross product of the measured and the predicted gravity direction.
    if (std::fabs(a_norm - gravity_) < kTiltTolerance * gravity_) {
        const float ux = a[0] / a_norm, uy = a[1] / a_norm, uz = a[2] / a_norm;
        const float vx = 2.0f * (q1 * q3 - q0 * q2);
        const float vy = 2.0f * (q0 * q1 + q2 * q3);
        const float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
        w[0] += kTiltCorrectionGain * (uy * vz - uz * vy);
        w[1] += kTiltCorrectionGain * (uz * vx - ux * vz);
        w[2] += kTiltCorrectionGain * (ux * vy - uy * vx);
    }

    const float h = 0.5f * dt;
    const float n0 = q0 + h * (-q1 * w[0] - q2 * w[1] - q3 * w[2]);
    const float n1 = q1 + h * (q0 * w[0] + q2 * w[2] - q3 * w[1]);
    const float n2 = q2 + h * (q0 * w[1] - q1 * w[2] + q3 * w[0]);
    const float n3 = q3 + h * (q0 * w[2] + q1 * w[1] - q2 * w[0]);
    const float inv = 1.0f / std::sqrt(n0 * n0 + n1 * n1 + n2 * n2 + n3 * n3);
    q0 = q_[0] = n0 * inv;
    q1 = q_[1] = n1 * inv;
    q2 = q_[2] = n2 * inv;
    q3 = q_[3] = n3 * inv;

    // World acceleration without gravity (body -> world rotation).
    float aw[3];
    aw[0] = (1.0f - 2.0f * (q2 * q2 + q3 * q3)) * a[0] + 2.0f * (q1 * q2 - q0 * q3) * a[1] + 2.0f * (q1 * q3 + q0 * q2) * a[2];
    aw[1] = 2.0f * (q1 * q2 + q0 * q3) * a[0] + (1.0f - 2.0f * (q1 * q1 + q3 * q3)) * a[1] + 2.0f * (q2 * q3 - q0 * q1) * a[2];
    aw[2] = 2.0f * (q1 * q3 - q0 * q2) * a[0] + 2.0f * (q2 * q3 + q0 * q1) * a[1] + (1.0f - 2.0f * (q1 * q1 + q2 * q2)) * a[2] - gravity_;

    const float w_norm = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    const float lin = std::sqrt(aw[0] * aw[0] + aw[1] * aw[1] + aw[2] * aw[2]);
    still_count_ = (w_norm < kStillGyro && lin < kStillAccel) ? still_count_ + 1 : 0;

    if (still_count_ >= kStillSamples) {
        for (int i = 0; i < 3; i++) {
            velocity_[i] = 0.0f;
            accel_filtered_[i] = 0.0f;
        }
    } else if (dt > 0.0f) {
        const float alpha = dt / (kLpfTimeConstant + dt);
        for (int i = 0; i < 3; i++) {
            const float prev_acc = accel_filtered_[i];
            accel_filtered_[i] += alpha * (aw[i] - accel_filtered_[i]);

            const float prev_vel = velocity_[i];
            velocity_[i] += 0.5f * (prev_acc + accel_filtered_[i]) * dt;
            position_[i] += 0.5f * (prev_vel + velocity_[i]) * dt;
        }
    }

    p.x = position_[0];
    p.y = position_[1];
    p.z = position_[2];
    return p;
}
