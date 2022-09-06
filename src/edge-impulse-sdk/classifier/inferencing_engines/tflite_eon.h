/* Edge Impulse inferencing library
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _EI_CLASSIFIER_INFERENCING_ENGINE_TFLITE_EON_H_
#define _EI_CLASSIFIER_INFERENCING_ENGINE_TFLITE_EON_H_

#include "model-parameters/model_metadata.h"
#if EI_CLASSIFIER_HAS_MODEL_VARIABLES == 1
#include "model-parameters/model_variables.h"
#endif

#if (EI_CLASSIFIER_INFERENCING_ENGINE == EI_CLASSIFIER_TFLITE) && (EI_CLASSIFIER_COMPILED == 1)

#include "edge-impulse-sdk/tensorflow/lite/c/common.h"
#include "edge-impulse-sdk/tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tflite-model/trained_model_compiled.h"
#include "edge-impulse-sdk/classifier/ei_aligned_malloc.h"
#include "edge-impulse-sdk/classifier/ei_fill_result_struct.h"

// old models don't have this, add this here
#ifndef EI_CLASSIFIER_TFLITE_OUTPUT_DATA_TENSOR
#define EI_CLASSIFIER_TFLITE_OUTPUT_DATA_TENSOR 0
#endif // not defined EI_CLASSIFIER_TFLITE_OUTPUT_DATA_TENSOR

#if defined(EI_CLASSIFIER_ENABLE_DETECTION_POSTPROCESS_OP)
namespace tflite {
namespace ops {
namespace micro {
extern TfLiteRegistration Register_TFLite_Detection_PostProcess(void);
}  // namespace micro
}  // namespace ops


extern float post_process_boxes[10 * 4 * sizeof(float)];
extern float post_process_classes[10];
extern float post_process_scores[10];

}  // namespace tflite

static TfLiteRegistration post_process_op = tflite::ops::micro::Register_TFLite_Detection_PostProcess();

#endif // defined(EI_CLASSIFIER_ENABLE_DETECTION_POSTPROCESS_OP)


/**
 * Setup the TFLite runtime
 *
 * @param      ctx_start_us       Pointer to the start time
 * @param      input              Pointer to input tensor
 * @param      output             Pointer to output tensor
 * @param      micro_tensor_arena Pointer to the arena that will be allocated
 *
 * @return  EI_IMPULSE_OK if successful
 */
static EI_IMPULSE_ERROR inference_tflite_setup(uint64_t *ctx_start_us, TfLiteTensor** input, TfLiteTensor** output,
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    TfLiteTensor** output_labels,
    TfLiteTensor** output_scores,
#endif
    ei_unique_ptr_t& p_tensor_arena) {
    TfLiteStatus init_status = trained_model_init(ei_aligned_calloc);
    if (init_status != kTfLiteOk) {
        ei_printf("Failed to allocate TFLite arena (error code %d)\n", init_status);
        return EI_IMPULSE_TFLITE_ARENA_ALLOC_FAILED;
    }

    *ctx_start_us = ei_read_timer_us();

    static bool tflite_first_run = true;

    *input = trained_model_input(EI_CLASSIFIER_TFLITE_OUTPUT_DATA_TENSOR);
    *output = trained_model_output(EI_CLASSIFIER_TFLITE_OUTPUT_DATA_TENSOR);
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    *output_scores = trained_model_output(EI_CLASSIFIER_TFLITE_OUTPUT_SCORE_TENSOR);
    *output_labels = trained_model_output(EI_CLASSIFIER_TFLITE_OUTPUT_LABELS_TENSOR);
#endif // EI_CLASSIFIER_OBJECT_DETECTION

    // Assert that our quantization parameters match the model
    if (tflite_first_run) {
        assert((*input)->type == EI_CLASSIFIER_TFLITE_INPUT_DATATYPE);
        assert((*output)->type == EI_CLASSIFIER_TFLITE_OUTPUT_DATATYPE);
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
        assert((*output_scores)->type == EI_CLASSIFIER_TFLITE_OUTPUT_DATATYPE);
        assert((*output_labels)->type == EI_CLASSIFIER_TFLITE_OUTPUT_DATATYPE);
#endif
#if defined(EI_CLASSIFIER_TFLITE_INPUT_QUANTIZED) || defined(EI_CLASSIFIER_TFLITE_OUTPUT_QUANTIZED)
        if (EI_CLASSIFIER_TFLITE_INPUT_QUANTIZED) {
            assert((*input)->params.scale == EI_CLASSIFIER_TFLITE_INPUT_SCALE);
            assert((*input)->params.zero_point == EI_CLASSIFIER_TFLITE_INPUT_ZEROPOINT);
        }
        if (EI_CLASSIFIER_TFLITE_OUTPUT_QUANTIZED) {
            assert((*output)->params.scale == EI_CLASSIFIER_TFLITE_OUTPUT_SCALE);
            assert((*output)->params.zero_point == EI_CLASSIFIER_TFLITE_OUTPUT_ZEROPOINT);
        }
#endif
        tflite_first_run = false;
    }
    return EI_IMPULSE_OK;
}

