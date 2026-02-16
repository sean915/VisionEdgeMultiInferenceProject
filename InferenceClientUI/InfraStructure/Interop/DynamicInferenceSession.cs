using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI.Infrastructure.Interop
{
    /// <summary>
    /// NativeLibrary를 사용하여 런타임에 HmStkDLL 또는 HmCutterDLL을 로드하고,
    /// 동일한 C API 함수 포인터를 바인딩하는 세션 래퍼.
    /// </summary>
    internal sealed class DynamicInferenceSession : IDisposable
    {
        // ── 함수 포인터 delegate 정의 ──

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr PFN_CreateAlgorithm(ref InferenceNative.AlgorithmConfig config);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PFN_Initialize(IntPtr handle);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PFN_Run(IntPtr handle);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PFN_Stop(IntPtr handle);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PFN_PushFrame(IntPtr handle, ref InferenceNative.FrameData frame);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void PFN_SetResultCallback(IntPtr handle, InferenceNative.ResultCallbackFunc cb);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PFN_GetStatus(IntPtr handle);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr PFN_GetVersion();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void PFN_Destroy(IntPtr handle);

        // ── 바인딩된 함수들 ──
        private readonly PFN_CreateAlgorithm _createAlgorithm;
        private readonly PFN_Initialize _initialize;
        private readonly PFN_Run _run;
        private readonly PFN_Stop _stop;
        private readonly PFN_PushFrame _pushFrame;
        private readonly PFN_SetResultCallback _setResultCallback;
        private readonly PFN_GetStatus _getStatus;
        private readonly PFN_GetVersion _getVersion;
        private readonly PFN_Destroy _destroy;

        // ── 상태 ──
        private IntPtr _dllHandle;
        private IntPtr _algoHandle;
        private IntPtr _baseDirStr;
        private InferenceNative.ResultCallbackFunc? _cb; // GC 방지

        public IntPtr Handle => _algoHandle;
        public string DllName { get; }

        public DynamicInferenceSession(
            ModelType modelType,
            string baseDirPath,
            float confThreshold,
            bool useCuda,
            InferenceNative.ResultCallbackFunc cb)
        {
            if (string.IsNullOrWhiteSpace(baseDirPath))
                throw new ArgumentException("baseDirPath is required.", nameof(baseDirPath));

            DllName = modelType.ToDllName();

            // 1) DLL 로드
            _dllHandle = NativeLibrary.Load(DllName);
            if (_dllHandle == IntPtr.Zero)
                throw new InvalidOperationException($"Failed to load {DllName}");

            // 2) 함수 바인딩
            _createAlgorithm = GetDelegate<PFN_CreateAlgorithm>("CreateAlgorithm");
            _initialize = GetDelegate<PFN_Initialize>("Initialize");
            _run = GetDelegate<PFN_Run>("Run");
            _stop = GetDelegate<PFN_Stop>("Stop");
            _pushFrame = GetDelegate<PFN_PushFrame>("PushFrame");
            _setResultCallback = GetDelegate<PFN_SetResultCallback>("SetResultCallback");
            _getStatus = GetDelegate<PFN_GetStatus>("GetStatus");
            _getVersion = GetDelegate<PFN_GetVersion>("GetVersion");
            _destroy = GetDelegate<PFN_Destroy>("Destroy");

            // 3) 알고리즘 생성
            _baseDirStr = Marshal.StringToHGlobalAnsi(baseDirPath);
            _cb = cb;

            var cfg = new InferenceNative.AlgorithmConfig
            {
                baseDirPath = _baseDirStr,
                confThreshold = confThreshold,
                useCuda = useCuda ? 1 : 0,
                struct_size = (uint)Marshal.SizeOf<InferenceNative.AlgorithmConfig>()
            };

            _algoHandle = _createAlgorithm(ref cfg);
            if (_algoHandle == IntPtr.Zero)
                throw new InvalidOperationException($"CreateAlgorithm failed ({DllName})");

            _setResultCallback(_algoHandle, _cb);

            int rc = _initialize(_algoHandle);
            if (rc != 0)
                throw new InvalidOperationException($"Initialize failed: {rc} ({DllName})");
        }

        public int Run()
        {
            return _run(_algoHandle);
        }

        public int Stop()
        {
            return _stop(_algoHandle);
        }

        public int GetStatus()
        {
            return _getStatus(_algoHandle);
        }

        public string? GetVersion()
        {
            IntPtr ptr = _getVersion();
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        public int PushBgr(ulong index, IntPtr bgr, int w, int h, long epochMs)
        {
            var fd = new InferenceNative.FrameData
            {
                index = index,
                data = bgr,
                width = w,
                height = h,
                timestamp = epochMs
            };
            return _pushFrame(_algoHandle, ref fd);
        }

        public void Dispose()
        {
            if (_algoHandle != IntPtr.Zero)
            {
                try { _destroy(_algoHandle); } catch { }
                _algoHandle = IntPtr.Zero;
            }

            if (_baseDirStr != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_baseDirStr);
                _baseDirStr = IntPtr.Zero;
            }

            _cb = null;

            if (_dllHandle != IntPtr.Zero)
            {
                try { NativeLibrary.Free(_dllHandle); } catch { }
                _dllHandle = IntPtr.Zero;
            }
        }

        private T GetDelegate<T>(string functionName) where T : Delegate
        {
            if (!NativeLibrary.TryGetExport(_dllHandle, functionName, out IntPtr ptr) || ptr == IntPtr.Zero)
                throw new EntryPointNotFoundException($"{functionName} not found in {DllName}");

            return Marshal.GetDelegateForFunctionPointer<T>(ptr);
        }
    }
}
