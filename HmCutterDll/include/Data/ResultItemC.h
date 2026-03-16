#pragma once
#include <cstdint>

struct BoxC {
    int x1, y1, x2, y2;
};

#define RESULT_STR_MAX 32
#define RESULT_LABEL_MAX 64
#define RESULT_PREDS_MAX 5       // 복수 예측 최대 개수

struct PredItemC {
    float pred_score;                         // 예측 점수 (0.0~1.0)
    char  pred_label[RESULT_LABEL_MAX];       // 예측 클래스명 (예: "OK_Tab", "NG_Tab")
    char  decision[RESULT_STR_MAX];           // 개별 판정      "OK" | "NG" | "Questionable"
};

struct ResultItemC {
    BoxC  box;
    int   pred_count;                    // 유효 예측 개수 (0 ~ RESULT_PREDS_MAX)
    PredItemC preds[RESULT_PREDS_MAX];   // 복수 예측 결과 (score 내림차순)

    char  final_decision[RESULT_STR_MAX];  // 최종 판정      "OK" | "NG" | "Questionable"
    char  input_timestamp[RESULT_STR_MAX]; // 입력 프레임 타임스탬프 (호출자가 전달한 값)
};

// 콜백으로 전달하는 이미지 데이터 묶음
struct ImageDataC {
    const uint8_t* data;
    int width;
    int height;
    int strideBytes;
    int cvType;
};

