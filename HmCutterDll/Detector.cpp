#include "Detector.h"
#include "Include/Data/DetectOutputDto.h"
#include "Include/Data/ResultItemC.h"

#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>   
#include <cstdio>    
#include <iostream>
#include <opencv2/imgproc.hpp>

//json
#include <Include/nlohmann/json.hpp>

#include <string>
#include <fstream>
#include <filesystem>

// TRT
#include <Trt/TrtSession.h>

#include "DetectorHelpers.h"
#include "DetectorPreprocessing.h"
#include "DetectorParsers.h"
#include "DetectorPostprocessing.h"

namespace HmCutter
{
    Detector::Detector(const AlgorithmConfig& config)
        : config_(config)
    {
        modelDir = config.baseDirPath ? std::filesystem::path(config.baseDirPath) : modelDir;
        // Apply per-class Q/AB thresholds from config
        ok_q_thr_   = config.thresholds.okQThreshold;
        ok_ab_thr_  = config.thresholds.okAbThreshold;
        ng_q_thr_   = config.thresholds.ngQThreshold;
        ng_ab_thr_  = config.thresholds.ngAbThreshold;
        etc_q_thr_  = config.thresholds.etcQThreshold;
        etc_ab_thr_ = config.thresholds.etcAbThreshold;
    }

    Detector::~Detector() {
        running_ = false;

        if (trigger_worker_.joinable()) trigger_worker_.join();
        if (defect_worker_.joinable())  defect_worker_.join();
    }


    void Detector::setCResultCallback(CResultCallback cb) {
        c_callback_ = cb;
    }

    AlgorithmStatus Detector::getStatus() const
    {
        return status_;
    }


    // ------------------------------------------------------------
    // model_setup (ORT / TRT 분기)
    // ------------------------------------------------------------
    // 모델 메타(JSON) 로드 + 세션 생성 + 워커 스레드 시작
    void Detector::model_setup() {
        status_ = AlgorithmStatus::INITIALIZING;

        try {
            // const std::filesystem::path modelDir = GetExeDirPath() / L"stackmagazine_model";

            std::filesystem::path trigger_meta = modelDir / L"Trigger_Cathode_V0.1.0.json";
            std::filesystem::path defect_meta = modelDir / L"Classifier_Cathode_V0.1.0.json";

            if (!std::filesystem::exists(trigger_meta)) {
                auto alt = modelDir / L"Trigger_Cathode_V0.1.0.json";
                if (std::filesystem::exists(alt)) trigger_meta = alt;
            }
            if (!std::filesystem::exists(defect_meta)) {
                auto alt = modelDir / L"Classifier_Cathode_V0.1.0.json";

                if (std::filesystem::exists(alt)) defect_meta = alt;
            }

            // meta 로드(여기서 onnx/engine 경로까지 채워짐)
            loadMetaData(trigger_meta, defect_meta);

            // 기존 세션 정리(재호출 대비)
            trigger_sess_.reset();
            defect_sess_.reset();
            trigger_sess_trt.reset();
            defect_sess_trt.reset();

            // meta에서 읽은 경로로 로드
            if (backend_ == InferenceBackend::TRT_ENGINE) {
                trigger_sess_trt = std::make_unique<TrtEngineWrap>(trigger_engine_path_.wstring(), true);
                defect_sess_trt = std::make_unique<TrtEngineWrap>(defect_engine_path_.wstring(), true);
            }
            else {
                trigger_sess_ = std::make_unique<OrtSessionWrap>(trigger_onnx_path_.wstring(), true);
                defect_sess_ = std::make_unique<OrtSessionWrap>(defect_onnx_path_.wstring(), true);
            }
        }
        catch (const Ort::Exception& e) {
            status_ = AlgorithmStatus::ERROR;
            std::cout << "[ORT EXCEPTION] " << e.what() << "\n";
            return;
        }
        catch (const std::exception& e) {
            status_ = AlgorithmStatus::ERROR;
            std::cout << "[EXCEPTION] " << e.what() << "\n";
            return;
        }

        status_ = AlgorithmStatus::READY;
    }

    void Detector::run() {
        if (running_ || status_ != AlgorithmStatus::READY) return;

        running_ = true;
        trigger_worker_ = std::thread(&Detector::triggerLoop, this);
        defect_worker_ = std::thread(&Detector::defectLoop, this);
    }

