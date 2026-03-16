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
#include "Include/Utills/DebugLog.h"

namespace HmCutter
{
    // ── epoch ms 헬퍼 ──
    static inline int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    Detector::Detector(const AlgorithmConfig& config)
        : config_(config)
    {
        if (config.paths.triggerEnginePath && config.paths.triggerEnginePath[0] != '\0') {
            std::filesystem::path p(config.paths.triggerEnginePath);
            triggerModelDir      = p.parent_path();
            triggerModelName_    = p.stem().string();
            trigger_engine_path_ = p;
        }

        if (config.paths.defectEnginePath && config.paths.defectEnginePath[0] != '\0') {
            std::filesystem::path p(config.paths.defectEnginePath);
            defectModelDir      = p.parent_path();
            defectModelName_    = p.stem().string();
            defect_engine_path_ = p;
        }

        // ✅ Input ROI (0,0,0,0이면 미적용)
        input_roi_ = cv::Rect(
            config.roi.x1,
            config.roi.y1,
            config.roi.x2 - config.roi.x1,
            config.roi.y2 - config.roi.y1);
        if (input_roi_.width <= 0 || input_roi_.height <= 0)
            input_roi_ = cv::Rect(); // 무효 ROI → 전체 프레임

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

    AlgorithmStatusInfo Detector::getStatus() const
    {
        return status_;
    }

    int Detector::notifyEquipStatus()
    {
        // 토글: 0→1, 1→0
        int prev = equip_status_.load();
        int next = (prev == 0) ? 1 : 0;
        equip_status_.store(next);
        DbgLog("[PLC] NotifyEquipStatus toggled: %d -> %d\n", prev, next);
        return next;
    }

    FrameQueueStatsC Detector::getFrameStats() const
    {
        FrameQueueStatsC s{};
        s.push_count        = frame_queue_owned_.push_count();
        s.pop_count         = frame_queue_owned_.pop_count();
        s.drop_count        = frame_queue_owned_.drop_count();
        s.queue_size        = frame_queue_owned_.size();
        s.last_pushed_index = last_pushed_index_.load();
        s.last_popped_index = last_popped_index_.load();
        return s;
    }


    // ------------------------------------------------------------
    // model_setup (ORT / TRT 분기)
    // ------------------------------------------------------------
    // 모델 메타(JSON) 로드 + 세션 생성 + 워커 스레드 시작
    void Detector::model_setup() {
        status_ = { false, NowMs() };   // INITIALIZING (not running)

        try {
            // ✅ fullPath가 생성자에서 세팅된 경우 그것을 사용, 아니면 dir/name으로 조립
            if (trigger_engine_path_.empty())
                trigger_engine_path_ = triggerModelDir / (triggerModelName_ + ".engine");
            if (defect_engine_path_.empty())
                defect_engine_path_  = defectModelDir  / (defectModelName_  + ".engine");

            // ✅ .json은 항상 engine 경로의 stem으로 찾음 (버전 변경 시 자동 반영)
            std::filesystem::path trigger_meta = trigger_engine_path_.parent_path()
                                               / (trigger_engine_path_.stem().string() + ".json");
            std::filesystem::path defect_meta  = defect_engine_path_.parent_path()
                                               / (defect_engine_path_.stem().string()  + ".json");

            trigger_onnx_path_ = trigger_engine_path_.parent_path()
                                / (trigger_engine_path_.stem().string() + ".onnx");
            defect_onnx_path_  = defect_engine_path_.parent_path()
                                / (defect_engine_path_.stem().string()  + ".onnx");

            // meta 로드(여기서 onnx/engine 경로까지 채워짐 — paths 섹션이 있으면 덮어씀)
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
             /*   trigger_sess_ = std::make_unique<OrtSessionWrap>(trigger_onnx_path_.wstring(), true);
                defect_sess_ = std::make_unique<OrtSessionWrap>(defect_onnx_path_.wstring(), true);*/
            }
        }
        catch (const Ort::Exception& e) {
            status_ = { false, NowMs() };
            DbgLog("[ORT EXCEPTION] model_setup failed: %s\n", e.what());
            trigger_sess_.reset();
            defect_sess_.reset();
            trigger_sess_trt.reset();
            defect_sess_trt.reset();
            return;
        }
        catch (const std::exception& e) {
            status_ = { false, NowMs() };
            DbgLog("[EXCEPTION] model_setup failed: %s\n", e.what());
            trigger_sess_.reset();
            defect_sess_.reset();
            trigger_sess_trt.reset();
            defect_sess_trt.reset();
            return;
        }

        status_ = { false, NowMs() };   // READY (not running yet)

        // ✅ warmup: 빈 Mat으로 추론 엔진 예열 (실제 프레임 낭비 방지)
        int warmup_iterations = 5;
        for (int i = 0; i < warmup_iterations; ++i) {
            warmupModels();
        }
    }

