#ifndef TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_
#define TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_

#define GESTURE_DATA_IMU_AXIS 3 // Typically, IMU has 3 axes: x, y, z.
// TODO: Adjust the maximum length of gesture data sequence according to your model requirements.
#define GESTURE_DATA_MAX_LEN 128 // Maximum length of gesture data sequence.
#define MAX_GESTURE_RAW_DATA_LEN 4000

#define CLASS_COUNT 4
#define CLASS_LABELS {"Negative (Static)", "Ring (Circle)", "Slope (L-shape)", "Wing (W-shape)"}

constexpr int arena_size = 0; // TODO: Edit this for your own model.
#endif  // TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_
