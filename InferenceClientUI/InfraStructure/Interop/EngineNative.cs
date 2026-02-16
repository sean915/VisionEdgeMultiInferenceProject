using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI
{
    internal static class EngineNative
    {
#if EngineDLLMode
        private const string Dll = "EngineDll.dll";

        [StructLayout(LayoutKind.Sequential)]
        internal struct FrameDesc
        {
            public int width;
            public int height;
            public int stride;
            public int format;         // 1 = BGRA32
            public long timestamp_ms;
        }

        [DllImport(Dll, EntryPoint = "Engine_Create", CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Engine_Create(int max_channels);

        [DllImport(Dll, EntryPoint = "Engine_Destroy", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Engine_Destroy(IntPtr h);

        [DllImport(Dll, EntryPoint = "Engine_StartRtsp", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Engine_StartRtsp(
            IntPtr h,
            int channel,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string rtsp_url);

        [DllImport(Dll, EntryPoint = "Engine_StopRtsp", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Engine_StopRtsp(IntPtr h, int channel);

        [DllImport(Dll, EntryPoint = "Engine_CopyLatestFrameBGRA", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Engine_CopyLatestFrameBGRA(
            IntPtr h,
            int channel,
            IntPtr dst,
            int dst_size,
            out FrameDesc out_desc);

        [DllImport(Dll, EntryPoint = "Engine_InitController", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Engine_InitController();

        [DllImport(Dll, EntryPoint = "getInferenceClient", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void getInferenceClient(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

#else
        // ✅ EngineDLLMode OFF: 컴파일만 되게 하는 더미(스텁)
        internal struct FrameDesc
        {
            public int width;
            public int height;
            public int stride;
            public int format;
            public long timestamp_ms;
        }

        internal static IntPtr Engine_Create(int max_channels) => IntPtr.Zero;
        internal static void Engine_Destroy(IntPtr h) { }
        internal static int Engine_StartRtsp(IntPtr h, int channel, string rtsp_url) => 0;
        internal static int Engine_StopRtsp(IntPtr h, int channel) => 0;

        internal static int Engine_CopyLatestFrameBGRA(
            IntPtr h,
            int channel,
            IntPtr dst,
            int dst_size,
            out FrameDesc out_desc)
        {
            out_desc = default;
            return 0;
        }

        internal static void Engine_InitController() { }
        internal static void getInferenceClient(string name) { }
#endif
    }
}