    // ------------------------------------------------------------
    // warmupModels — initialize 시 빈 깡통 Mat으로 엔진 예열
    // ------------------------------------------------------------
    void Detector::warmupModels()
    {
        DbgLog("[Detector] warmupModels: starting engine warmup with blank Mat...\n");

        std::vector<uint16_t> fp16buf;
        std::vector<float>    fp32buf;
        std::vector<int64_t>  outShape;

        // ✅ Trigger 모델 warmup: 빈 BGR 이미지 생성 (모델 입력 크기)
        {
            cv::Mat blank(trig_in_h_, trig_in_w_, CV_8UC3, cv::Scalar(0, 0, 0));
            const bool useFp16 =
                (backend_ == InferenceBackend::TRT_ENGINE) &&
                (trig_trt_precision_ == "fp16");

            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            if (backend_ == InferenceBackend::TRT_ENGINE && trigger_sess_trt) {
                bool ok = trigger_sess_trt->run(blank, trig_in_w_, trig_in_h_, useFp16, fp16buf, fp32buf, &outShape);
                DbgLog("[Detector] warmup trigger (TRT): %s\n", ok ? "OK" : "FAIL");
            }
            else if (trigger_sess_) {
                trigger_sess_->run(blank, trig_in_w_, trig_in_h_, useFp16, fp16buf, fp32buf);
                DbgLog("[Detector] warmup trigger (ORT): done\n");
            }
        }

        // ✅ Defect 모델 warmup: 빈 BGR 이미지 생성 (모델 입력 크기)
        {
            cv::Mat blank(defect_in_h_, defect_in_w_, CV_8UC3, cv::Scalar(0, 0, 0));
            const bool useFp16 =
                (backend_ == InferenceBackend::TRT_ENGINE) &&
                (defect_trt_precision_ == "fp16");

            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            if (backend_ == InferenceBackend::TRT_ENGINE && defect_sess_trt) {
                bool ok = defect_sess_trt->run(blank, defect_in_w_, defect_in_h_, useFp16, fp16buf, fp32buf, &outShape);
                DbgLog("[Detector] warmup defect (TRT): %s\n", ok ? "OK" : "FAIL");
            }
            else if (defect_sess_) {
                defect_sess_->run(blank, defect_in_w_, defect_in_h_, useFp16, fp16buf, fp32buf);
                DbgLog("[Detector] warmup defect (ORT): done\n");
            }
        }

        DbgLog("[Detector] warmupModels: complete.\n");
    }

    void Detector::run() {
        if (running_ || status_.is_running) return;

        // ✅ 모델 세션이 로드되지 않은 경우 스레드 시작 방지
        if (backend_ == InferenceBackend::TRT_ENGINE) {
            if (!trigger_sess_trt || !defect_sess_trt) {
                DbgLog("[Detector::run] ABORT: TRT sessions not loaded. Call model_setup() first.\n");
                return;
            }
        } else {
            if (!trigger_sess_ || !defect_sess_) {
                DbgLog("[Detector::run] ABORT: ORT sessions not loaded. Call model_setup() first.\n");
                return;
            }
        }

        // ✅ 큐를 재사용 가능 상태로 리셋 (이전 stop()에서 stopped_=true로 설정됨)
        frame_queue_owned_.reset();
        defect_queue_.reset();

        running_ = true;
        trigger_worker_ = std::thread(&Detector::triggerLoop, this);
        defect_worker_ = std::thread(&Detector::defectLoop, this);
    }

