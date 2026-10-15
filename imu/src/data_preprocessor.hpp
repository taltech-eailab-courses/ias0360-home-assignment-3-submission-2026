#ifndef DATA_PREPROCESSOR_HPP
#define DATA_PREPROCESSOR_HPP

#include <cstddef>
#include <cstdint>

#include "preprocessing_settings.hpp"


// TODO: Turns a raw IMU recording into a format that matches the input of the quantized model.
//
// Buffers are (ax, ay, az) interleaved: sample t, axis a is at t * kNumAxes + a.
// `length` always counts samples per axis (time steps), not values.
//
// The individual stages can be called separately; preprocess() runs them as:
//   1. Normalize        raw counts -> g -> (g - mean) / std       (float)
//   2. PadWithTailMode  length < kInputWindowLength -> pad with per-axis mode
//                       of the last kPaddingModeWindow samples
//   3. CutTrailing      length > kInputWindowLength -> drop the last samples
//   4. Downsample       kInputWindowLength -> kTargetLength (block average)
//   5. Quantize         float -> uint8 using the input tensor's scale/zero point
// preprocess() applies the cut (3) before normalizing, so only retained
// samples are normalized; the result is identical.
class DataPreprocessor {
public:
    DataPreprocessor() = default;

    bool preprocess(const int16_t* raw, size_t length, uint8_t* output,
                    float scale = kInputScale,
                    int32_t zero_point = kInputZeroPoint);

    void Normalize(const int16_t* raw, size_t length, float* out) const;

    size_t PadWithTailMode(float* data, size_t length, size_t target_length) const;

    size_t CutTrailing(size_t length, size_t target_length) const;

    void Downsample(const float* in, size_t length, float* out, size_t out_length) const;

    void Quantize(const float* in, size_t count, uint8_t* out,
                  float scale = kInputScale,
                  int32_t zero_point = kInputZeroPoint) const;

private:
    // Work buffer for the padded/cut window.
    float window_[kInputWindowLength * kNumAxes];
    float downsampled_[kTargetLength * kNumAxes];
};

#endif // DATA_PREPROCESSOR_HPP
