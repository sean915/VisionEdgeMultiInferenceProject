using InferenceClientUI.Infrastructure.Interop;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace InferenceClientUI.InfraStructure.Interop
{
    internal class InferenceSession
    {
        private IntPtr _h = IntPtr.Zero;

        private IntPtr _trigPath = IntPtr.Zero;
        private IntPtr _defPath = IntPtr.Zero;

        private InferenceNative.ResultCallbackFunc? _cb; // GC 방지

        public IntPtr Handle => _h;

        public InferenceSession(string triggerModelPath, string defectModelPath, float confThreshold, bool useCuda,
            InferenceNative.ResultCallbackFunc cb)
        {
            if (string.IsNullOrWhiteSpace(triggerModelPath)) throw new ArgumentException(nameof(triggerModelPath));
            if (string.IsNullOrWhiteSpace(defectModelPath)) throw new ArgumentException(nameof(defectModelPath));

            _trigPath = Marshal.StringToHGlobalAnsi(triggerModelPath);
            _defPath = Marshal.StringToHGlobalAnsi(defectModelPath);

            _cb = cb;

            var cfg = new InferenceNative.AlgorithmConfig
            {
                triggerModelPath = _trigPath,
                defectModelPath = _defPath,
                confThreshold = confThreshold,
                useCuda = useCuda ? 1 : 0,
                struct_size = (uint)Marshal.SizeOf<InferenceNative.AlgorithmConfig>()
            };

            _h = InferenceNative.CreateAlgorithm(ref cfg);
            if (_h == IntPtr.Zero) throw new InvalidOperationException("CreateAlgorithm failed");

            InferenceNative.SetResultCallback(_h, _cb);

            int rc = InferenceNative.Initialize(_h);
            if (rc != 0) throw new InvalidOperationException($"Initialize failed: {rc}");
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
            return InferenceNative.PushFrame(_h, ref fd);
        }

        public void Dispose()
        {
            if (_h != IntPtr.Zero)
            {
                try { InferenceNative.Destroy(_h); } catch { }
                _h = IntPtr.Zero;
            }
            if (_trigPath != IntPtr.Zero) { Marshal.FreeHGlobal(_trigPath); _trigPath = IntPtr.Zero; }
            if (_defPath != IntPtr.Zero) { Marshal.FreeHGlobal(_defPath); _defPath = IntPtr.Zero; }
            _cb = null;
        }
    }
}