    void Detector::stop() {
        if (!running_) return;

        running_ = false;

        // ✅ 큐의 pop() 블로킹을 해제하여 워커 스레드가 종료될 수 있도록 함
        frame_queue_owned_.stop();
        defect_queue_.stop();

        if (trigger_worker_.joinable()) trigger_worker_.join();
        if (defect_worker_.joinable()) defect_worker_.join();
        status_ = { false, NowMs() };   // stopped
    }

    // ------------------------------------------------------------
    // pushFrame
    // ------------------------------------------------------------
    int Detector::pushFrame(const FrameData& frame) {
        if (!status_.is_running && !running_)
            return -1;

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
        strncpy_s(of.timestamp, FRAME_TIMESTAMP_MAX, frame.timestamp, _TRUNCATE);
        of.buf.resize(totalBytes);

        if (!frame.data || frame.width <= 0 || frame.height <= 0) return -1;
        std::memcpy(of.buf.data(), frame.data, totalBytes);

        last_pushed_index_.store(frame.index);
        frame_queue_owned_.push(std::move(of));
        return 0;
    }


    // cv::Mat → ImageDataC 변환 헬퍼
    static inline ImageDataC MakeImageDataC(const cv::Mat& m) {
        ImageDataC d{};
        d.data        = m.data;
        d.width       = m.cols;
        d.height      = m.rows;
        d.strideBytes = (int)m.step;
        d.cvType      = m.type();
        return d;
    }

    // ResultItem -> C API용 ResultItemC 변환
    static inline ResultItemC ToCResult(const HmCutter::ResultItem& r)
    {
        ResultItemC c{};
        c.box.x1 = r.box.x1;
        c.box.y1 = r.box.y1;
        c.box.x2 = r.box.x2;
        c.box.y2 = r.box.y2;

        // 복수 예측 결과
        int n = (int)r.preds.size();
        if (n > RESULT_PREDS_MAX) n = RESULT_PREDS_MAX;
        c.pred_count = n;
        for (int i = 0; i < n; ++i) {
            c.preds[i].pred_score = r.preds[i].score;
            strncpy_s(c.preds[i].pred_label, RESULT_LABEL_MAX, r.preds[i].label.c_str(), _TRUNCATE);
            strncpy_s(c.preds[i].decision,   RESULT_STR_MAX,   r.preds[i].decision.c_str(), _TRUNCATE);
        }
        // preds가 비어있으면 하위 호환용 pred_score/pred_label로 채움
        if (n == 0 && !r.pred_label.empty()) {
            c.pred_count = 1;
            c.preds[0].pred_score = r.pred_score;
            strncpy_s(c.preds[0].pred_label, RESULT_LABEL_MAX, r.pred_label.c_str(), _TRUNCATE);
            strncpy_s(c.preds[0].decision,   RESULT_STR_MAX,   r.decision.c_str(),   _TRUNCATE);
        }

        strncpy_s(c.final_decision,  RESULT_STR_MAX, r.final_decision.c_str(),  _TRUNCATE);
        strncpy_s(c.input_timestamp, RESULT_STR_MAX, r.input_timestamp.c_str(), _TRUNCATE);
        return c;
    }

