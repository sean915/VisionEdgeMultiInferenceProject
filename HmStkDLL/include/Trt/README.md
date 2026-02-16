# TRT Inference - 백엔드 스위칭 가능한 추론 프레임워크

## 📋 개요

이 프로젝트는 **ONNX Runtime (ORT)**와 **TensorRT (TRT)** 백엔드를 동적으로 전환할 수 있는 추론 프레임워크입니다. Detector의 핵심 로직(`triggerLoop`, `defectLoop`)은 변경 없이 백엔드만 교체하여 사용할 수 있습니다.

### 주요 특징

✅ **Detector / triggerLoop / defectLoop 코드는 거의 안 바꾸고**  
✅ **백엔드만 스위칭 가능 (ORT ↔ TRT)**  
✅ **나중에 engine / onnx / pt 섞여 있어도 대응 가능**  
✅ **DLL / On-prem 배포 최적화**

## 🏗️ 프로젝트 구조

```
trt_inference/
├── Inference/
│   ├── IInferencer.h          # 공통 인터페이스 정의
│   ├── TrtEngine.h/.cpp       # TensorRT 엔진 래퍼
│   ├── TrtInferencer.h/.cpp   # TensorRT 구현체
│   └── OrtInferencer.h/.cpp   # ONNX Runtime 구현체
├── Detector.h                 # 메인 Detector 클래스
├── Detector.cpp               # Detector 구현
└── README.md                  # 프로젝트 문서
```

## 📐 아키텍처

```
Detector
 ├─ InferenceBackend (enum: ORT, TRT)
 ├─ ITriggerInferencer (공통 인터페이스)
 │    ├─ OrtTriggerInferencer
 │    └─ TrtTriggerInferencer
 ├─ IDefectInferencer (공통 인터페이스)
 │    ├─ OrtDefectInferencer
 │    └─ TrtDefectInferencer
 └─ triggerLoop / defectLoop (백엔드 독립)
```

## 🔧 핵심 컴포넌트

### 1. 공통 인터페이스 (`IInferencer.h`)

#### Trigger 추론 인터페이스
```cpp
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
```

#### Defect 추론 인터페이스
```cpp
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
```

### 2. 백엔드 타입
```cpp
enum class InferenceBackend {
    ORT,  // ONNX Runtime
    TRT   // TensorRT
};
```

## 🚀 사용 방법

### 기본 사용법

```cpp
#include "Detector.h"

// Detector 생성
Detector detector;

// 백엔드 설정 (기본값: TRT)
detector.setBackend(InferenceBackend::TRT);  // 또는 InferenceBackend::ORT

// 모델 초기화
detector.model_setup();

// triggerLoop와 defectLoop는 자동으로 백그라운드 스레드에서 실행됨
```

### 백엔드 전환

```cpp
// TensorRT 사용
detector.setBackend(InferenceBackend::TRT);
detector.model_setup();

// ONNX Runtime 사용
detector.setBackend(InferenceBackend::ORT);
detector.model_setup();
```

### 모델 파일 경로

#### TensorRT 모델
- Trigger 모델: `Trigger_Cathode.engine`
- Defect 모델: `Classifier_Anode.engine`

#### ONNX Runtime 모델
- Trigger 모델: `Trigger_Cathode.onnx`
- Defect 모델: `Classifier_Anode.onnx`

## 📝 구현 세부사항

### TensorRT 구현 (`TrtInferencer`)

- CUDA 스트림 기반 비동기 추론
- GPU 메모리 사전 할당으로 성능 최적화
- NCHW 형식 전처리 자동 처리

### ONNX Runtime 구현 (`OrtInferencer`)

- ONNX 모델 직접 로드
- CPU/GPU 자동 선택 (구현에 따라)
- 기존 ORT 코드 재사용 가능

## 🔨 빌드 요구사항

### 필수 의존성

- **C++17** 이상
- **OpenCV** (이미지 처리)
- **CUDA** (TensorRT 사용 시)
- **TensorRT** (TensorRT 백엔드 사용 시)
- **ONNX Runtime** (ORT 백엔드 사용 시)

## 📦 TensorRT 최소 라이브러리 (추론 전용)

이 프로젝트는 이미 빌드된 `.engine` 파일만 사용하므로, 추론에 필요한 최소한의 TensorRT 라이브러리만 필요합니다.

