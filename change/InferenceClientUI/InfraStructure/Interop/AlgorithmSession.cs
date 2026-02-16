using System;
using System.Runtime.InteropServices;

namespace InferenceClientUI.Infrastructure.Interop
{
    internal sealed class AlgorithmSession : IDisposable
    {
        private IntPtr _h = IntPtr.Zero;

        private IntPtr _trigPath = IntPtr.Zero;
        private IntPtr _defPath = IntPtr.Zero;

        private AlgorithmNative.ResultCallbackFunc? _cb; // GC 방지

        public IntPtr Handle => _h;

        public AlgorithmSession(string triggerModelPath, string defectModelPath, float confThreshold, bool useCuda,
            AlgorithmNative.ResultCallbackFunc cb)
        {
            if (string.IsNullOrWhiteSpace(triggerModelPath)) throw new ArgumentException(nameof(triggerModelPath));
            if (string.IsNullOrWhiteSpace(defectModelPath)) throw new ArgumentException(nameof(defectModelPath));

            _trigPath = Marshal.StringToHGlobalAnsi(triggerModelPath);
            _defPath = Marshal.StringToHGlobalAnsi(defectModelPath);

            _cb = cb;

            var cfg = new AlgorithmNative.AlgorithmConfigNative
            {
                triggerModelPath = _trigPath,
                defectModelPath = _defPath,
                confThreshold = confThreshold,
                useCuda = useCuda ? 1 : 0,
                struct_size = (uint)Marshal.SizeOf<AlgorithmNative.AlgorithmConfigNative>()
            };

            _h = AlgorithmNative.CreateAlgorithm(ref cfg);
            if (_h == IntPtr.Zero) throw new InvalidOperationException("CreateAlgorithm failed");

            AlgorithmNative.SetResultCallback(_h, _cb);

            int rc = AlgorithmNative.Initialize(_h);
            if (rc != 0) throw new InvalidOperationException($"Initialize failed: {rc}");

            rc = AlgorithmNative.Run(_h);
            if (rc != 0) throw new InvalidOperationException($"Run failed: {rc}");
        }

        public int Run()
        {
            if (_h == IntPtr.Zero) return -1;
            return AlgorithmNative.Run(_h);
        }

        public int Stop()
        {
            if (_h == IntPtr.Zero) return -1;
            return AlgorithmNative.Stop(_h);
        }

        public int PushBgr(ulong index, IntPtr bgr, int w, int h, long epochMs)
        {
            var fd = new AlgorithmNative.FrameData
            {
                index = index,
                data = bgr,
                width = w,
                height = h,
                timestamp = epochMs
            };
            return AlgorithmNative.PushFrame(_h, ref fd);
        }

        public void Dispose()
        {
            if (_h != IntPtr.Zero)
            {
                try { AlgorithmNative.Stop(_h); } catch { }
                try { AlgorithmNative.Destroy(_h); } catch { }
                _h = IntPtr.Zero;
            }
            if (_trigPath != IntPtr.Zero) { Marshal.FreeHGlobal(_trigPath); _trigPath = IntPtr.Zero; }
            if (_defPath != IntPtr.Zero) { Marshal.FreeHGlobal(_defPath); _defPath = IntPtr.Zero; }
            _cb = null;
        }
    }
}