    // ------------------------------------------------------------
    // triggerLoop (ORT/TRT 분기)
    // ------------------------------------------------------------
    void Detector::triggerLoop()
    {
        status_ = { true, NowMs() };   // RUNNING

        std::vector<uint16_t> fp16buf;
        std::vector<float>    fp32buf;
        std::vector<int64_t>  outShape;

        const int modelW = trig_in_w_;
        const int modelH = trig_in_h_;

        const float conf_thr = cell_min_score_;
        const float nms_iou_thr = 0.45f;

        const bool enable_tracking = true;

        const float horn_direction_threshold = 2.0f;
        const int   tracking_max_age = 10;

        enum class HornDir { None, Up, Down };
        bool   has_last_horn = false;
        float  last_horn_center_y = 0.f;
        HornDir last_dir = HornDir::None;

        int64_t frame_count = 0;

        cv::Mat lb_resized, lb_out;

        while (running_) {
            OwnedFrame of;
            if (!frame_queue_owned_.pop(of)) continue;

            ++frame_count;

            // ✅ 디버깅: pop된 프레임 인덱스 추적
            last_popped_index_.store(of.index);

            cv::Mat raw(of.height, of.width, of.cvType, of.buf.data(), of.strideBytes);

            // ✅ Input ROI 적용 (UI에서 설정, area()==0이면 전체 프레임)
            cv::Mat roi_frame = raw;
            if (input_roi_.area() > 0) {
                cv::Rect clipped = input_roi_ & cv::Rect(0, 0, raw.cols, raw.rows);
                if (clipped.area() > 0)
                    roi_frame = raw(clipped);
            }

            LetterboxInfo lb;
            letterbox_bgr(roi_frame, modelW, modelH, lb, lb_resized, lb_out);

            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            const bool useFp16 =
                (backend_ == InferenceBackend::TRT_ENGINE) &&
                (trig_trt_precision_ == "fp16");

            DefectJob job;
            job.frameIndex = of.index;
            strncpy_s(job.ts_input, FRAME_TIMESTAMP_MAX, of.timestamp, _TRUNCATE);

            if (backend_ == InferenceBackend::TRT_ENGINE)
            {
                bool okRun = false;
                if (trigger_sess_trt) {
                    okRun = trigger_sess_trt->run(lb_out, modelW, modelH, useFp16, fp16buf, fp32buf, &outShape);
                }

                if (okRun) {
                    MapTriggerOutsToJob_Letterbox_Trt(
                        fp16buf, fp32buf, outShape,
                        lb, modelW, modelH,
                        raw.cols, raw.rows,
                        conf_thr, conf_thr,
                        nms_iou_thr,
                        trig_class_start_index_,
                        job
                    );
                }
                else {
                    job.trig.ok = false;
                }
            }
            else
            {
               /* auto outs = trigger_sess_->run(lb_out, modelW, modelH, useFp16, fp16buf, fp32buf);

                MapTriggerOutsToJob_Letterbox_Ort(
                    outs, lb, modelW, modelH,
                    raw.cols, raw.rows,
                    conf_thr, conf_thr,
                    nms_iou_thr,
                    job
                );*/
            }

            const bool tab_ok =
                (job.trig.tab.area() > 0) &&
                (job.trig.tab_score >= conf_thr);

            const bool horn_ok =
                (job.trig.horn.area() > 0) &&
                (job.trig.horn_score >= conf_thr);

            DbgLog("tab_score: %.3f, horn_score: %.3f\n", job.trig.tab_score, job.trig.horn_score);

            bool should_trigger = false;
            std::string skip_reason;

            if (!(tab_ok && horn_ok)) {
                should_trigger = false;
                skip_reason = "no_tab_or_horn_or_low_score";
            }
            else {
                if (!enable_tracking) {
                    should_trigger = true;
                }
                else {
                    const float horn_center_y = job.trig.horn.y + (job.trig.horn.height * 0.5f);

                    HornDir current_dir = HornDir::None;

                    if (has_last_horn) {
                        const float delta_y = horn_center_y - last_horn_center_y;

                        if (std::fabs(delta_y) >= horn_direction_threshold) {
                            current_dir = (delta_y > 0.f) ? HornDir::Down : HornDir::Up;
                        }
                        else {
                            current_dir = last_dir;
                        }

                        const bool turn_detected = (current_dir == HornDir::Up);
                        should_trigger = turn_detected;

                        if (!turn_detected) {
                            skip_reason = "horn_not_turning_up";
                        }
                    }
                    else {
                        should_trigger = false;
                        skip_reason = "horn_direction_unknown_first_observation";
                    }

                    has_last_horn = true;
                    last_horn_center_y = horn_center_y;
                    last_dir = current_dir;

                    if (has_last_horn && ((frame_count - 1) % tracking_max_age == 0)) {
                        has_last_horn = false;
                        last_dir = HornDir::None;
                    }
                }
            }

            if (should_trigger) {
                job.frame_bgr = raw.clone();
                cv::Rect roi = job.trig.tab;
                roi &= cv::Rect(0, 0, raw.cols, raw.rows);

                if (roi.area() > 0) {
                    job.crop_bgr = job.frame_bgr(roi).clone();
                    job.used_roi_abs = roi;
                    job.used_roi_kind = UsedRoiKind::TAB;
                }
                else {
                    job.crop_bgr.release();
                    job.used_roi_abs = cv::Rect();
                    job.used_roi_kind = UsedRoiKind::FULL;
                }

                defect_queue_.push(std::move(job));
            }
            else
            {
                // trigger skip — 현재 콜백 비활성화
            }
        }
    }


