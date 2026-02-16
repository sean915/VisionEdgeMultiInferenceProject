#include "TrtInferencer.h"
#include <stdexcept>

// ===== 공통 유틸 =====
static void preprocess_bgr_to_nchw(
    const cv::Mat& bgr,
    int w, int h,
    std::vector<float>& out
) {
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(w, h));

    out.resize(3 * w * h);
    int idx = 0;
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                out[idx++] = resized.at<cv::Vec3b>(y, x)[c] / 255.f;
            }
        }
    }
}

// ===================== Trigger =====================

TrtTriggerInferencer::TrtTriggerInferencer(
    const std::string& enginePath, int w, int h)
    : trt_(enginePath), in_w_(w), in_h_(h)
{
    cudaError_t err;
    err = cudaStreamCreate(&stream_);
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to create CUDA stream");
    }

    err = cudaMalloc(&d_input_, 3 * w * h * sizeof(float));
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to allocate device input memory");
    }

    // Output size는 실제 모델에 맞게 조정 필요
    // 예시: YOLO 출력 크기 (예: 8400 * 85)
    const int OUTPUT_SIZE = 8400 * 85;
    err = cudaMalloc(&d_output_, OUTPUT_SIZE * sizeof(float));
    if (err != cudaSuccess) {
        cudaFree(d_input_);
        cudaStreamDestroy(stream_);
        throw std::runtime_error("Failed to allocate device output memory");
    }

    h_output_.resize(OUTPUT_SIZE);
}

TrtTriggerInferencer::~TrtTriggerInferencer() {
    if (d_input_) {
        cudaFree(d_input_);
    }
    if (d_output_) {
        cudaFree(d_output_);
    }
    if (stream_) {
        cudaStreamDestroy(stream_);
    }
}

TriggerInferenceResult TrtTriggerInferencer::infer(const cv::Mat& bgr) {
    preprocess_bgr_to_nchw(bgr, in_w_, in_h_, h_input_);

    void* bindings[] = { d_input_, d_output_ };

    cudaMemcpyAsync(
        d_input_, h_input_.data(),
        h_input_.size() * sizeof(float),
        cudaMemcpyHostToDevice, stream_);

    trt_.infer(bindings, stream_);

    cudaMemcpyAsync(
        h_output_.data(), d_output_,
        h_output_.size() * sizeof(float),
        cudaMemcpyDeviceToHost, stream_);

    cudaStreamSynchronize(stream_);

    // 👉 기존 YOLO 파서 연결 필요
    // parse_detection_outputs_or_throw(h_output_, result);
    TriggerInferenceResult result;
    // TODO: 실제 파싱 로직 구현
    return result;
}

// ===================== Defect =====================

TrtDefectInferencer::TrtDefectInferencer(
    const std::string& enginePath, int w, int h)
    : trt_(enginePath), in_w_(w), in_h_(h)
{
    cudaError_t err;
    err = cudaStreamCreate(&stream_);
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to create CUDA stream");
    }

    err = cudaMalloc(&d_input_, 3 * w * h * sizeof(float));
    if (err != cudaSuccess) {
        cudaStreamDestroy(stream_);
        throw std::runtime_error("Failed to allocate device input memory");
    }

    err = cudaMalloc(&d_output_, 3 * sizeof(float));
    if (err != cudaSuccess) {
        cudaFree(d_input_);
        cudaStreamDestroy(stream_);
        throw std::runtime_error("Failed to allocate device output memory");
    }
}

TrtDefectInferencer::~TrtDefectInferencer() {
    if (d_input_) {
        cudaFree(d_input_);
    }
    if (d_output_) {
        cudaFree(d_output_);
    }
    if (stream_) {
        cudaStreamDestroy(stream_);
    }
}

void TrtDefectInferencer::infer(
    const cv::Mat& crop,
    float& p_ab,
    float& p_no,
    float& p_em
) {
    preprocess_bgr_to_nchw(crop, in_w_, in_h_, h_input_);

    void* bindings[] = { d_input_, d_output_ };

    cudaMemcpyAsync(
        d_input_, h_input_.data(),
        h_input_.size() * sizeof(float),
        cudaMemcpyHostToDevice, stream_);

    trt_.infer(bindings, stream_);

    float logits[3];
    cudaMemcpyAsync(
        logits, d_output_,
        sizeof(logits),
        cudaMemcpyDeviceToHost, stream_);
    cudaStreamSynchronize(stream_);

    p_ab = logits[0];
    p_no = logits[1];
    p_em = logits[2];
}

