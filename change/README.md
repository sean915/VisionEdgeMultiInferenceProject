# Initialize / Run / Stop 분리 변경사항

## 개요
Initialize에서 워커 스레드(triggerLoop, defectLoop) 시작을 분리하여 Run/Stop API를 추가했습니다.
멈춤 후 재시작 시 Initialize 재호출 없이 Run만 호출하면 됩니다.

## 변경된 파일 목록
- `HmCutterDll/Detector.h`
- `HmCutterDll/Detector.cpp`
- `HmCutterDll/AlgorithmModule.cpp`
- `InferenceClientUI/InfraStructure/Interop/AlgorithmNative.cs`
- `InferenceClientUI/InfraStructure/Interop/AlgorithmSession.cs`

## API 호출 순서
1. CreateAlgorithm(config)
2. SetResultCallback(handle, cb)
3. Initialize(handle)  ← 모델 로드만 (워커 미시작)
4. Run(handle)         ← 워커 시작
5. PushFrame(handle, frame)  ← Run 상태에서만 유효
6. Stop(handle)        ← 멈춤 시
7. Destroy(handle)     ← 종료 시

**멈춤 후 재시작**: Stop → Run (Initialize 불필요)
