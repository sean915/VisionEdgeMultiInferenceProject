using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI.Infrastructure.Interop
{
    internal static class InferenceNative
    {
        private const string DllName = "HmCutterDLL.dll";

        [StructLayout(LayoutKind.Sequential)]
        public struct FrameData
        {
            public ulong index;
            public IntPtr data;      // uint8_t*
            public int width;
            public int height;
            public long timestamp;   // epoch ms
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct BoxC
        {
            public int x1, y1, x2, y2;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ResultItemC
        {
            public int defect_type;
            public float score;
            public BoxC box;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct AlgorithmConfig
        {
            public IntPtr baseDirPath;       // const char*
            public float confThreshold;
            public int useCuda;
            public uint struct_size;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void ResultCallbackFunc(
            ulong frameIndex,
            IntPtr results,     // ResultItemC*
            int errorCode,
            IntPtr errorMsg,    // const char*
            IntPtr imgData,     // uint8_t*
            int imgW,
            int imgH,
            int imgStrideBytes,
            int imgCvType
        );

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr CreateAlgorithm(ref AlgorithmConfig config);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int Initialize(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int Run(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int Stop(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int PushFrame(IntPtr handle, ref FrameData frame);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SetResultCallback(IntPtr handle, ResultCallbackFunc cb);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GetStatus(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr GetVersion();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr handle);

    }
}
