#include "data_preprocessor.hpp"

#include <algorithm>
#include <cmath>

void DataPreprocessor::Normalize(const int16_t* raw, size_t length, float* out) const {

}

size_t DataPreprocessor::PadWithTailMode(float* data, size_t length, size_t target_length) const {
  return 0;
}

size_t DataPreprocessor::CutTrailing(size_t length, size_t target_length) const {
  return 0;
}

void DataPreprocessor::Downsample(const float* in, size_t length, float* out,
                                  size_t out_length) const {
}

void DataPreprocessor::Quantize(const float* in, size_t count, uint8_t* out, float scale,
                                int32_t zero_point) const {

}

// TODO: Implement IMU preprocessor and set the data to the model's input tensor
bool DataPreprocessor::preprocess(const int16_t* raw, size_t length, uint8_t* output,
                                  float scale, int32_t zero_point) {
  if (raw == nullptr || output == nullptr || length == 0) {
    return false;
  }

  return true;
}