    // ------------------------------------------------------------
    // defectLoop
    // ------------------------------------------------------------
    void Detector::defectLoop()
    {
        std::vector<uint16_t> fp16buf;
        std::vector<float>    fp32buf;
        std::vector<int64_t>  outShape;

        const bool useFp16 =
            (backend_ == InferenceBackend::TRT_ENGINE) &&
            (defect_trt_precision_ == "fp16");

        const bool XYXY_INCLUSIVE = true;

        // ✅ JSON class_names에서 클래스 인덱스 동적 매핑
        const auto& cls = defect_class_names_;
        const int numClasses = (int)cls.size();

        // NG 계열 판단 헬퍼: class_names에 "abnormal" 또는 "ng"가 포함되면 NG
        auto isNgClass = [](const std::string& name) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.find("abnormal") != std::string::npos ||
                   lower.find("ng") != std::string::npos;
        };

        while (running_)
        {
            DefectJob job;
            if (!defect_queue_.pop(job)) continue;

            auto start = std::chrono::steady_clock::now();

            cv::Mat crop;
            cv::Rect used_roi = job.used_roi_abs;
            UsedRoiKind used_kind = job.used_roi_kind;

            if (!job.crop_bgr.empty())
            {
                crop = job.crop_bgr;
                if (crop.empty()) continue;

                if (used_roi.area() <= 0)
                {
                    const cv::Rect bounds(
                        0, 0,
                        job.frame_bgr.empty() ? crop.cols : job.frame_bgr.cols,
                        job.frame_bgr.empty() ? crop.rows : job.frame_bgr.rows);
                    cv::Rect tab  = job.trig.tab  & bounds;
                    cv::Rect horn = job.trig.horn & bounds;
                    if      (tab.area()  > 0) { used_roi = tab;  used_kind = UsedRoiKind::TAB;  }
                    else if (horn.area() > 0) { used_roi = horn; used_kind = UsedRoiKind::HORN; }
                }
            }
            else if (!job.frame_bgr.empty())
            {
                const cv::Rect bounds(0, 0, job.frame_bgr.cols, job.frame_bgr.rows);
                if (used_roi.area() <= 0)
                {
                    cv::Rect tab  = job.trig.tab  & bounds;
                    cv::Rect horn = job.trig.horn & bounds;
                    if      (tab.area()  > 0) { used_roi = tab;  used_kind = UsedRoiKind::TAB;  }
                    else if (horn.area() > 0) { used_roi = horn; used_kind = UsedRoiKind::HORN; }
                    else                      { used_roi = bounds; used_kind = UsedRoiKind::FULL; }
                }
                used_roi &= bounds;
                if (used_roi.area() <= 0) continue;
                crop = job.frame_bgr(used_roi).clone();
            }

            if (crop.empty()) continue;

            fp16buf.clear();
            fp32buf.clear();
            outShape.clear();

            // ✅ 추론 + 확률 파싱 — 클래스 수만큼 확률 배열로 받음
            std::vector<float> probs(numClasses, 0.f);
            bool ok_probs = false;

            if (backend_ == InferenceBackend::TRT_ENGINE)
            {
                bool okRun = false;
                if (defect_sess_trt)
                    okRun = defect_sess_trt->run(crop, defect_in_w_, defect_in_h_, useFp16, fp16buf, fp32buf, &outShape);
                if (okRun)
                    ok_probs = ParseDefectProbs_Trt(fp16buf, fp32buf, outShape, numClasses, probs);
            }
            else
            {
               /* auto outs = defect_sess_->run(crop, defect_in_w_, defect_in_h_, useFp16, fp16buf, fp32buf);
                ok_probs = ParseDefectProbs_Ort(outs, numClasses, probs);*/
            }

            // ✅ top-1 결정 — 동적 클래스 수 기반
            int topIdx = 0;
            float topScore = 0.f;
            if (ok_probs) {
                for (int i = 0; i < numClasses; ++i) {
                    if (probs[i] > topScore) {
                        topIdx = i;
                        topScore = probs[i];
                    }
                }
            }

            // ✅ top-1 클래스 이름 (JSON class_names 기반)
            const std::string& topClassName = (topIdx < numClasses) ? cls[topIdx] : std::string("unknown");
            const bool topIsNg = isNgClass(topClassName);

            // ✅ Q/AB 임계 판별 — class_names 기반
            float q_thr = 0.f, ab_thr = 0.f;
            if (topIsNg) {
                q_thr = ng_q_thr_;  ab_thr = ng_ab_thr_;
            } else {
                std::string lower = topClassName;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find("normal") != std::string::npos || lower.find("ok") != std::string::npos) {
                    q_thr = ok_q_thr_;  ab_thr = ok_ab_thr_;
                } else {
                    q_thr = etc_q_thr_; ab_thr = etc_ab_thr_;
                }
            }

