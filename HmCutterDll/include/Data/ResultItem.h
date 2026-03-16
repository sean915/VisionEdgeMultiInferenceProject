#pragma once
#include <string>
#include <vector>

namespace HmCutter {

    struct Box {
        unsigned int x1;
        unsigned int y1;
        unsigned int x2;
        unsigned int y2;
    };

    struct PredItem {
        float       score = 0.f;
        std::string label;
        std::string decision;
    };

    struct ResultItem {
        float       pred_score = 0.f;       // top-1 (하위 호환)
        Box         box{};

        std::vector<PredItem> preds;        // 복수 예측 (score 내림차순)
        std::string pred_label;             // top-1 label (하위 호환)
        std::string decision;               // top-1 decision (하위 호환)
        std::string final_decision;
        std::string input_timestamp;        // 입력 프레임 타임스탬프
    };

} // namespace HmCutter