using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI.Infrastructure.Interop
{
    internal static class AlgorithmNative
    {
        private const string Dll = "AlgorithmModule.dll"; // 실제 추론 DLL 이름으로 변경

        [StructLayout(LayoutKind.Sequential)]
        internal struct FrameData
        {
            public ulong index;
            public IntPtr data;     // BGR pointer (caller-owned)
            public int width;
            public int height;
            public long timestamp;  // epoch ms
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct BoxC
        {
            public int x1, y1, x2, y2;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ResultItemC
        {
            public int defect_type;
            public float score;
            public BoxC box;
        }


        [StructLayout(LayoutKind.Sequential)]
        internal struct AlgorithmConfig
        {
            public IntPtr triggerModelPath;
            public IntPtr defectModelPath;
            public float confThreshold;
            public int useCuda;
            public uint struct_size;
        }


        [StructLayout(LayoutKind.Sequential)]
        internal struct AlgorithmConfigNative
        {
            public IntPtr triggerModelPath; // char*
            public IntPtr defectModelPath;  // char*
            public float confThreshold;
            public int useCuda;             // 0/1
            public uint struct_size;        // ABI guard
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ResultCallbackFunc(
            ulong frameIndex,
            int resultCount,
            IntPtr results,         // ResultItemC*
            int errorCode,
            IntPtr errorMsg,        // char*
            IntPtr imgData,         // uint8_t*
            int imgW,
            int imgH,
            int imgStrideBytes,
            int imgCvType
        );

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr CreateAlgorithm(ref AlgorithmConfigNative config);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Initialize(IntPtr handle);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Run(IntPtr handle);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Stop(IntPtr handle);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int PushFrame(IntPtr handle, ref FrameData frame);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void SetResultCallback(IntPtr handle, ResultCallbackFunc cb);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int GetStatus(IntPtr handle);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr GetVersion();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Destroy(IntPtr handle);
    }
}
