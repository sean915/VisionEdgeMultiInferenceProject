#pragma once

#include "IInferencer.h"
#include "TrtEngine.h"
#include <cuda_runtime.h>
#include <vector>

class TrtTriggerInferencer : public ITriggerInferencer {
public:
    TrtTriggerInferencer(const std::string& enginePath, int w, int h);
    ~TrtTriggerInferencer();

    TriggerInferenceResult infer(const cv::Mat& bgr) override;

private:
    TrtEngine trt_;
    int in_w_, in_h_;

    cudaStream_t stream_{};
    float* d_input_{nullptr};
    float* d_output_{nullptr};

    std::vector<float> h_input_;
    std::vector<float> h_output_;
};

class TrtDefectInferencer : public IDefectInferencer {
public:
    TrtDefectInferencer(const std::string& enginePath, int w, int h);
    ~TrtDefectInferencer();

    void infer(
        const cv::Mat& crop,
        float& p_ab,
        float& p_no,
        float& p_em
    ) override;

private:
    TrtEngine trt_;
    int in_w_, in_h_;

    cudaStream_t stream_{};
    float* d_input_{nullptr};
    float* d_output_{nullptr};

    std::vector<float> h_input_;
};

