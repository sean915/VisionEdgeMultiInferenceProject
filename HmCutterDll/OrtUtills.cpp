//#pragma once
//#pragma once
//#include "OrtUtills.h"
//
//
//// NCHW 텐서 생성 (float/float16 지원, 입력은 BGR HWC)
//inline Ort::Value make_blob_nchw(const cv::Mat& img, const Ort::MemoryInfo& memInfo, ONNXTensorElementDataType dtype,
//    std::vector<uint16_t>& fp16buf, std::vector<float>& fp32buf) 
//{
//    int h = img.rows, w = img.cols, c = img.channels();
//    std::vector<int64_t> shape = { 1, c, h, w };
//
//    if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
//        fp32buf.resize(c * h * w);
//        // HWC(BGR) -> NCHW
//        for (int y = 0; y < h; ++y) {
//            for (int x = 0; x < w; ++x) {
//                for (int ch = 0; ch < c; ++ch) {
//                    fp32buf[ch * h * w + y * w + x] = img.ptr<uchar>(y)[x * c + ch] / 255.0f;
//                }
//            }
//        }
//        auto tensor = Ort::Value::CreateTensor(memInfo, fp32buf.data(), fp32buf.size() * sizeof(float), shape.data(), shape.size(), dtype);
//        return std::move(tensor);
//    }
//    else if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
//        fp16buf.resize(c * h * w);
//        // HWC(BGR) -> NCHW, float->float16
//        for (int y = 0; y < h; ++y) {
//            for (int x = 0; x < w; ++x) {
//                for (int ch = 0; ch < c; ++ch) {
//                    float v = img.ptr<uchar>(y)[x * c + ch] / 255.0f;
//                    //fp16buf[ch * h * w + y * w + x] = Ort::Float16_t(v).value;
//                }
//            }
//        }
//        auto tensor = Ort::Value::CreateTensor(memInfo, fp16buf.data(), fp16buf.size() * sizeof(uint16_t), shape.data(), shape.size(), dtype);
//        return std::move(tensor);
//    }
//    else {
//        throw std::runtime_error("지원하지 않는 dtype입니다.");
//    }
//}
//
////void parse_detection_outputs_or_throw(const std::vector<Ort::Value>& outs, const std::vector<const char*>& outNames, const HMSTACK::LetterboxInfo& lb, int img_w, int img_h, bool& has_cell, cv::Rect& best_cell, float& best_cell_score, bool& has_pnp, cv::Rect& best_pnp, float& best_pnp_score)
////{
////}
//
////void parse_detection_outputs_or_throw(const std::vector<Ort::Value>& outs, const std::vector<const char*>& outNames, const HMSTACK::LetterboxInfo& lb, int img_w, int img_h, bool& has_cell, cv::Rect& best_cell, float& best_cell_score, bool& has_pnp, cv::Rect& best_pnp, float& best_pnp_score)
////{
////    // 기본적으로 "boxes", "labels", "scores" output이 있다고 가정
////   // (YOLO 등 단일 output 모델은 별도 파서 필요)
////    const float* boxes = nullptr;
////    const int64_t* labels = nullptr;
////    const float* scores = nullptr;
////    size_t num = 0;
////
////    for (size_t i = 0; i < outNames.size(); ++i) {
////        std::string name(outNames[i]);
////        if (name == "boxes") {
////            boxes = outs[i].GetTensorData<float>();
////            auto shape = outs[i].GetTensorTypeAndShapeInfo().GetShape();
////            if (shape.size() == 2 && shape[1] == 4) num = shape[0];
////        }
////        else if (name == "labels") {
////            labels = outs[i].GetTensorData<int64_t>();
////        }
////        else if (name == "scores") {
////            scores = outs[i].GetTensorData<float>();
////        }
////    }
////    if (!boxes || !labels || !scores || num == 0)
////        throw std::runtime_error("Detection output tensor 파싱 실패");
////
////    has_cell = false;
////    has_pnp = false;
////    best_cell_score = 0.f;
////    best_pnp_score = 0.f;
////
////    for (size_t i = 0; i < num; ++i) {
////        int label = static_cast<int>(labels[i]);
////        float score = scores[i];
////        // letterbox 좌표를 원본 이미지 좌표로 복원
////        float x1 = (boxes[i * 4 + 0] - lb.pad_left) / lb.scale;
////        float y1 = (boxes[i * 4 + 1] - lb.pad_top) / lb.scale;
////        float x2 = (boxes[i * 4 + 2] - lb.pad_left) / lb.scale;
////        float y2 = (boxes[i * 4 + 3] - lb.pad_top) / lb.scale;
////        x1 = std::clamp(x1, 0.f, static_cast<float>(img_w - 1));
////        y1 = std::clamp(y1, 0.f, static_cast<float>(img_h - 1));
////        x2 = std::clamp(x2, 0.f, static_cast<float>(img_w - 1));
////        y2 = std::clamp(y2, 0.f, static_cast<float>(img_h - 1));
////        cv::Rect rect(cv::Point(static_cast<int>(x1), static_cast<int>(y1)),
////            cv::Point(static_cast<int>(x2), static_cast<int>(y2)));
////
////        // label 정책: 0=cell, 1=pnp (예시)
////        if (label == 0 && score > best_cell_score) {
////            has_cell = true;
////            best_cell = rect;
////            best_cell_score = score;
////        }
////        else if (label == 1 && score > best_pnp_score) {
////            has_pnp = true;
////            best_pnp = rect;
////            best_pnp_score = score;
////        }
////}