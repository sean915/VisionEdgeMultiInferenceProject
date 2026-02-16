#pragma once
#include <thread>
#include <atomic>
#include "Include/Common/AlgorithmTypes.h"
#include "Include/Listners//IDetector.h"
#include "Include/Listners/IResultListner.h"
#include "Include/Utills/ThreadSafeQueue.h"
#include "Include/Data/Frame.h"
#include <Data/ResultItemC.h>
#include <Ort/OrtSession.h>
#include <Trt/TrtSession.h>
#include <filesystem>

namespace HMSTACK {

using CResultCallback = void(*)(uint64_t frameIndex,
    const ResultItemC* results,
    int errorCode,
    const char* errorMsg,
    const uint8_t* imgData,
    int imgW,
    int imgH,
    int imgStrideBytes,
    int imgCvType);

enum class InputDType {
    FP32,
    FP16,
    UINT8
};

// 분류 정책에 맞게 수정 필요
struct DefectEvalResult {
    std::string final;      // "Abnormal", "Normal", "Questionable", "Error"
    std::string top1;       // "Abnormal", "Normal", "Empty"
    float top1_score = 0.f;
    std::string reason;
};

enum class InferenceBackend {
    ORT_ONNX,
    TRT_ENGINE
};


class Detector : public IDetector 
{

public:
    AlgorithmConfig config_;

    // Detector.h 일부
    std::unique_ptr<OrtSessionWrap> trigger_sess_;
    std::unique_ptr<OrtSessionWrap> defect_sess_;

    std::unique_ptr<TrtEngineWrap> trigger_sess_trt;
    std::unique_ptr<TrtEngineWrap> defect_sess_trt;

    Detector(const AlgorithmConfig& config);
    ~Detector();

    void run();
    void stop();
    void model_setup() override;
    void updateConfig(const std::string&, const std::string&) override {}
    void setCResultCallback(CResultCallback cb);
    AlgorithmStatus getStatus() const;

    int pushFrame(const FrameData& frame);
    enum class CropMode { CELL, PNP, FULL } crop_mode_ = CropMode::CELL;

    enum class UsedRoiKind { CELL, PNP, FULL, PRE_CROP };

    // Detector.h 내부에 추가
    struct DefectJob {
        uint64_t frameIndex = 0;
        cv::Mat frame_bgr;
        cv::Mat crop_bgr;
        struct {
            bool ok = false;
            cv::Rect cell;
            cv::Rect pnp;
            float cell_score = 0.f;
            float pnp_score = 0.f;
        } trig;
        int64_t ts_ms = 0;

        // ✅ 추가: "이번 분류에 실제로 사용한 ROI(원본 frame 기준)"
        cv::Rect used_roi_abs;          // frame 기준 ROI
        UsedRoiKind used_roi_kind = UsedRoiKind::FULL;
    };

private:
    //void inferenceLoop();
    void triggerLoop();
    void defectLoop();
    void loadMetaData(const std::filesystem::path& trigger_meta, const std::filesystem::path& defect_meta);
    bool LoadModelMetaJson(
        const std::filesystem::path& jsonPath,
        std::string& modelName,
        std::string& modelType,
        // input
        std::string& inputName,
        std::string& inputLayout,
        std::string& inputDtype,
        std::string& inputColorOrder,
        std::vector<int64_t>& inputShape,
        bool& inputDynamic,

        // output (bindings[0])
        std::string& outputName,
        std::vector<int64_t>& outputShape,
        std::string& bboxFormat,
        int& classStartIndex,

        // class names
        std::vector<std::string>& classNames,

        // trtexec
        std::string& trtPrecision,
        int& trtWorkspace,

        // derived convenience
        int& modelW,
        int& modelH,

        // ✅ NEW: model file paths
        std::filesystem::path& onnxPathOut,
        std::filesystem::path& enginePathOut,

        std::string* errMsg
    );
private:
    InferenceBackend backend_ = InferenceBackend::TRT_ENGINE; // config로부터 세팅

    // Detector.h (private)
    std::filesystem::path trigger_onnx_path_;
    std::filesystem::path trigger_engine_path_;
    std::filesystem::path defect_onnx_path_;
    std::filesystem::path defect_engine_path_;

     std::filesystem::path modelDir = L"D:/workspace/IVS.SW.INFERENCE.cppNcsharp/InferenceClient/inferenceClient/x64/Debug/stackmagazine_model";

    
    std::thread worker_;
    std::thread worker2_;
    // Detector.cpp 상단 또는 적절한 위치에 evaluate_defect_cpp 함수 정의 추가
   

    std::atomic<bool> running_{false};

    AlgorithmStatus status_{AlgorithmStatus::CREATED};
    CResultCallback c_callback_ = nullptr;

    // DLL 내부에서 소유할 프레임(중요: data 포인터 위험 제거)
    struct OwnedFrame {
        uint64_t index = 0;
        int width = 0;
        int height = 0;
        int cvType = 0;          // 예: CV_8UC3
        int strideBytes = 0;     // step
        int64_t timestamp = 0;
        std::vector<uint8_t> buf; // 이미지 bytes
    };

    ThreadSafeQueue<OwnedFrame> frame_queue_owned_{ 3 }; // 기존 frame_queue_ 대신 사용(권장)

    int trig_in_w_ = 640, trig_in_h_ = 640;    // TODO: 모델 입력에서 읽거나 설정
    int defect_in_w_ = 640, defect_in_h_ = 640;// TODO: 모델 입력에서 읽거나 설정
    InputDType trig_dtype_ = InputDType::FP32; // FP16이면 half 변환 필요
    InputDType defect_dtype_ = InputDType::FP32;

    float cell_min_score_ = 0.25f;
    float pnp_min_score_ = 0.25f;
    int crop_padding_ = 10;

    // per-class Q/AB thresholds (from config_.thresholds)
    float ok_q_thr_  = 0.3f;
    float ok_ab_thr_ = 0.7f;
    float ng_q_thr_  = 0.4f;
    float ng_ab_thr_ = 0.8f;
    float etc_q_thr_ = 0.25f;
    float etc_ab_thr_= 0.7f;


    
    // trigger meta
    std::vector<int64_t> trig_out_shape_{ 1, 6, 8400 };
    int trig_class_start_index_{ 4 };
    std::vector<std::string> trig_class_names_{ "cell", "pnp" };
    std::string trig_bbox_format_{ "xywh" };
    std::string trig_trt_precision_{ "fp32" };

    // defect meta
    std::string defect_trt_precision_{ "fp32" };

    // thread + queue
    std::thread trigger_worker_;
    std::thread defect_worker_;
    ThreadSafeQueue<DefectJob> defect_queue_{ 3 };

};

} // namespace HMSTACK