            const char* final_str = "Questionable";
            if (ok_probs) {
                if (topIsNg) {
                    if (topScore >= ab_thr)       final_str = "Abnormal";
                    else if (topScore >= q_thr)   final_str = "Questionable";
                    else                          final_str = "OK";
                }
                else {
                    if (topScore >= ab_thr)       final_str = "Normal";
                    else if (topScore >= q_thr)   final_str = "Questionable";
                    else                          final_str = "OK";
                }
            }

            HmCutter::ResultItem resultItem;
            resultItem.input_timestamp = job.ts_input;

            resultItem.pred_score = 0.f;
            resultItem.pred_label = "unknown";
            resultItem.decision = "Questionable";
            resultItem.final_decision = "Questionable";

            if (ok_probs)
            {
                // ✅ pred_label: JSON class_names 기반 (첫 글자 대문자)
                std::string label = topClassName;
                if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
                resultItem.pred_label = label;
                resultItem.decision   = final_str;
                resultItem.pred_score = topScore;

                if (std::strcmp(final_str, "Abnormal") == 0)
                    resultItem.final_decision = "NG";
                else if (std::strcmp(final_str, "Normal") == 0 || std::strcmp(final_str, "OK") == 0)
                    resultItem.final_decision = "OK";
                else
                    resultItem.final_decision = "Questionable";
            }

            {
                const cv::Rect frameBounds(
                    0, 0,
                    job.frame_bgr.empty() ? crop.cols : job.frame_bgr.cols,
                    job.frame_bgr.empty() ? crop.rows : job.frame_bgr.rows);
                cv::Rect box = (used_roi.area() > 0) ? (used_roi & frameBounds)
                                                     : cv::Rect(0, 0, crop.cols, crop.rows);
                resultItem.box.x1 = (unsigned int)box.x;
                resultItem.box.y1 = (unsigned int)box.y;
                resultItem.box.x2 = (unsigned int)(box.x + box.width  - (XYXY_INCLUSIVE ? 1 : 0));
                resultItem.box.y2 = (unsigned int)(box.y + box.height - (XYXY_INCLUSIVE ? 1 : 0));
            }

            // ✅ rawFrame: vis 드로잉 전에 원본 프레임을 clone (깊은 복사)
            const cv::Mat rawFrame = (job.frame_bgr.empty() ? crop : job.frame_bgr).clone();