/**
 * Run TFLite model
 *
 * @param   ctx_start_us    Start time of the setup function (see above)
 * @param   output          Output tensor
 * @param   interpreter     TFLite interpreter (non-compiled models)
 * @param   tensor_arena    Allocated arena (will be freed)
 * @param   result          Struct for results
 * @param   debug           Whether to print debug info
 *
 * @return  EI_IMPULSE_OK if successful
 */
static EI_IMPULSE_ERROR inference_tflite_run(uint64_t ctx_start_us,
    TfLiteTensor* output,
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    TfLiteTensor* labels_tensor,
    TfLiteTensor* scores_tensor,
#endif
    uint8_t* tensor_arena,
    ei_impulse_result_t *result,
    bool debug) {
    if(trained_model_invoke() != kTfLiteOk) {
        return EI_IMPULSE_TFLITE_ERROR;
    }

    uint64_t ctx_end_us = ei_read_timer_us();

    result->timing.classification_us = ctx_end_us - ctx_start_us;
    result->timing.classification = (int)(result->timing.classification_us / 1000);

    // Read the predicted y value from the model's output tensor
    if (debug) {
        ei_printf("Predictions (time: %d ms.):\n", result->timing.classification);
    }

#if EI_CLASSIFIER_OBJECT_DETECTION_CONSTRAINED == 1
    bool int8_output = output->type == TfLiteType::kTfLiteInt8;
    if (int8_output) {
        fill_result_struct_i8(result, output->data.int8, output->params.zero_point, output->params.scale,
            (int)output->dims->data[1], (int)output->dims->data[2]);
    }
    else {
        fill_result_struct_f32(result, output->data.f,
            (int)output->dims->data[1], (int)output->dims->data[2]);
    }
#elif EI_CLASSIFIER_OBJECT_DETECTION == 1
    fill_result_struct_f32(result, tflite::post_process_boxes, tflite::post_process_scores, tflite::post_process_classes, debug);
    // fill_result_struct_f32(result, output->data.f, scores_tensor->data.f, labels_tensor->data.f, debug);
#else
    bool int8_output = output->type == TfLiteType::kTfLiteInt8;
    if (int8_output) {
        fill_result_struct_i8(result, output->data.int8, output->params.zero_point, output->params.scale, debug);
    }
    else {
        fill_result_struct_f32(result, output->data.f, debug);
    }
#endif

    trained_model_reset(ei_aligned_free);

    if (ei_run_impulse_check_canceled() == EI_IMPULSE_CANCELED) {
        return EI_IMPULSE_CANCELED;
    }

    return EI_IMPULSE_OK;
}


/**
 * @brief      Do neural network inferencing over the processed feature matrix
 *
 * @param      fmatrix  Processed matrix
 * @param      result   Output classifier results
 * @param[in]  debug    Debug output enable
 *
 * @return     The ei impulse error.
 */
EI_IMPULSE_ERROR run_nn_inference(
    ei::matrix_t *fmatrix,
    ei_impulse_result_t *result,
    bool debug = false)
{
    TfLiteTensor* input;
    TfLiteTensor* output;
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    TfLiteTensor* output_scores;
    TfLiteTensor* output_labels;
#endif
    uint64_t ctx_start_us = ei_read_timer_us();
    ei_unique_ptr_t p_tensor_arena(nullptr,ei_aligned_free);

    EI_IMPULSE_ERROR init_res = inference_tflite_setup(&ctx_start_us, &input, &output,
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
        &output_labels,
        &output_scores,
#endif
        p_tensor_arena);

    if (init_res != EI_IMPULSE_OK) {
        return init_res;
    }

    uint8_t* tensor_arena = static_cast<uint8_t*>(p_tensor_arena.get());

    // Place our calculated x value in the model's input tensor
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    bool uint8_input = input->type == TfLiteType::kTfLiteUInt8;
    for (size_t ix = 0; ix < fmatrix->rows * fmatrix->cols; ix++) {
        if (uint8_input) {
            float pixel = (float)fmatrix->buffer[ix];
            input->data.uint8[ix] = static_cast<uint8_t>((pixel / EI_CLASSIFIER_TFLITE_INPUT_SCALE) + EI_CLASSIFIER_TFLITE_INPUT_ZEROPOINT);
        }
        else {
            input->data.f[ix] = fmatrix->buffer[ix];
        }
    }
#else
    bool int8_input = input->type == TfLiteType::kTfLiteInt8;
    for (size_t ix = 0; ix < fmatrix->rows * fmatrix->cols; ix++) {
        // Quantize the input if it is int8
        if (int8_input) {
            input->data.int8[ix] = static_cast<int8_t>(round(fmatrix->buffer[ix] / input->params.scale) + input->params.zero_point);
            // printf("float %ld : %d\r\n", ix, input->data.int8[ix]);
        } else {
            input->data.f[ix] = fmatrix->buffer[ix];
        }
    }
#endif

    EI_IMPULSE_ERROR run_res = inference_tflite_run(ctx_start_us, output,
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
        output_labels,
        output_scores,
#endif
        tensor_arena, result, debug);

    result->timing.classification_us = ei_read_timer_us() - ctx_start_us;

    if (run_res != EI_IMPULSE_OK) {
        return run_res;
    }

    return EI_IMPULSE_OK;
}