    void Detector::stop() {
        if (!running_) {
            if (status_ == AlgorithmStatus::RUNNING) {
                status_ = AlgorithmStatus::READY;
            }
            return;
        }

        running_ = false;
        if (trigger_worker_.joinable()) trigger_worker_.join();
        if (defect_worker_.joinable()) defect_worker_.join();
        status_ = AlgorithmStatus::READY;
    }

    // ------------------------------------------------------------
    // pushFrame
    // ------------------------------------------------------------
    // 외부에서 받은 FrameData를 내부 큐로 복사 저장
    int Detector::pushFrame(const FrameData& frame) {
        if (status_ != AlgorithmStatus::READY && status_ != AlgorithmStatus::RUNNING)
            return -1;

        // 입력은 BGR 8UC3, 연속 메모리 가정
        const int cvType = CV_8UC3;
        const int bytesPerPixel = 3;
        const int stride = frame.width * bytesPerPixel;
        const size_t totalBytes = static_cast<size_t>(stride) * static_cast<size_t>(frame.height);

        OwnedFrame of;
        of.index = frame.index;
        of.width = frame.width;
        of.height = frame.height;
        of.cvType = cvType;
        of.strideBytes = stride;
        of.timestamp = frame.timestamp;
        of.buf.resize(totalBytes);

        if (!frame.data || frame.width <= 0 || frame.height <= 0) return -1;
        std::memcpy(of.buf.data(), frame.data, totalBytes);

        frame_queue_owned_.push(std::move(of));
        return 0;
    }




    // ResultItem -> C API용 ResultItemC 변환
    static inline ResultItemC ToCResult(const HmCutter::ResultItem& r)
    {
        ResultItemC c{};
        c.defect_type = static_cast<int>(r.defect_type); // NONE=0, NG=1, OK=2
        c.score = r.score;

        c.box.x1 = r.box.x1;
        c.box.y1 = r.box.y1;
        c.box.x2 = r.box.x2;
        c.box.y2 = r.box.y2;
        return c;
    }

