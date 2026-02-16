using System;
using System.Runtime.InteropServices;

internal static class RtspNative
{
    private const string DllName = "VideoReceiverDll.dll";

    public enum RtspLogLevel : int
    {
        Info = 1,
        Warn = 2,
        Error = 3
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RtspOptions
    {
        public int tcp_only;
        public int stimeout_us;
        public int reconnect;
        public int reconnect_wait_ms;
        public int target_fps;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void FrameCallback(
        IntPtr user, int channelId,
        IntPtr bgra, int width, int height, int stride, long ptsMs
    );

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void StatusCallback(
        IntPtr user, int channelId, RtspLogLevel level, int code,
        [MarshalAs(UnmanagedType.LPStr)] string message
    );

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rtsp_default_options(ref RtspOptions opt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr rtsp_create(
        int channelId,
        [MarshalAs(UnmanagedType.LPStr)] string rtspUrl,
        ref RtspOptions opt,
        FrameCallback onFrame,
        StatusCallback onStatus,
        IntPtr user
    );

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int rtsp_start(IntPtr h);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rtsp_stop(IntPtr h);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void rtsp_destroy(IntPtr h);
}
