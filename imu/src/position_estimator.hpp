#pragma once
#include <cstdint>


typedef enum { SAMPLE_DATA = 1, SAMPLE_CHAR_END, SAMPLE_SESSION_END } SampleType;

typedef struct {
    uint8_t type;        // SampleType
    uint8_t char_index;
    uint8_t char_count;
    uint8_t reserved;
    uint32_t session_id;
    uint32_t t_us;
    int16_t accel[3];    // raw counts, +-2 g (16384 LSB/g)
    int16_t gyro[3];     // raw counts, +-1000 dps (32.8 LSB/dps)
} ImuSample;

typedef struct {
    uint32_t t_us;       // time since the start of the character
    float x, y, z;       // metres from the start position; Z up, X along the sensor X axis at the start
} PositionPoint;


// Streaming 6-axis estimator (accelerometer + gyroscope, no magnetometer): it keeps O(1) state, so every
// sample yields one point that the caller can write out immediately.
//   1. Calibration needs kCalibrationSamples consecutive stationary samples (the window restarts whenever the
//      board moves). They give the gyroscope bias, the gravity magnitude and the initial orientation:
//      world Z = measured "up" (any sign of the sensor Z reading), world X = sensor X projected onto the
//      horizontal plane, world Y = Z x X. Face up (sensor Z reads -g) this makes world Y = -sensor Y. If the board never keeps still, the
//      samples collected after kMaxCalibrationSamples are used anyway. Until then position stays at 0.
//   2. Orientation: quaternion integrated from the gyroscope with the measured time step, with a weak
//      accelerometer correction (it is only a tilt reference while the device is not accelerating).
//   3. World acceleration = R(q) * accel - gravity, low-pass filtered, trapezoidal integration.
//   4. Zero-velocity update: after kStillSamples quiet samples (no rotation, no linear acceleration) the
//      velocity is reset, which stops the drift between strokes and after the character.
class PositionEstimator {
public:
    static constexpr uint32_t kCalibrationSamples = 20;
    static constexpr uint32_t kMaxCalibrationSamples = 150;
    static constexpr uint32_t kStillSamples = 12;

    PositionEstimator();
    void reset();
    PositionPoint update(const ImuSample& sample);
    const float* getPosition() const { return position_; }

private:
    void finishCalibration();

    uint32_t calib_count_;
    uint32_t calib_seen_;           // samples looked at while calibrating, including restarted windows
    int16_t calib_ref_accel_[3];    // first sample of the current window
    int16_t calib_ref_gyro_[3];
    int32_t accel_sum_[3];
    int32_t gyro_sum_[3];
    float gyro_bias_[3];        // rad/s
    float gravity_;             // m/s^2, measured during calibration
    float q_[4];                // body -> world
    bool has_prev_;
    uint32_t prev_t_us_;
    uint32_t still_count_;
    float accel_filtered_[3];
    float velocity_[3];
    float position_[3];
};