    // ------------------------------------------------------------
    // triggerLoop (ORT/TRT 분기)
    // ------------------------------------------------------------
    // 프레임 입력 -> 트리거 탐지 -> defect 큐로 전달 또는 skip 처리
    void Detector::triggerLoop()
    {
        status_ = AlgorithmStatus::RUNNING;

        std::vector<uint16_t> fp16buf;
        std::vector<float>    fp32buf;
        std::vector<int64_t>  outShape;

        // meta에서 읽어온 입력 크기 사용
        const int modelW = trig_in_w_;
        const int modelH = trig_in_h_;

        // config에서 읽어온 임계값 사용
        const float conf_thr = cell_min_score_;
        const float nms_iou_thr = 0.45f;

        // ===== Python enable_tracking=True 기준 동작 구현 =====
        // (C++ 쪽 config에 enable_tracking 플래그가 없어서, 제공된 Python main과 동일하게 "tracking on"으로 구현)
        const bool enable_tracking = true;

        // Python tracking 파라미터 (기본/사용 예와 동일)
        const float horn_direction_threshold = 2.0f; // px
        const int   tracking_max_age = 10;

        enum class HornDir { None, Up, Down };
        bool   has_last_horn = false;
        float  last_horn_center_y = 0.f;
        HornDir last_dir = HornDir::None;

        // Python의 frame_idx 대신 처리 카운터(스트리밍에서 frame.index가 연속이 아닐 수 있음)
        int64_t frame_count = 0;

        while (running_) {
            OwnedFrame of;
            if (!frame_queue_owned_.pop(of)) continue;

            ++frame_count;

            cv::Mat raw(of.height, of.width, of.cvType, of.buf.data(), of.strideBytes);


            cv::Mat frame_bgr = raw.clone();

            LetterboxInfo lb;
            cv::Mat inp = letterbox_bgr(frame_bgr, modelW, modelH, lb);

            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            // json(trtexec.precision) 기반으로 useFp16 결정
            const bool useFp16 =
                (backend_ == InferenceBackend::TRT_ENGINE) &&
                (trig_trt_precision_ == "fp16");

            DefectJob job;
            job.frameIndex = of.index;
            job.frame_bgr = frame_bgr;
            job.ts_ms = of.timestamp;

            // ===== trigger model run + parse (기존 유지) =====
            if (backend_ == InferenceBackend::TRT_ENGINE)
            {
                bool okRun = false;
                if (trigger_sess_trt) {
                    okRun = trigger_sess_trt->run(inp, modelW, modelH, useFp16, fp16buf, fp32buf, &outShape);
                }

                if (okRun) {
                    // 기존 파서 시그니처 유지(임계는 conf_thr로 통일)
                    MapTriggerOutsToJob_Letterbox_Trt(
                        fp16buf, fp32buf, outShape,
                        lb, modelW, modelH,
                        frame_bgr.cols, frame_bgr.rows,
                        conf_thr, conf_thr,
                        nms_iou_thr,
                        job
                    );
                }
                else {
                    job.trig.ok = false;
                }
            }
            else
            {
                auto outs = trigger_sess_->run(inp, modelW, modelH, useFp16, fp16buf, fp32buf);

                MapTriggerOutsToJob_Letterbox_Ort(
                    outs, lb, modelW, modelH,
                    frame_bgr.cols, frame_bgr.rows,
                    conf_thr, conf_thr,
                    nms_iou_thr,
                    job
                );
            }

            // ===== Python의 조건: tab(0) + horn(1) 둘 다 + score>=conf_thr =====
            // (여기서 cell=tab, pnp=horn 가정)
            const bool tab_ok =
                (job.trig.tab.area() > 0) &&
                (job.trig.tab_score >= conf_thr);

            const bool horn_ok =
                (job.trig.horn.area() > 0) &&
                (job.trig.horn_score >= conf_thr);

            std::cout << "tab_score: " << job.trig.tab_score
                << ", horn_score: " << job.trig.horn_score << std::endl;


            bool should_trigger = false;
            std::string skip_reason;

            if (!(tab_ok && horn_ok)) {
                should_trigger = false;
                skip_reason = "no_tab_or_horn_or_low_score";
            }
            else {
                if (!enable_tracking) {
                    // Python enable_tracking=False: 둘 다 있으면 저장
                    should_trigger = true;
                }
                else {
                    // Python enable_tracking=True: horn center y 이동 방향이 "up"일 때만 저장
                    const float horn_center_y = job.trig.horn.y + (job.trig.horn.height * 0.5f);

                    HornDir current_dir = HornDir::None;

                    if (has_last_horn) {
                        const float delta_y = horn_center_y - last_horn_center_y;

                        if (std::fabs(delta_y) >= horn_direction_threshold) {
                            current_dir = (delta_y > 0.f) ? HornDir::Down : HornDir::Up;
                        }
                        else {
                            current_dir = last_dir; // 미세 흔들림이면 이전 방향 유지 (Python 동일)
                        }

                        const bool turn_detected = (current_dir == HornDir::Up);
                        should_trigger = turn_detected;

                        if (!turn_detected) {
                            skip_reason = "horn_not_turning_up";
                        }
                    }
                    else {
                        // Python: last_horn_center_y가 없으면 방향 판단 불가 -> 저장 안 함
                        should_trigger = false;
                        skip_reason = "horn_direction_unknown_first_observation";
                    }

                    // Python과 동일하게 상태 업데이트(둘 다 검출된 경우에만)
                    has_last_horn = true;
                    last_horn_center_y = horn_center_y;
                    last_dir = current_dir;

                    // Python의 tracking_max_age 초기화 로직 그대로 반영:
                    // if enable_tracking and last_horn_center_y is not None:
                    //     if (frame_idx - 1) % tracking_max_age == 0: reset
                    if (has_last_horn && ((frame_count - 1) % tracking_max_age == 0)) {
                        has_last_horn = false;
                        last_dir = HornDir::None;
                    }
                }
            }

            if (should_trigger) {
                // Python: 저장 crop은 tab bbox(=cell) 기준
                cv::Rect roi = job.trig.tab;
                roi &= cv::Rect(0, 0, frame_bgr.cols, frame_bgr.rows);

                if (roi.area() > 0) {
                    job.crop_bgr = frame_bgr(roi).clone();
                    job.used_roi_abs = roi;
                    job.used_roi_kind = UsedRoiKind::TAB;
                }
                else {
                    // tab_ok였는데 roi가 깨진 경우(방어)
                    job.crop_bgr.release();
                    job.used_roi_abs = cv::Rect();
                    job.used_roi_kind = UsedRoiKind::FULL;
                }

                defect_queue_.push(std::move(job));
            }
            else
            {
                // Python은 “저장 안 함”이면 그냥 넘어가지만,
                // 기존 DLL 구조상 vis/콜백이 필요할 수 있어 기존 출력 흐름은 유지
                cv::Mat vis = frame_bgr.clone();

                if (!vis.empty())
                {
                    const cv::Rect bounds(0, 0, vis.cols, vis.rows);
                    cv::Rect tab = job.trig.tab & bounds;
                    cv::Rect horn = job.trig.horn & bounds;

                    if (tab.area() > 0)
                    {
                        cv::rectangle(vis, tab, cv::Scalar(0, 0, 255), 2);
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "tab:%.3f", job.trig.tab_score);
                        cv::putText(vis, buf, cv::Point(tab.x, std::max(0, tab.y - 5)),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 4);
                    }
                    if (horn.area() > 0)
                    {
                        cv::rectangle(vis, horn, cv::Scalar(255, 0, 0), 2);
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "horn:%.3f", job.trig.horn_score);
                        cv::putText(vis, buf, cv::Point(horn.x, std::max(0, horn.y - 5)),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 0, 0), 2);
                    }

                    char probBuf[256];
                    if (!skip_reason.empty()) {
                        std::snprintf(probBuf, sizeof(probBuf), "trigger skipped: %s", skip_reason.c_str());
                    }
                    else {
                        std::snprintf(probBuf, sizeof(probBuf), "trigger skipped");
                    }
                    cv::putText(vis, probBuf, cv::Point(20, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
                }

                DetectOutputDto output;
                output.error_code = 0;
                output.error_msg = "OK";

                if (c_callback_)
                {
                    std::vector<ResultItemC> c_results;

                    const uint8_t* visData = (!vis.empty()) ? vis.data : nullptr;
                    const int visW = (!vis.empty()) ? vis.cols : 0;
                    const int visH = (!vis.empty()) ? vis.rows : 0;
                    const int visStep = (!vis.empty()) ? (int)vis.step : 0;
                    const int visType = (!vis.empty()) ? vis.type() : 0;

                    c_callback_(
                        job.frameIndex,
                        c_results.empty() ? nullptr : c_results.data(),
                        output.error_code,
                        output.error_msg.c_str(),
                        visData, visW, visH, visStep, visType
                    );
                }
            }
        }
    }


    // ------------------------------------------------------------
    // defectLoop
    // ------------------------------------------------------------
    // defect_queue_ 에서 crop을 꺼내 분류 결과 생성
    void Detector::defectLoop()
    {
        std::vector<uint16_t> fp16buf;
        std::vector<float>    fp32buf;
        std::vector<int64_t>  outShape;

        const bool useFp16 =
            (backend_ == InferenceBackend::TRT_ENGINE) &&
            (defect_trt_precision_ == "fp16");

        const bool XYXY_INCLUSIVE = true;

        while (running_)
        {
            DefectJob job;
            if (!defect_queue_.pop(job)) continue;

            auto start = std::chrono::steady_clock::now();

            // (1) crop 준비 + used_roi 결정
            cv::Mat crop;
            cv::Rect used_roi = job.used_roi_abs;
            UsedRoiKind used_kind = job.used_roi_kind;

            if (!job.crop_bgr.empty())
            {
                crop = job.crop_bgr;
                if (crop.empty()) continue;

                // ✅ triggerLoop가 used_roi_abs를 채우는 전제지만,
                // 방어적으로 비어있으면 trig의 tab/horn 박스에서 유추(절대좌표만 가능)
                if (used_roi.area() <= 0)
                {
                    const cv::Rect bounds(
                        0, 0,
                        job.frame_bgr.empty() ? crop.cols : job.frame_bgr.cols,
                        job.frame_bgr.empty() ? crop.rows : job.frame_bgr.rows
                    );

                    cv::Rect tab = job.trig.tab & bounds;
                    cv::Rect horn = job.trig.horn & bounds;

                    if (tab.area() > 0) { used_roi = tab;  used_kind = UsedRoiKind::TAB; }
                    else if (horn.area() > 0) { used_roi = horn; used_kind = UsedRoiKind::HORN; }
                    // else: 그대로 둠(최종 vis에서 used box 생략)
                }
            }
            else if (!job.frame_bgr.empty())
            {
                const cv::Rect bounds(0, 0, job.frame_bgr.cols, job.frame_bgr.rows);

                if (used_roi.area() <= 0)
                {
                    cv::Rect tab = job.trig.tab & bounds;
                    cv::Rect horn = job.trig.horn & bounds;

                    if (tab.area() > 0) { used_roi = tab;  used_kind = UsedRoiKind::TAB; }
                    else if (horn.area() > 0) { used_roi = horn; used_kind = UsedRoiKind::HORN; }
                    else { used_roi = bounds; used_kind = UsedRoiKind::FULL; }
                }
                used_roi &= bounds;

                if (used_roi.area() <= 0) continue;
                crop = job.frame_bgr(used_roi).clone();
            }

            if (crop.empty()) continue;

            // (2) 버퍼 준비
            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            // (3) 실행 + (4) 확률 파싱 (기존 로직 유지)
            float p_ab = 0.f, p_no = 0.f;
            bool ok_probs = false;

            if (backend_ == InferenceBackend::TRT_ENGINE)
            {
                bool okRun = false;
                if (defect_sess_trt)
                {
                    okRun = defect_sess_trt->run(
                        crop, defect_in_w_, defect_in_h_,
                        useFp16, fp16buf, fp32buf, &outShape);
                }
                ok_probs = okRun && ParseDefectProbs3_Trt(fp16buf, fp32buf, outShape, p_ab, p_no);
            }
            else
            {
                auto outs = defect_sess_->run(crop, defect_in_w_, defect_in_h_, useFp16, fp16buf, fp32buf);
                ok_probs = ParseDefectProbs3_Ort(outs, p_ab, p_no);
            }

            enum { AB = 0, NO = 1 } top = NO;
            float topScore = 0.f;

            if (ok_probs) {
                top = AB; topScore = p_ab;
                if (p_no > topScore) { top = NO; topScore = p_no; }
            }

            const char* top1_str =
                (!ok_probs) ? "unknown" :
                (top == AB) ? "abnormal" : "normal";

            // ===== Per-class Q/AB threshold judgment =====
            // Class mapping: AB(0)=NG_class, NO(1)=OK_class
            float q_thr = 0.f, ab_thr = 0.f;
            if (top == AB) { q_thr = ng_q_thr_;  ab_thr = ng_ab_thr_;  }
            else           { q_thr = ok_q_thr_;  ab_thr = ok_ab_thr_;  }

            const char* final_str = "Questionable";
            std::string reason = "parse_failed";

            if (ok_probs) {
                if (topScore < q_thr) {
                    final_str = "OK";
                    reason = (top == AB) ? "ng_below_q_threshold" : "ok_below_q_threshold";
                }
                else if (topScore < ab_thr) {
                    final_str = "Questionable";
                    reason = "between_q_and_ab_threshold";
                }
                else {
                    if (top == AB) {
                        final_str = "Abnormal";
                        reason = "ng_above_ab_threshold";
                    }
                    else {
                        final_str = "Normal";
                        reason = "ok_above_ab_threshold";
                    }
                }
            }
            else {
                final_str = "Questionable";
                reason = "parse_failed";
            }

            cv::Mat vis = job.frame_bgr.empty() ? crop.clone() : job.frame_bgr.clone();
            cv::Rect box = used_roi;
            const cv::Rect bounds(0, 0, vis.cols, vis.rows);

            if (!vis.empty())
            {
                cv::Rect tab = job.trig.tab & bounds;
                cv::Rect horn = job.trig.horn & bounds;
                cv::Rect used = used_roi & bounds;

                if (tab.area() > 0)
                {
                    cv::rectangle(vis, tab, cv::Scalar(0, 0, 255), 2);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "tab:%.3f", job.trig.tab_score);
                    cv::putText(vis, buf, cv::Point(tab.x, std::max(0, tab.y - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 4);
                }
                if (horn.area() > 0)
                {
                    cv::rectangle(vis, horn, cv::Scalar(255, 0, 0), 2);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "horn:%.3f", job.trig.horn_score);
                    cv::putText(vis, buf, cv::Point(horn.x, std::max(0, horn.y - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 0, 0), 2);
                }

                if (used.area() > 0)
                {
                    cv::rectangle(vis, used, cv::Scalar(255, 255, 255), 3);

                    const char* usedName =
                        (used_kind == UsedRoiKind::HORN) ? "used:tab" :
                        (used_kind == UsedRoiKind::TAB) ? "used:horn" :
                        (used_kind == UsedRoiKind::PRE_CROP) ? "used:pre_crop" :
                        "used:full";

                    cv::putText(vis, usedName, cv::Point(used.x, std::max(0, used.y - 25)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);

                    box = used;
                }

                char probBuf[256];
                if (ok_probs)
                    std::snprintf(probBuf, sizeof(probBuf), "ab:%.3f no:%.3f | top1:%s %.3f | %s",
                        p_ab, p_no, top1_str, topScore, final_str);
                else
                    std::snprintf(probBuf, sizeof(probBuf), "prob parse failed");
                cv::putText(vis, probBuf, cv::Point(20, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
            }

            DetectOutputDto output;
            output.error_code = ok_probs ? 0 : 1;
            output.error_msg = ok_probs ? "OK" : "ParseDefectProbs3 failed";
            output.takt_time = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            HmCutter::ResultItem resultItem;

            if (!ok_probs) {
                resultItem.defect_type = HmCutter::DefectTypeEnum::NONE;
                resultItem.score = 0.f;
            }
            else {
                if (std::strcmp(final_str, "Abnormal") == 0) {
                    resultItem.defect_type = HmCutter::DefectTypeEnum::NG;
                    resultItem.score = topScore;
                }
                else if (std::strcmp(final_str, "Normal") == 0) {
                    resultItem.defect_type = HmCutter::DefectTypeEnum::OK;
                    resultItem.score = topScore;
                }
                else {
                    resultItem.defect_type = HmCutter::DefectTypeEnum::NONE;
                    resultItem.score = topScore;
                }
            }

            resultItem.box.x1 = (unsigned int)box.x;
            resultItem.box.y1 = (unsigned int)box.y;
            if (XYXY_INCLUSIVE) {
                resultItem.box.x2 = (unsigned int)(box.x + box.width - 1);
                resultItem.box.y2 = (unsigned int)(box.y + box.height - 1);
            }
            else {
                resultItem.box.x2 = (unsigned int)(box.x + box.width);
                resultItem.box.y2 = (unsigned int)(box.y + box.height);
            }

            if (resultItem.defect_type == HmCutter::DefectTypeEnum::NG) {
                output.ab_results.push_back(resultItem);
            }
            else if (resultItem.defect_type == HmCutter::DefectTypeEnum::OK) {
                output.normal_results.push_back(resultItem);
            }
            else {
                output.q_results.push_back(resultItem);
            }

            if (c_callback_)
            {
                std::vector<ResultItemC> c_results;
                c_results.reserve(output.ab_results.size() + output.q_results.size() + output.normal_results.size());

                for (const auto& r : output.ab_results)     c_results.push_back(ToCResult(r));
                for (const auto& r : output.q_results)      c_results.push_back(ToCResult(r));
                for (const auto& r : output.normal_results) c_results.push_back(ToCResult(r));

                const uint8_t* visData = (!vis.empty()) ? vis.data : nullptr;
                const int visW = (!vis.empty()) ? vis.cols : 0;
                const int visH = (!vis.empty()) ? vis.rows : 0;
                const int visStep = (!vis.empty()) ? (int)vis.step : 0;
                const int visType = (!vis.empty()) ? vis.type() : 0;

                c_callback_(
                    job.frameIndex,
                    c_results.empty() ? nullptr : c_results.data(),
                    output.error_code,
                    output.error_msg.c_str(),
                    visData, visW, visH, visStep, visType
                );
            }
        }
    }



    // trigger/defect 메타 파일 로드 및 내부 설정 반영
    void Detector::loadMetaData(const std::filesystem::path& trigger_meta, const std::filesystem::path& defect_meta)
    {
        auto parseOne = [&](const std::filesystem::path& jsonPath, bool isTrigger)
            {
                std::string err;

                std::string modelName, modelType;
                std::string inName, inLayout, inDtype, inColor;
                std::vector<int64_t> inShape;
                bool inDynamic = false;

                std::string outName, bboxFormat, trtPrecision;
                std::vector<int64_t> outShape;
                int classStartIndex = 0;
                std::vector<std::string> classNames;
                int trtWorkspace = 0;

                int modelW = 0, modelH = 0;

                std::filesystem::path onnxPath, enginePath;

                if (!LoadModelMetaJson(
                    jsonPath,
                    modelName, modelType,
                    inName, inLayout, inDtype, inColor, inShape, inDynamic,
                    outName, outShape, bboxFormat, classStartIndex,
                    classNames,
                    trtPrecision, trtWorkspace,
                    modelW, modelH,
                    onnxPath, enginePath,
                    &err))
                {
                    throw std::runtime_error(std::string(isTrigger ? "[Trigger meta] " : "[Defect meta] ") + err);
                }

                if (isTrigger)
                {
                    trig_in_w_ = modelW;
                    trig_in_h_ = modelH;

                    trig_out_shape_ = outShape;
                    trig_bbox_format_ = bboxFormat;
                    trig_class_start_index_ = classStartIndex;
                    trig_class_names_ = classNames;
                    trig_trt_precision_ = trtPrecision;

                    trigger_onnx_path_ = onnxPath;
                    trigger_engine_path_ = enginePath;
                }
                else
                {
                    defect_in_w_ = modelW;
                    defect_in_h_ = modelH;

                    defect_trt_precision_ = trtPrecision;

                    defect_onnx_path_ = onnxPath;
                    defect_engine_path_ = enginePath;
                }
            };

        parseOne(trigger_meta, true);
        parseOne(defect_meta, false);
    }

    // 모델 메타 JSON 파싱 함수
    bool Detector::LoadModelMetaJson(
        const std::filesystem::path& jsonPath,

        std::string& modelName,
        std::string& modelType,

        std::string& inputName,
        std::string& inputLayout,
        std::string& inputDtype,
        std::string& inputColorOrder,
        std::vector<int64_t>& inputShape,
        bool& inputDynamic,

        std::string& outputName,
        std::vector<int64_t>& outputShape,
        std::string& bboxFormat,
        int& classStartIndex,

        std::vector<std::string>& classNames,

        std::string& trtPrecision,
        int& trtWorkspace,

        int& modelW,
        int& modelH,

        std::filesystem::path& onnxPathOut,
        std::filesystem::path& enginePathOut,

        std::string* errMsg
    ) {
        auto fail = [&](const std::string& msg) -> bool {
            if (errMsg) *errMsg = msg;
            return false;
            };

        std::ifstream ifs(jsonPath, std::ios::binary);
        if (!ifs.is_open()) {
            return fail("LoadModelMetaJson: cannot open file: " + jsonPath.string());
        }

        nlohmann::json j;
        try { ifs >> j; }
        catch (const std::exception& e) {
            return fail(std::string("LoadModelMetaJson: json parse failed: ") + e.what());
        }

        auto get_str = [&](const nlohmann::json& o, const char* key, std::string& out) -> bool {
            if (!o.contains(key) || !o[key].is_string()) return false;
            out = o[key].get<std::string>();
            return true;
            };
        auto get_bool = [&](const nlohmann::json& o, const char* key, bool& out) -> bool {
            if (!o.contains(key) || !o[key].is_boolean()) return false;
            out = o[key].get<bool>();
            return true;
            };
        auto get_int = [&](const nlohmann::json& o, const char* key, int& out) -> bool {
            if (!o.contains(key) || !o[key].is_number_integer()) return false;
            out = o[key].get<int>();
            return true;
            };
        auto get_i64_array = [&](const nlohmann::json& o, const char* key, std::vector<int64_t>& out) -> bool {
            if (!o.contains(key) || !o[key].is_array()) return false;
            out.clear();
            out.reserve(o[key].size());
            for (const auto& v : o[key]) {
                if (!v.is_number_integer()) return false;
                out.push_back(v.get<int64_t>());
            }
            return true;
            };
        auto get_str_array = [&](const nlohmann::json& o, const char* key, std::vector<std::string>& out) -> bool {
            if (!o.contains(key) || !o[key].is_array()) return false;
            out.clear();
            out.reserve(o[key].size());
            for (const auto& v : o[key]) {
                if (!v.is_string()) return false;
                out.push_back(v.get<std::string>());
            }
            return true;
            };

        // 필수 필드
        if (!get_str(j, "model_name", modelName)) return fail("missing/invalid: model_name");
        if (!get_str(j, "model_type", modelType)) return fail("missing/invalid: model_type");

        if (!j.contains("input") || !j["input"].is_object()) return fail("missing/invalid: input");
        const auto& in = j["input"];
        if (!get_str(in, "name", inputName)) return fail("missing/invalid: input.name");
        if (!get_str(in, "layout", inputLayout)) return fail("missing/invalid: input.layout");
        if (!get_str(in, "dtype", inputDtype)) return fail("missing/invalid: input.dtype");
        if (!get_str(in, "color_order", inputColorOrder)) return fail("missing/invalid: input.color_order");
        if (!get_i64_array(in, "shape", inputShape)) return fail("missing/invalid: input.shape");
        if (!get_bool(in, "dynamic", inputDynamic)) return fail("missing/invalid: input.dynamic");

        // NCHW 기준 H/W 추출
        if (inputLayout != "NCHW") return fail("unsupported input.layout (expected NCHW): " + inputLayout);
        if (inputShape.size() != 4) return fail("input.shape must have 4 dims for NCHW");
        if (inputShape[2] <= 0 || inputShape[3] <= 0) return fail("input.shape H/W invalid");
        modelH = (int)inputShape[2];
        modelW = (int)inputShape[3];

        // output
        if (!j.contains("output") || !j["output"].is_object()) return fail("missing/invalid: output");
        const auto& out = j["output"];
        if (!out.contains("bindings") || !out["bindings"].is_array() || out["bindings"].empty())
            return fail("missing/invalid: output.bindings[0]");

        const auto& b0 = out["bindings"][0];
        if (!b0.is_object()) return fail("missing/invalid: output.bindings[0] object");
        if (!get_str(b0, "name", outputName)) return fail("missing/invalid: output.bindings[0].name");
        if (!get_i64_array(b0, "shape", outputShape)) return fail("missing/invalid: output.bindings[0].shape");
        if (!get_str(b0, "bbox_format", bboxFormat)) return fail("missing/invalid: output.bindings[0].bbox_format");
        if (!get_int(b0, "class_start_index", classStartIndex)) return fail("missing/invalid: output.bindings[0].class_start_index");

        // class_names
        if (!get_str_array(j, "class_names", classNames)) return fail("missing/invalid: class_names");

        // trtexec
        if (!j.contains("trtexec") || !j["trtexec"].is_object()) return fail("missing/invalid: trtexec");
        const auto& te = j["trtexec"];
        if (!get_str(te, "precision", trtPrecision)) return fail("missing/invalid: trtexec.precision");
        if (!get_int(te, "workspace", trtWorkspace)) return fail("missing/invalid: trtexec.workspace");

        // ✅ NEW: paths
        std::string onnxStr, engineStr;
        bool okPaths = false;

        if (j.contains("paths") && j["paths"].is_object()) {
            const auto& p = j["paths"];
            if (get_str(p, "onnx", onnxStr) && get_str(p, "engine", engineStr)) okPaths = true;
        }
        // fallback 키도 허용
        if (!okPaths) {
            if (get_str(j, "onnx", onnxStr) && get_str(j, "engine", engineStr)) okPaths = true;
        }
        if (!okPaths) {
            return fail("missing/invalid: paths.onnx/paths.engine (or onnx_path/engine_path)");
        }

        auto resolve = [&](const std::string& s) -> std::filesystem::path {
            std::filesystem::path p = std::filesystem::path(s);
            if (p.is_relative()) p = jsonPath.parent_path() / p;
            return p;
            };

        onnxPathOut = resolve(onnxStr);
        enginePathOut = resolve(engineStr);
        return true;
    }

} // namespace HmCutter} // namespace HmCutter} // namespace HmCutter} // namespace HmCutter