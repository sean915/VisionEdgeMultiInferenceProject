using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI.Infrastructure.Interop
{
    /// <summary>
    /// VideoReceiverDll.dll P/Invoke 선언만 담당.
    /// PollingService에서 분리하여 단일 책임 유지.
    /// </summary>
    internal static class RtspNativeDefinitions
    {
        private const string DllName = "VideoReceiverDll.dll";

        internal enum RtspLogLevel : int { Info = 1, Warn = 2, Error = 3 }
        internal enum RtspPixelFormat : int { BGRA32 = 0, BGR24 = 1 }

        [StructLayout(LayoutKind.Sequential)]
        internal struct RtspOptions
        {
            public int tcp_only;
            public int stimeout_us;
            public int reconnect;
            public int reconnect_wait_ms;
            public int target_fps;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct RtspOptions2
        {
            public uint struct_size;
            public int tcp_only;
            public int stimeout_us;
            public int reconnect;
            public int reconnect_wait_ms;
            public int target_fps;
            public RtspPixelFormat out_pixfmt;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void FrameCallback(IntPtr user, int channelId, IntPtr bgra, int w, int h, int stride, long pts_ms);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void FrameCallbackEx(IntPtr user, int channelId, IntPtr data, int w, int h, int stride, long pts_ms, RtspPixelFormat pixfmt);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void StatusCallback(IntPtr user, int channelId, RtspLogLevel level, int code, [MarshalAs(UnmanagedType.LPStr)] string message);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rtsp_default_options(ref RtspOptions opt);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rtsp_default_options2(ref RtspOptions2 opt);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr rtsp_create(int channelId, [MarshalAs(UnmanagedType.LPStr)] string url, ref RtspOptions opt, FrameCallback onFrame, StatusCallback onStatus, IntPtr user);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr rtsp_create2(int channelId, [MarshalAs(UnmanagedType.LPStr)] string url, ref RtspOptions2 opt, FrameCallbackEx onFrame, StatusCallback onStatus, IntPtr user);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int rtsp_start(IntPtr h);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rtsp_stop(IntPtr h);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void rtsp_destroy(IntPtr h);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        internal static extern int rtsp_request_clip(IntPtr h, long center_pts_ms, int pre_ms, int post_ms, string out_path);
    }
}