### 필수 파일 (추론에 반드시 필요)

#### 1. 헤더 파일 (`include/`)
```
include/
└── NvInfer.h                    # 핵심 추론 API (IRuntime, ICudaEngine, IExecutionContext)
```

#### 2. 라이브러리 파일 (`lib/`)
```
lib/
└── nvinfer_10.lib               # TensorRT 런타임 라이브러리 (정적 링크용)
```

#### 3. 런타임 DLL (`bin/`)
```
bin/
└── nvinfer_10.dll               # TensorRT 런타임 DLL (실행 시 필요)
```

### 배포 시 최소 파일 구조

```
배포_폴더/
├── bin/
│   └── nvinfer_10.dll          # 필수: 런타임 DLL
├── include/
│   └── NvInfer.h               # 빌드 시 필요 (배포 시 제외 가능)
└── lib/
    └── nvinfer_10.lib          # 빌드 시 필요 (배포 시 제외 가능)
```

### 불필요한 파일 (추론에는 사용 안 함)

다음 파일들은 **빌드/파싱**에만 필요하고 **추론에는 불필요**합니다:

#### ❌ 빌더 관련 (빌드 시에만 필요)
- `nvinfer_builder_resource_*.dll` - 빌더 리소스 (추론 불필요)
- `NvInferImpl.h` - 빌더 구현 (추론 불필요)

#### ❌ 파서 관련 (ONNX → Engine 변환 시에만 필요)
- `nvonnxparser_10.lib` / `nvonnxparser_10.dll` - ONNX 파서
- `NvOnnxParser.h` - ONNX 파서 헤더
- `NvOnnxConfig.h` - ONNX 설정

#### ❌ 플러그인 관련 (커스텀 플러그인 사용 시에만 필요)
- `nvinfer_plugin_10.lib` / `nvinfer_plugin_10.dll` - 플러그인 라이브러리
- `nvinfer_vc_plugin_10.lib` / `nvinfer_vc_plugin_10.dll` - VC 플러그인
- `NvInferPlugin.h` - 플러그인 헤더
- `NvInferPluginBase.h` - 플러그인 베이스
- `NvInferPluginUtils.h` - 플러그인 유틸리티

#### ❌ 기타 최적화 버전 (기본 버전으로 충분)
- `nvinfer_lean_10.*` - Lean 버전 (최적화, 선택적)
- `nvinfer_dispatch_10.*` - Dispatch 버전 (최적화, 선택적)
- `trtexec.exe` - 명령줄 도구 (빌드/테스트용)

### CMake 설정 예시 (최소 라이브러리만 링크)

```cmake
# TensorRT 경로 설정
set(TENSORRT_ROOT "TensorRT-10.14.1.48")

# 헤더 경로
include_directories(${TENSORRT_ROOT}/include)

# 라이브러리 링크 (추론에 필요한 최소 라이브러리만)
target_link_libraries(trt_inference
    ${TENSORRT_ROOT}/lib/nvinfer_10.lib
    # nvonnxparser_10.lib  # ONNX 파싱 불필요 (이미 .engine 파일 사용)
    # nvinfer_plugin_10.lib  # 커스텀 플러그인 불필요
)

# 런타임 DLL 경로 설정 (Windows)
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    file(COPY ${TENSORRT_ROOT}/bin/nvinfer_10.dll
         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()
```

### 배포 시 주의사항

1. **런타임 DLL 필수**: 실행 환경에 `nvinfer_10.dll`이 PATH에 있거나 실행 파일과 같은 폴더에 있어야 합니다.
2. **CUDA 런타임**: TensorRT는 CUDA 런타임에 의존하므로, CUDA 런타임 DLL도 함께 배포해야 합니다.
3. **버전 호환성**: TensorRT 버전과 CUDA 버전이 호환되어야 합니다.

### 파일 크기 비교

- **전체 TensorRT**: ~500MB+
- **최소 추론 라이브러리**: ~50MB (DLL만)
- **절약**: 약 90% 크기 감소

### CMake 예시 (참고용)