            cv::Mat& vis = job.frame_bgr.empty() ? crop : job.frame_bgr;
            if (!vis.empty() && c_callback_)
            {
                const cv::Rect bounds(0, 0, vis.cols, vis.rows);
                cv::Rect tab  = job.trig.tab  & bounds;
                cv::Rect horn = job.trig.horn & bounds;
                cv::Rect used = (used_roi.area() > 0) ? (used_roi & bounds) : cv::Rect{};

                if (tab.area() > 0) {
                    cv::rectangle(vis, tab, cv::Scalar(0, 0, 255), 2);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "tab:%.3f", job.trig.tab_score);
                    cv::putText(vis, buf, cv::Point(tab.x, std::max(0, tab.y - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 4);
                }
                if (horn.area() > 0) {
                    cv::rectangle(vis, horn, cv::Scalar(255, 0, 0), 2);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "horn:%.3f", job.trig.horn_score);
                    cv::putText(vis, buf, cv::Point(horn.x, std::max(0, horn.y - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 0, 0), 2);
                }
                if (used.area() > 0)
                    cv::rectangle(vis, used, cv::Scalar(255, 255, 255), 3);

                char probBuf[256];
                std::snprintf(probBuf, sizeof(probBuf),
                    "[%s] %s score=%.3f -> %s",
                    job.ts_input,
                    resultItem.pred_label.c_str(),
                    topScore,
                    resultItem.final_decision.c_str());
                cv::putText(vis, probBuf, cv::Point(20, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
            }

            if (c_callback_)
            {
                int errorCode = ok_probs ? 0 : 1;
                const char* errorMsg = ok_probs ? "OK" : "ParseDefectProbs3 failed";

                ResultItemC c = ToCResult(resultItem);

                ImageDataC visImgData{};
                if (!vis.empty()) {
                    visImgData = MakeImageDataC(vis);
                }

                ImageDataC rawImgDataC{};
                if (!rawFrame.empty()) {
                    rawImgDataC = MakeImageDataC(rawFrame);
                }

                c_callback_(job.frameIndex, &c, errorCode, errorMsg,
                    vis.empty()      ? nullptr : &visImgData,
                    rawFrame.empty() ? nullptr : &rawImgDataC);
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

                std::filesystem::path onnxPath   = isTrigger ? trigger_onnx_path_   : defect_onnx_path_;
                std::filesystem::path enginePath = isTrigger ? trigger_engine_path_ : defect_engine_path_;

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
                    defect_class_names_ = classNames;

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

        if (inputLayout != "NCHW") return fail("unsupported input.layout (expected NCHW): " + inputLayout);
        if (inputShape.size() != 4) return fail("input.shape must have 4 dims for NCHW");
        if (inputShape[2] <= 0 || inputShape[3] <= 0) return fail("input.shape H/W invalid");
        modelH = (int)inputShape[2];
        modelW = (int)inputShape[3];

        if (!j.contains("output") || !j["output"].is_object()) return fail("missing/invalid: output");
        const auto& out = j["output"];
        if (!out.contains("bindings") || !out["bindings"].is_array() || out["bindings"].empty())
            return fail("missing/invalid: output.bindings[0]");

        const auto& b0 = out["bindings"][0];
        if (!b0.is_object()) return fail("missing/invalid: output.bindings[0] object");
        if (!get_str(b0, "name", outputName)) return fail("missing/invalid: output.bindings[0].name");
        if (!get_i64_array(b0, "shape", outputShape)) return fail("missing/invalid: output.bindings[0].shape");
        get_str(b0, "bbox_format", bboxFormat);           // 선택적: 없으면 기본값 유지
        get_int(b0, "class_start_index", classStartIndex); // 선택적: 없으면 기본값(0) 유지

        if (!get_str_array(j, "class_names", classNames)) return fail("missing/invalid: class_names");

        if (!j.contains("trtexec") || !j["trtexec"].is_object()) return fail("missing/invalid: trtexec");
        const auto& te = j["trtexec"];
        if (!get_str(te, "precision", trtPrecision)) return fail("missing/invalid: trtexec.precision");
        if (!get_int(te, "workspace", trtWorkspace)) return fail("missing/invalid: trtexec.workspace");

        std::string onnxStr, engineStr;
        bool okPaths = false;

        if (j.contains("paths") && j["paths"].is_object()) {
            const auto& p = j["paths"];
            if (get_str(p, "onnx", onnxStr) && get_str(p, "engine", engineStr)) okPaths = true;
        }
        if (!okPaths) {
            if (get_str(j, "onnx", onnxStr) && get_str(j, "engine", engineStr)) okPaths = true;
        }

        if (okPaths) {
            auto resolve = [&](const std::string& s) -> std::filesystem::path {
                std::filesystem::path p = std::filesystem::path(s);
                if (p.is_relative()) p = jsonPath.parent_path() / p;
                return p;
            };
            onnxPathOut   = resolve(onnxStr);
            enginePathOut = resolve(engineStr);
        }
        
        return true;
    }

} // namespace HmCutter