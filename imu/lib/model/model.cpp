#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#include "model.h"
#include "model_settings.h"
// TODO: 4. Import your model data
#include "gesture_model_data.h"



Model::Model() :
    model(nullptr),
    interpreter(nullptr),
    input(nullptr),
    error_reporter(nullptr)
{
}

Model::~Model()
{
    if (interpreter != NULL) {
        delete interpreter;
        interpreter = NULL;
    }
    if (model != NULL) {
        delete model;
        model = NULL;
    }
    if (input != NULL) {
        delete input;
        input = NULL;
    }
    if (error_reporter != NULL) {
        delete error_reporter;
        error_reporter = NULL;
    }
}

int Model::setup() 
{
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model = tflite::GetModel(gesture_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return 0;
  }

  static tflite::MicroMutableOpResolver<11> micro_op_resolver;
  // TODO: 7. Add operations according to your model.
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddMaxPool2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddShape();
  micro_op_resolver.AddFullyConnected();
  micro_op_resolver.AddSoftmax();
  micro_op_resolver.AddStridedSlice();
  micro_op_resolver.AddRelu();
  micro_op_resolver.AddPack();
  micro_op_resolver.AddQuantize();
  
  static uint8_t tensor_arena[arena_size];
  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, arena_size);
  interpreter = &static_interpreter;

  // TODO: 8. Allocate tensor
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
    return 0;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

  return 1;
}

uint8_t* Model::input_data() {
  if (input == nullptr) {
    return nullptr;
  }
  return input->data.uint8;
}

int Model::byte_size() {
  if (input == nullptr) {
    return 0;
  }
  return input->bytes;
}

const TfLiteTensor* Model::output_tensor() {
  if (interpreter == nullptr) {
    return nullptr;
  }
  return interpreter->output(0);
}

int Model::predict()
{
  printf("Invocation started\n");
  // TODO: 9. Run invoke inference, if error, return -1
  if (interpreter->Invoke() != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
    return -1;
  }

  printf("Invocation finished\n");

  TfLiteTensor* output = interpreter->output(0);

  int result = -1;
  // TODO: 10. Return an index of the output neuron, which has maximum probability.
  float prev_prob = 0.0;
  for (int i = 0; i < output->dims->data[1]; i++) {
    float probability = (float)output->data.uint8[i] / 255.0;

    // printf("[%d]: %u\t", i, output->data.uint8[i]);
    if (probability > prev_prob) {
      prev_prob = probability;
      result = i;
    }
    // printf("\n");
  }
  return result;
}