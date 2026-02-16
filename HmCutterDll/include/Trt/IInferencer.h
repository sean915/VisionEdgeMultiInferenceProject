#pragma once

#include <opencv2/opencv.hpp>
#include <string>

struct TriggerInferenceResult {
    bool has_cell = false;
    cv::Rect cell;
    float cell_score = 0.f;

    bool has_pnp = false;
    cv::Rect pnp;
    float pnp_score = 0.f;
};

class ITriggerInferencer {
public:
    virtual ~ITriggerInferencer() = default;
    virtual TriggerInferenceResult infer(const cv::Mat& bgr) = 0;
};

class IDefectInferencer {
public:
    virtual ~IDefectInferencer() = default;
    virtual void infer(
        const cv::Mat& crop,
        float& p_ab,
        float& p_no,
        float& p_em
    ) = 0;
};

enum class InferenceBackend {
    ORT,
    TRT
};