#if EI_CLASSIFIER_TFLITE_INPUT_QUANTIZED == 1
/**
 * Special function to run the classifier on images, only works on TFLite models (either interpreter or EON or for tensaiflow)
 * that allocates a lot less memory by quantizing in place. This only works if 'can_run_classifier_image_quantized'
 * returns EI_IMPULSE_OK.
 */
EI_IMPULSE_ERROR run_nn_inference_image_quantized(
    signal_t *signal,
    ei_impulse_result_t *result,
    bool debug = false)
{
    memset(result, 0, sizeof(ei_impulse_result_t));

    uint64_t ctx_start_us;
    TfLiteTensor* input;
    TfLiteTensor* output;
#if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
    TfLiteTensor* output_scores;
    TfLiteTensor* output_labels;
#endif
    ei_unique_ptr_t p_tensor_arena(nullptr,ei_aligned_free);

    EI_IMPULSE_ERROR init_res = inference_tflite_setup(&ctx_start_us, &input, &output,
    #if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
        &output_labels,
        &output_scores,
    #endif
        p_tensor_arena);
    if (init_res != EI_IMPULSE_OK) {
        return init_res;
    }

    if (input->type != TfLiteType::kTfLiteInt8) {
        return EI_IMPULSE_ONLY_SUPPORTED_FOR_IMAGES;
    }

    uint64_t dsp_start_us = ei_read_timer_us();

    // features matrix maps around the input tensor to not allocate any memory
    ei::matrix_i8_t features_matrix(1, EI_CLASSIFIER_NN_INPUT_FRAME_SIZE, input->data.int8);

    // run DSP process and quantize automatically
    int ret = extract_image_features_quantized(signal, &features_matrix, ei_dsp_blocks[0].config, EI_CLASSIFIER_FREQUENCY);
    if (ret != EIDSP_OK) {
        ei_printf("ERR: Failed to run DSP process (%d)\n", ret);
        return EI_IMPULSE_DSP_ERROR;
    }

    if (ei_run_impulse_check_canceled() == EI_IMPULSE_CANCELED) {
        return EI_IMPULSE_CANCELED;
    }

    result->timing.dsp_us = ei_read_timer_us() - dsp_start_us;
    result->timing.dsp = (int)(result->timing.dsp_us / 1000);

    if (debug) {
        ei_printf("Features (%d ms.): ", result->timing.dsp);
        for (size_t ix = 0; ix < features_matrix.cols; ix++) {
            ei_printf_float((features_matrix.buffer[ix] - EI_CLASSIFIER_TFLITE_INPUT_ZEROPOINT) * EI_CLASSIFIER_TFLITE_INPUT_SCALE);
            ei_printf(" ");
        }
        ei_printf("\n");
    }

    ctx_start_us = ei_read_timer_us();

    EI_IMPULSE_ERROR run_res = inference_tflite_run(ctx_start_us, output,
    #if EI_CLASSIFIER_OBJDET_HAS_SCORE_TENSOR
        output_labels,
        output_scores,
    #endif
        static_cast<uint8_t*>(p_tensor_arena.get()),
        result, debug);

    if (run_res != EI_IMPULSE_OK) {
        return run_res;
    }

    result->timing.classification_us = ei_read_timer_us() - ctx_start_us;

    return EI_IMPULSE_OK;
}
#endif // EI_CLASSIFIER_TFLITE_INPUT_QUANTIZED == 1

#endif // (EI_CLASSIFIER_INFERENCING_ENGINE == EI_CLASSIFIER_TFLITE) && (EI_CLASSIFIER_COMPILED == 1)
#endif // _EI_CLASSIFIER_INFERENCING_ENGINE_TFLITE_EON_H_
