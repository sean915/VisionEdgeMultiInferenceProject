using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace InferenceClientUI.Services
{
    internal sealed class NgSaveJob
    {
        public string JpegPath = "";
        public string CsvPath = "";
        public string ClipPath = "";
        public int ClipRequestResult = -999;

        public int ChannelIndex;
        public ulong FrameIndex;
        public long CenterPtsMs;
        public int Ng, Ok, None;

        public byte[]? ImgBuf;
        public int ImgBytes;
        public int W, H, Stride;
        public int CvType;
        public int JpegQuality = 80;

        public void Dispose()
        {
            if (ImgBuf != null)
            {
                ArrayPool<byte>.Shared.Return(ImgBuf);
                ImgBuf = null;
            }
        }
    }

    internal static class NgSaveWorker
    {
        private const int CV_8UC3 = 16;
        private const int CV_8UC4 = 24;

        private static readonly BlockingCollection<NgSaveJob> _q =
            new(new ConcurrentQueue<NgSaveJob>(), boundedCapacity: 200);

        static NgSaveWorker()
        {
            var t = new Thread(Loop) { IsBackground = true, Name = "NgSaveWorker" };
            t.Start();
        }

        public static void Enqueue(NgSaveJob job)
        {
            if (!_q.TryAdd(job))
                job.Dispose();
        }

        private static void Loop()
        {
            foreach (var job in _q.GetConsumingEnumerable())
            {
                try
                {
                    if (job.ImgBuf != null && job.ImgBytes > 0)
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(job.JpegPath)!);
                        SaveJpeg(job.ImgBuf, job.W, job.H, job.Stride, job.CvType,
                                 job.JpegPath, job.JpegQuality);
                    }

                    Directory.CreateDirectory(Path.GetDirectoryName(job.CsvPath)!);
                    var line = string.Join(",",
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff"),
                        $"ch={job.ChannelIndex}",
                        $"frame={job.FrameIndex}",
                        $"ptsMs={job.CenterPtsMs}",
                        $"NG={job.Ng}",
                        $"OK={job.Ok}",
                        $"NONE={job.None}",
                        $"clipReq={job.ClipRequestResult}",
                        $"jpg={job.JpegPath}",
                        $"mp4={job.ClipPath}");

                    File.AppendAllText(job.CsvPath, line + Environment.NewLine, Encoding.UTF8);
                }
                catch { }
                finally { job.Dispose(); }
            }
        }

        private static void SaveJpeg(byte[] buf, int w, int h, int stride,
            int cvType, string path, int quality)
        {
            PixelFormat pf;
            int bpp;

            if (cvType == CV_8UC3) { pf = PixelFormats.Bgr24; bpp = 3; }
            else if (cvType == CV_8UC4) { pf = PixelFormats.Bgra32; bpp = 4; }
            else return;

            int minStride = w * bpp;
            if (stride < minStride) stride = minStride;

            var bs = BitmapSource.Create(w, h, 96, 96, pf, null, buf, stride);
            bs.Freeze();

            var enc = new JpegBitmapEncoder { QualityLevel = Math.Clamp(quality, 1, 100) };
            enc.Frames.Add(BitmapFrame.Create(bs));

            using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
            enc.Save(fs);
        }
    }
}
