#include "inference_api.h"
#include "infer_engine.h"
#include <mutex>
#include <string>
#include <excpt.h>
#include <WTypesbase.h>
#include <onnxruntime_c_api.h>

static std::mutex g_mtx;
static InferEngine g_engine;
static std::string g_lastErr;

extern "C" __declspec(dllexport) int __cdecl infer_init(
    const wchar_t* onnxPath, int useCuda, int deviceId, int inputW, int inputH)
{
    //__try {
        std::lock_guard<std::mutex> lk(g_mtx);

        if (!onnxPath || onnxPath[0] == L'\0') {
            g_lastErr = "onnxPath is null/empty";
            return -1;
        }

        // 여기서 네 InferEngine::init 호출
        std::string err;
        bool ok = g_engine.init(onnxPath, useCuda != 0, deviceId, inputW, inputH, err);
        if (!ok) {
            g_lastErr = err;
            return -2;
        }

        g_lastErr.clear();
        return 0;
   // }
 /*   __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        char buf[256];
        sprintf_s(buf, "SEH in infer_init. code=0x%08X", (unsigned)code);
        g_lastErr = buf;
        return -99;
    }*/
}

extern "C" DLL_EXPORT void __cdecl infer_shutdown()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_engine.shutdown();
}

extern "C" DLL_EXPORT const char* __cdecl infer_last_error()
{
    return g_lastErr.c_str();
}

// 엔진을 다른 cpp에서 쓰고 싶으면 getter 제공(내부용)
InferEngine* GetGlobalInferEngine()
{
    return &g_engine;
}

extern "C" __declspec(dllexport) int __cdecl native_ping()
{
    return 1234;
}

extern "C" __declspec(dllexport) const char* __cdecl ort_version()
{
    const OrtApiBase* b = OrtGetApiBase();
    if (!b) return "OrtGetApiBase=null";
    const char* v = b->GetVersionString();
    return v ? v : "GetVersionString=null";
}



// InferEngine 멤버에 있어야 함(예시)
// const OrtApi* m_api;
// OrtSession* m_sess;

static std::string TensorElemTypeToStr(ONNXTensorElementDataType t) {
    switch (t) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return "uint8";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8: return "int8";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16: return "uint16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16: return "int16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "int32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "float16";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: return "double";
    default: return "other";
    }
}

//static std::string ShapeToStr(const std::vector<int64_t>& d) {
//    std::ostringstream oss;
//    oss << "[";
//    for (size_t i = 0; i < d.size(); ++i) {
//        oss << d[i];
//        if (i + 1 < d.size()) oss << ", ";
//    }
//    oss << "]";
//    return oss.str();
//}
//
//bool InferEngine::dump_io(std::string& out, std::string& err)
//{
//    if (!m_api || !m_sess) { err = "ORT not ready"; return false; }
//
//    OrtAllocator* alloc = nullptr;
//    OrtStatus* st = m_api->GetAllocatorWithDefaultOptions(&alloc);
//    if (st) {
//        err = std::string("GetAllocatorWithDefaultOptions: ") + m_api->GetErrorMessage(st);
//        m_api->ReleaseStatus(st);
//        return false;
//    }
//
//    std::ostringstream oss;
//
//    // ---- Inputs ----
//    size_t inCount = 0;
//    st = m_api->SessionGetInputCount(m_sess, &inCount);
//    if (st) {
//        err = std::string("SessionGetInputCount: ") + m_api->GetErrorMessage(st);
//        m_api->ReleaseStatus(st);
//        return false;
//    }
//    oss << "Inputs=" << inCount << "\n";
//
//    for (size_t i = 0; i < inCount; ++i) {
//        char* name = nullptr;
//        st = m_api->SessionGetInputName(m_sess, i, alloc, &name);
//        if (st) { err = std::string("SessionGetInputName: ") + m_api->GetErrorMessage(st); m_api->ReleaseStatus(st); return false; }
//
//        OrtTypeInfo* ti = nullptr;
//        st = m_api->SessionGetInputTypeInfo(m_sess, i, &ti);
//        if (st) { err = std::string("SessionGetInputTypeInfo: ") + m_api->GetErrorMessage(st); m_api->ReleaseStatus(st); return false; }
//
//        const OrtTensorTypeAndShapeInfo* tsh = m_api->CastTypeInfoToTensorInfo(ti);
//        ONNXTensorElementDataType elemType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
//        if (tsh) elemType = m_api->GetTensorElementType(tsh);
//
//        size_t dimCount = 0;
//        if (tsh) m_api->GetDimensionsCount(tsh, &dimCount);
//
//        std::vector<int64_t> dims(dimCount);
//        if (tsh && dimCount > 0) m_api->GetDimensions(tsh, dims.data(), dimCount);
//
//        oss << "  IN[" << i << "] name=" << (name ? name : "")
//            << " type=" << TensorElemTypeToStr(elemType)
//            << " shape=" << ShapeToStr(dims) << "\n";
//
//        if (name) alloc->Free(alloc, name);
//        m_api->ReleaseTypeInfo(ti);
//    }
//
//    // ---- Outputs ----
//    size_t outCount = 0;
//    st = m_api->SessionGetOutputCount(m_sess, &outCount);
//    if (st) {
//        err = std::string("SessionGetOutputCount: ") + m_api->GetErrorMessage(st);
//        m_api->ReleaseStatus(st);
//        return false;
//    }
//    oss << "Outputs=" << outCount << "\n";
//
//    for (size_t i = 0; i < outCount; ++i) {
//        char* name = nullptr;
//        st = m_api->SessionGetOutputName(m_sess, i, alloc, &name);
//        if (st) { err = std::string("SessionGetOutputName: ") + m_api->GetErrorMessage(st); m_api->ReleaseStatus(st); return false; }
//
//        OrtTypeInfo* ti = nullptr;
//        st = m_api->SessionGetOutputTypeInfo(m_sess, i, &ti);
//        if (st) { err = std::string("SessionGetOutputTypeInfo: ") + m_api->GetErrorMessage(st); m_api->ReleaseStatus(st); return false; }
//
//        const OrtTensorTypeAndShapeInfo* tsh = m_api->CastTypeInfoToTensorInfo(ti);
//        ONNXTensorElementDataType elemType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
//        if (tsh) elemType = m_api->GetTensorElementType(tsh);
//
//        size_t dimCount = 0;
//        if (tsh) m_api->GetDimensionsCount(tsh, &dimCount);
//
//        std::vector<int64_t> dims(dimCount);
//        if (tsh && dimCount > 0) m_api->GetDimensions(tsh, dims.data(), dimCount);
//
//        oss << "  OUT[" << i << "] name=" << (name ? name : "")
//            << " type=" << TensorElemTypeToStr(elemType)
//            << " shape=" << ShapeToStr(dims) << "\n";
//
//        if (name) alloc->Free(alloc, name);
//        m_api->ReleaseTypeInfo(ti);
//    }
//
//    out = oss.str();
//    return true;
//}
//
//// inference_api.cpp
//static std::string g_ioDump;
//
//extern "C" __declspec(dllexport) const char* __cdecl infer_dump_io()
//{
//    std::lock_guard<std::mutex> lk(g_mtx);
//    std::string err, out;
//    if (!g_engine.dump_io(out, err)) {
//        g_lastErr = err;
//        g_ioDump = "";
//        return "";
//    }
//    g_lastErr.clear();
//    g_ioDump = out;
//    return g_ioDump.c_str();
//}