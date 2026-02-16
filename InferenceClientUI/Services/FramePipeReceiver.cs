using System;
using System.IO;
using System.IO.Pipes;
using System.Threading;
using System.Threading.Tasks;

namespace InferenceClientUI.Services
{
    public sealed class FramePipeReceiver : IDisposable
    {
        private readonly string _pipeName;
        private CancellationTokenSource? _cts;
        private Task? _loopTask;

        // idx, w, h, stride, tsMs, bgra
        public event Action<int, int, int, int, long, byte[]>? FrameArrived;

        public FramePipeReceiver(string pipeName)
        {
            _pipeName = pipeName ?? throw new ArgumentNullException(nameof(pipeName));
        }

        public void Start()
        {
            if (_cts != null) return;
            _cts = new CancellationTokenSource();
            _loopTask = Task.Run(() => AcceptLoopAsync(_cts.Token));
        }

        public void Stop()
        {
            if (_cts == null) return;
            try { _cts.Cancel(); } catch { }
            _cts = null;
        }

        private async Task AcceptLoopAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested)
            {
                using var server = new NamedPipeServerStream(
                    _pipeName,
                    PipeDirection.In,
                    1,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous);

                try
                {
                    await server.WaitForConnectionAsync(ct).ConfigureAwait(false);
                    await ReadFramesAsync(server, ct).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                catch
                {
                    // 연결/읽기 실패 → 다시 대기
                    try { await Task.Delay(200, ct).ConfigureAwait(false); } catch { }
                }
            }
        }

        private async Task ReadFramesAsync(Stream stream, CancellationToken ct)
        {
            // uint32 magic("FRAM")
            // int32 clientIndex
            // int32 width
            // int32 height
            // int32 stride
            // int64 timestampMs
            // int32 payloadLen
            // byte[payloadLen]
            var header = new byte[32];

            while (!ct.IsCancellationRequested)
            {
                await ReadExactlyAsync(stream, header, 0, header.Length, ct).ConfigureAwait(false);

                uint magic = BitConverter.ToUInt32(header, 0);
                if (magic != 0x4652414D) // "FRAM"
                    throw new InvalidDataException("Bad magic");

                int clientIndex = BitConverter.ToInt32(header, 4);
                int width = BitConverter.ToInt32(header, 8);
                int height = BitConverter.ToInt32(header, 12);
                int stride = BitConverter.ToInt32(header, 16);
                long tsMs = BitConverter.ToInt64(header, 20);
                int payloadLen = BitConverter.ToInt32(header, 28);

                if (width <= 0 || height <= 0 || stride <= 0 || payloadLen <= 0)
                    continue;

                var payload = new byte[payloadLen];
                await ReadExactlyAsync(stream, payload, 0, payloadLen, ct).ConfigureAwait(false);

                FrameArrived?.Invoke(clientIndex, width, height, stride, tsMs, payload);
            }
        }

        private static async Task ReadExactlyAsync(Stream s, byte[] buffer, int offset, int count, CancellationToken ct)
        {
            int readTotal = 0;
            while (readTotal < count)
            {
                int n = await s.ReadAsync(buffer, offset + readTotal, count - readTotal, ct).ConfigureAwait(false);
                if (n <= 0) throw new EndOfStreamException();
                readTotal += n;
            }
        }

        public void Dispose() => Stop();
    }
}
