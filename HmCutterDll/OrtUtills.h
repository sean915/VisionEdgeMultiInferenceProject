//#pragma once
//#pragma once
//#include <opencv2/core.hpp>
//#include <vector>
//#include <algorithm>
//#include <onnxruntime_cxx_api.h>
//#include <Detector.cpp>
//#include <Detector.h>
//
//Ort::Value make_blob_nchw(const cv::Mat& img, const Ort::MemoryInfo& memInfo, ONNXTensorElementDataType dtype, std::vector<uint16_t>& fp16buf, std::vector<float>& fp32buf);
//
////void parse_detection_outputs_or_throw(
////    const std::vector<Ort::Value>& outs,
////    const std::vector<const char*>& outNames,
////    const HMSTACK::LetterboxInfo& lb,
////    int img_w, int img_h,
////    bool& has_cell, cv::Rect& best_cell, float& best_cell_score,
////    bool& has_pnp, cv::Rect& best_pnp, float& best_pnp_score
////);