```cmake
cmake_minimum_required(VERSION 3.15)
project(trt_inference)

set(CMAKE_CXX_STANDARD 17)

# OpenCV
find_package(OpenCV REQUIRED)

# CUDA (TensorRT 사용 시)
find_package(CUDA REQUIRED)

# TensorRT (선택적)
find_package(TensorRT QUIET)

# ONNX Runtime (선택적)
# find_package(onnxruntime QUIET)

# 소스 파일
add_library(trt_inference
    Detector.cpp
    Inference/TrtEngine.cpp
    Inference/TrtInferencer.cpp
    Inference/OrtInferencer.cpp
)

target_include_directories(trt_inference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(trt_inference
    ${OpenCV_LIBS}
    # CUDA 라이브러리
    # TensorRT 라이브러리
    # ONNX Runtime 라이브러리
)
```

## 📌 주요 설계 원칙

1. **인터페이스 분리**: Detector는 추상 인터페이스만 의존
2. **최소 변경**: `triggerLoop`/`defectLoop`는 백엔드 변경과 무관하게 동작
3. **확장성**: 새로운 백엔드 추가 시 인터페이스만 구현하면 됨
4. **성능**: TensorRT는 CUDA 스트림 기반 비동기 처리로 최적화

## 🔍 코드 예시

### Detector 내부 동작

```cpp
void Detector::model_setup() {
    status_ = AlgorithmStatus::INITIALIZING;

    if (backend_ == InferenceBackend::TRT) {
        trigger_inf_ = std::make_unique<TrtTriggerInferencer>(
            "Trigger_Cathode.engine", trig_in_w_, trig_in_h_);
        defect_inf_ = std::make_unique<TrtDefectInferencer>(
            "Classifier_Anode.engine", defect_in_w_, defect_in_h_);
    } else {
        trigger_inf_ = std::make_unique<OrtTriggerInferencer>(
            L"Trigger_Cathode.onnx");
        defect_inf_ = std::make_unique<OrtDefectInferencer>(
            L"Classifier_Anode.onnx");
    }

    running_ = true;
    trigger_worker_ = std::thread(&Detector::triggerLoop, this);
    defect_worker_ = std::thread(&Detector::defectLoop, this);

    status_ = AlgorithmStatus::READY;
}
```

### Loop에서 사용 (백엔드 독립)

```cpp
void Detector::triggerLoop() {
    while (running_) {
        cv::Mat frame_bgr = getFrameFromQueue();
        auto det = trigger_inf_->infer(frame_bgr);  // 인터페이스만 사용
        
        if (det.has_cell) {
            // 처리 로직
        }
    }
}

void Detector::defectLoop() {
    while (running_) {
        cv::Mat crop = getCropFromQueue();
        float p_ab, p_no, p_em;
        defect_inf_->infer(crop, p_ab, p_no, p_em);  // 인터페이스만 사용
        
        auto dec = evaluate_defect_cpp(p_ab, p_no, p_em, sure_, min_valid_);
    }
}
```

## ⚠️ 주의사항

1. **모델 파일 경로**: 현재 하드코딩되어 있으므로, 필요시 설정 파일이나 생성자 파라미터로 변경
2. **출력 크기**: `TrtTriggerInferencer`의 `OUTPUT_SIZE`는 실제 모델 출력 크기에 맞게 조정 필요
3. **파싱 로직**: `TrtTriggerInferencer::infer()`에서 YOLO 출력 파싱 로직 추가 필요
4. **ORT 구현**: `OrtInferencer.cpp`의 `OrtSessionWrap` 클래스를 실제 ORT API로 교체 필요

## 🔄 확장 가능성

### 새로운 백엔드 추가

새로운 백엔드(예: PyTorch C++, LibTorch)를 추가하려면:

1. `IInferencer.h`의 인터페이스 구현
2. `InferenceBackend` enum에 새 타입 추가
3. `Detector::model_setup()`에 분기 추가

예시:
```cpp
enum class InferenceBackend {
    ORT,
    TRT,
    TORCH  // 새 백엔드
};

// Detector::model_setup()에 추가
else if (backend_ == InferenceBackend::TORCH) {
    trigger_inf_ = std::make_unique<TorchTriggerInferencer>(...);
    defect_inf_ = std::make_unique<TorchDefectInferencer>(...);
}
```

## 📄 라이선스

[라이선스 정보를 여기에 추가하세요]

## 👥 기여

[기여 가이드라인을 여기에 추가하세요]

