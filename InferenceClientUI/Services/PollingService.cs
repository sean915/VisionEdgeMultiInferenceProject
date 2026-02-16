using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Models;
using InferenceClientUI.ViewModels;
using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace InferenceClientUI.Services
{
    public sealed class PollingService : IPollingService
    {
        private readonly IControllerService _controller;
        private readonly ConcurrentDictionary<Guid, StreamSession> _sessions = new();

        public PollingService(IControllerService controller)
        {
            _controller = controller;
        }

        public void StartStream(Guid clientId, string source, int fps = 10)
        {
            if (clientId == Guid.Empty) return;
            if (string.IsNullOrWhiteSpace(source)) return;

            StopStream(clientId);

            if (!DashboardChannelHub.Instance.TryGetChannel(clientId, out var ch))
                return;

            ch.RtspUrl = source;

            var session = new StreamSession(clientId, source, fps, ch);
            _sessions[clientId] = session;
            session.Start();
        }

        public void StopStream(Guid clientId)
        {
            if (clientId == Guid.Empty) return;

            if (_sessions.TryRemove(clientId, out var s))
                s.Cancel();

            if (DashboardChannelHub.Instance.TryGetChannel(clientId, out var ch))
            {
                Application.Current.Dispatcher.Invoke(() =>
                {
                    ch.StatusText = "Stopped";
                    ch.HasFrame = false;
                });
            }
        }


        // =====================================================================
        //  StreamSession: RTSP 수신 + 추론 DLL 연동 + 결과 콜백 처리
        // =====================================================================
        private sealed class StreamSession
        {
            private const int CV_8UC3 = 16;
            private const int CV_8UC4 = 24;
            private const int PTS_MAP_MAX = 8192;

            // --- 세션 레지스트리 (static: 채널 인덱스 → 세션) ---
            private static readonly ConcurrentDictionary<int, StreamSession> s_sessionsByChIdx = new();
            private static readonly InferenceNative.ResultCallbackFunc s_algoCb = OnAlgoResultStatic;

            // --- 인스턴스 필드 ---
            private readonly string _source;
            private readonly int _fps;
            private readonly ChannelVm _channel;
            private readonly CancellationTokenSource _cts = new();
            private readonly int _channelId;

            // PTS 추적 (frameIndex → ptsMs)
            private readonly ConcurrentDictionary<ulong, long> _ptsMap = new();
            private readonly ConcurrentQueue<ulong> _ptsOrder = new();

            // GC 방지: 네이티브에 넘기는 delegate는 반드시 필드로 유지
            private RtspNativeDefinitions.FrameCallback? _frameCb;
            private RtspNativeDefinitions.FrameCallbackEx? _frameCbEx;
            private RtspNativeDefinitions.StatusCallback? _statusCb;

            private DynamicInferenceSession? _algo;
            private IntPtr _handle = IntPtr.Zero;
            private volatile bool _stopped;
            private long _frameIndex;
            private long _lastNgSavedPtsMs = -1;

            public IntPtr Handle => _handle;

            public StreamSession(Guid clientId, string source, int fps, ChannelVm channel)
            {
                _source = source;
                _fps = Math.Clamp(fps, 1, 60);
                _channel = channel;
                _channelId = clientId.GetHashCode();
            }

            // =================================================================
            //  Start / Cancel / SafeDestroy
            // =================================================================

            public void Start()
            {
                _stopped = false;
                UpdateUi(() =>
                {
                    _channel.StatusText = "Starting...";
                    _channel.HasFrame = false;
                });

                InitAlgorithm();
                InitRtspReceiver();
            }

            public void Cancel()
            {
                _stopped = true;
                _cts.Cancel();
                SafeDestroy();
                UpdateUi(() =>
                {
                    _channel.StatusText = "Stopped";
                    _channel.HasFrame = false;
                });
            }

            private void SafeDestroy()
            {
                UnregisterSession();

                var h = Interlocked.Exchange(ref _handle, IntPtr.Zero);
                if (h != IntPtr.Zero)
                {
                    try { RtspNativeDefinitions.rtsp_stop(h); } catch { }
                    try { RtspNativeDefinitions.rtsp_destroy(h); } catch { }
                }

                try { _algo?.Stop(); } catch { }
                try { _algo?.Dispose(); } catch { }
                _algo = null;
            }

            // =================================================================
            //  초기화 헬퍼
            // =================================================================

            private void InitAlgorithm()
            {
                var baseDir = _channel.BaseDirPath ?? "";
                if (string.IsNullOrWhiteSpace(baseDir)) return;

                try
                {
                    _algo = new DynamicInferenceSession(
                        _channel.ModelType, baseDir,
                        confThreshold: 0.5f, useCuda: true, s_algoCb);
                    _algo.Run();
                }
                catch (Exception ex)
                {
                    UpdateUi(() => _channel.StatusText = $"Algo init fail: {ex.Message}");
                    _algo?.Dispose();
                    _algo = null;
                }
            }

            private void InitRtspReceiver()
            {
                _statusCb = OnStatus;
                _frameCbEx = OnFrameEx;

                try
                {
                    _handle = TryCreateBgr24() ?? FallbackCreateBgra();

                    if (_handle == IntPtr.Zero)
                    {
                        UpdateUi(() =>
                        {
                            _channel.StatusText = "Create failed";
                            _channel.HasFrame = false;
                        });
                        return;
                    }

                    int r = RtspNativeDefinitions.rtsp_start(_handle);
                    if (r != 0)
                    {
                        UpdateUi(() =>
                        {
                            _channel.StatusText = $"Start failed: {r}";
                            _channel.HasFrame = false;
                        });
                        SafeDestroy();
                        return;
                    }

                    RegisterSession();
                    UpdateUi(() => _channel.StatusText = $"Running ({_fps} fps)");
                }
                catch (Exception ex)
                {
                    UpdateUi(() =>
                    {
                        _channel.StatusText = $"Exception: {ex.Message}";
                        _channel.HasFrame = false;
                    });
                    SafeDestroy();
                }
            }

            private IntPtr? TryCreateBgr24()
            {
                try
                {
                    var opt = default(RtspNativeDefinitions.RtspOptions2);
                    RtspNativeDefinitions.rtsp_default_options2(ref opt);
                    opt.struct_size = (uint)Marshal.SizeOf<RtspNativeDefinitions.RtspOptions2>();
                    opt.tcp_only = 1;
                    opt.stimeout_us = 3_000_000;
                    opt.reconnect = 1;
                    opt.reconnect_wait_ms = 1000;
                    opt.target_fps = 0;
                    opt.out_pixfmt = RtspNativeDefinitions.RtspPixelFormat.BGR24;

                    return RtspNativeDefinitions.rtsp_create2(
                        _channelId, _source, ref opt, _frameCbEx!, _statusCb!, IntPtr.Zero);
                }
                catch (EntryPointNotFoundException)
                {
                    UpdateUi(() => _channel.StatusText = "Receiver DLL has no create2(BGR24). Inference disabled.");
                    return null;
                }
            }

            private IntPtr FallbackCreateBgra()
            {
                var opt = default(RtspNativeDefinitions.RtspOptions);
                RtspNativeDefinitions.rtsp_default_options(ref opt);
                opt.tcp_only = 1;
                opt.stimeout_us = 3_000_000;
                opt.reconnect = 1;
                opt.reconnect_wait_ms = 1000;
                opt.target_fps = 0;

                _frameCb = OnFrame;
                return RtspNativeDefinitions.rtsp_create(
                    _channelId, _source, ref opt, _frameCb, _statusCb!, IntPtr.Zero);
            }

            // =================================================================
            //  세션 레지스트리
            // =================================================================

            private void RegisterSession() => s_sessionsByChIdx[_channel.ChannelIndex] = this;
            private void UnregisterSession() => s_sessionsByChIdx.TryRemove(_channel.ChannelIndex, out _);
            private static bool TryGetSession(int chIdx, out StreamSession session)
                => s_sessionsByChIdx.TryGetValue(chIdx, out session);

            // =================================================================
            //  PTS 추적
            // =================================================================

            private void TrackPts(ulong frameIndex, long ptsMs)
            {
                _ptsMap[frameIndex] = ptsMs;
                _ptsOrder.Enqueue(frameIndex);

                while (_ptsOrder.Count > PTS_MAP_MAX && _ptsOrder.TryDequeue(out var oldKey))
                    _ptsMap.TryRemove(oldKey, out _);
            }

            public bool TryGetCenterPts(ulong frameIndex, out long centerPtsMs)
                => _ptsMap.TryGetValue(frameIndex, out centerPtsMs);

            // =================================================================
            //  NG 쿨다운
            // =================================================================

            public bool CanSaveNgOnce(long centerPtsMs)
            {
                if (centerPtsMs <= 0) return false;

                int cd = _channel?.SaveCooldownMs ?? 3000;
                long last = Interlocked.Read(ref _lastNgSavedPtsMs);

                // 첫 1회 또는 PTS 역행(리커넥트) → 무조건 통과
                if (last < 0 || centerPtsMs < last)
                {
                    Interlocked.Exchange(ref _lastNgSavedPtsMs, centerPtsMs);
                    return true;
                }

                if (centerPtsMs - last < cd) return false;

                Interlocked.Exchange(ref _lastNgSavedPtsMs, centerPtsMs);
                return true;
            }

            // =================================================================
            //  프레임 인덱스 생성 (상위 8비트: 채널, 하위 56비트: 시퀀스)
            // =================================================================

            private ulong NextFrameIndexWithChannel()
            {
                ulong ch = ((ulong)(byte)_channel.ChannelIndex) << 56;
                ulong seq = (ulong)Interlocked.Increment(ref _frameIndex) & 0x00FFFFFFFFFFFFFFUL;
                return ch | seq;
            }

            // =================================================================
            //  RTSP 콜백: 상태
            // =================================================================

            private void OnStatus(IntPtr user, int channelId,
                RtspNativeDefinitions.RtspLogLevel level, int code, string message)
            {
                if (_stopped || level == RtspNativeDefinitions.RtspLogLevel.Info) return;

                Application.Current?.Dispatcher.BeginInvoke(() =>
                {
                    if (_stopped) return;
                    _channel.StatusText = $"RTSP: {level} {code} - {message}";
                    if (level == RtspNativeDefinitions.RtspLogLevel.Error)
                        _channel.HasFrame = false;
                });
            }

            // =================================================================
            //  RTSP 콜백: BGRA 프리뷰 전용 (fallback)
            // =================================================================

            private void OnFrame(IntPtr user, int channelId,
                IntPtr bgra, int width, int height, int stride, long ptsMs)
            {
                if (_stopped || bgra == IntPtr.Zero || width <= 0 || height <= 0 || stride <= 0)
                    return;

                int bytes = checked(height * stride);
                byte[]? buffer = ArrayPool<byte>.Shared.Rent(bytes);
                try
                {
                    Marshal.Copy(bgra, buffer, 0, bytes);
                }
                catch
                {
                    ArrayPool<byte>.Shared.Return(buffer);
                    return;
                }

                if (Application.Current == null)
                {
                    ArrayPool<byte>.Shared.Return(buffer);
                    return;
                }

                Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    if (_stopped) { ArrayPool<byte>.Shared.Return(buffer); return; }
                    try
                    {
                        _channel.EnsureBitmap(width, height);
                        _channel.UpdateFrameBgra(buffer, width, height, stride);
                        _channel.HasFrame = true;
                        _channel.StatusText = $"{_fps} fps";
                    }
                    finally { ArrayPool<byte>.Shared.Return(buffer); }
                });
            }
            // StreamSession 클래스 내부에 아래 메서드를 추가하세요.

            // 기존 PushFrameToAlgorithm 메서드를 아래와 같이 수정하세요.
            private unsafe void PushFrameToAlgorithm(ulong frameIndex, IntPtr data, int width, int height, int stride)
            {
                if (_algo == null) return;
                // DynamicInferenceSession에는 PushFrame이 없으므로, PushBgr를 사용해야 합니다.
                // epochMs(ptsMs)는 현재 사용할 수 없으므로 0을 전달하거나 필요시 ptsMs를 인자로 추가하세요.
                _algo.PushBgr(frameIndex, data, width, height, 0);
            }

            // =================================================================
            //  RTSP 콜백: BGR24 프레임 → 추론 Push
            // =================================================================

            private unsafe void OnFrameEx(IntPtr user, int channelId,
                IntPtr data, int width, int height, int stride,
                long ptsMs, RtspNativeDefinitions.RtspPixelFormat pixfmt)
            {
                if (_stopped || data == IntPtr.Zero || width <= 0 || height <= 0 || stride <= 0) return;
                if (pixfmt != RtspNativeDefinitions.RtspPixelFormat.BGR24) return;

                ulong frameIndex = NextFrameIndexWithChannel();
                TrackPts(frameIndex, ptsMs);

                // 추론 알고리즘이 있으면 Push
                PushFrameToAlgorithm(frameIndex, data, width, height, stride);

                // 추론이 없으면 BGR24 프리뷰를 직접 표시
                if (_algo == null)
                {
                    int bytes = checked(height * stride);
                    byte[]? buffer = ArrayPool<byte>.Shared.Rent(bytes);
                    try
                    {
                        Marshal.Copy(data, buffer, 0, bytes);
                    }
                    catch
                    {
                        ArrayPool<byte>.Shared.Return(buffer);
                        return;
                    }

                    Application.Current?.Dispatcher.BeginInvoke(() =>
                    {
                        if (_stopped) { ArrayPool<byte>.Shared.Return(buffer); return; }
                        try
                        {
                            _channel.EnsureBitmap(width, height);
                            _channel.UpdateFrameBgr(buffer, width, height, stride);
                            _channel.HasFrame = true;
                            _channel.StatusText = $"Preview ({width}x{height})";
                        }
                        finally { ArrayPool<byte>.Shared.Return(buffer); }
                    });
                }
            }

            // =================================================================
            //  추론 결과 콜백 (static → UI 스레드 디스패치)
            // =================================================================

            private static void OnAlgoResultStatic(
                ulong frameIndex, IntPtr results, int errorCode, IntPtr errorMsg,
                IntPtr imgData, int imgW, int imgH, int imgStrideBytes, int imgCvType)
            {
                int chIdx = (int)(frameIndex >> 56);
                if (chIdx < 0 || chIdx >= DashboardChannelHub.MaxChannels) return;

                var ch = DashboardChannelHub.Instance.Channels[chIdx];
                string msg = errorMsg != IntPtr.Zero ? (Marshal.PtrToStringAnsi(errorMsg) ?? "") : "";

                // 결과 파싱 (네이티브 스레드에서 수행)
                var counts = ParseResultCounts(results);

                // 프리뷰용 이미지 복사 (네이티브 메모리 → managed)
                byte[]? buffer = null;
                int bytes = 0;
                bool canPreview = counts.HasResult && IsValidImage(imgData, imgW, imgH, imgStrideBytes, imgCvType);

                if (canPreview)
                {
                    bytes = checked(imgH * imgStrideBytes);
                    buffer = ArrayPool<byte>.Shared.Rent(bytes);
                    try { Marshal.Copy(imgData, buffer, 0, bytes); }
                    catch { ArrayPool<byte>.Shared.Return(buffer); buffer = null; canPreview = false; }
                }

                if (Application.Current == null)
                {
                    if (buffer != null) ArrayPool<byte>.Shared.Return(buffer);
                    return;
                }

                // UI 스레드
                Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    if (errorCode != 0)
                    {
                        ch.StatusText = $"AlgoError {errorCode}: {msg}";
                        if (buffer != null) ArrayPool<byte>.Shared.Return(buffer);
                        return;
                    }

                    if (counts.HasResult) ch.Trigger += 1;

                    if (counts.Ng > 0)
                        HandleDefect(ch, chIdx, frameIndex, counts, buffer, bytes, imgW, imgH, imgStrideBytes, imgCvType);

                    UpdatePreview(ch, counts, buffer, bytes, imgW, imgH, imgStrideBytes, imgCvType, canPreview);
                });
            }

            // --- 결과 파싱 ---

            private readonly record struct DefectCounts(int Ng, int Ok, int None, bool HasResult);

            private static DefectCounts ParseResultCounts(IntPtr results)
            {
                if (results == IntPtr.Zero) return default;

                unsafe
                {
                    var item = *(InferenceNative.ResultItemC*)results;
                    return (DefectTypeEnum)item.defect_type switch
                    {
                        DefectTypeEnum.NG   => new(1, 0, 0, true),
                        DefectTypeEnum.OK   => new(0, 1, 0, true),
                        _                   => new(0, 0, 1, true),
                    };
                }
            }

            private static bool IsValidImage(IntPtr data, int w, int h, int stride, int cvType)
                => data != IntPtr.Zero && w > 0 && h > 0 && stride > 0
                   && (cvType == CV_8UC3 || cvType == CV_8UC4);

            // --- NG 처리: 카운터 + Inspection Result + 파일 저장 ---

            private static void HandleDefect(ChannelVm ch, int chIdx, ulong frameIndex,
                DefectCounts counts, byte[]? buffer, int bytes, int imgW, int imgH, int stride, int cvType)
            {
                ch.Abnormal += counts.Ng;
                ch.AddInspectionResult(counts.Ng, counts.Ok, frameIndex);

                if (!TryGetSession(chIdx, out var session)) return;
                if (!session.TryGetCenterPts(frameIndex, out long centerPtsMs)) return;
                if (!session.CanSaveNgOnce(centerPtsMs)) return;

                EnqueueNgSave(ch, chIdx, frameIndex, centerPtsMs, counts,
                              buffer, bytes, imgW, imgH, stride, cvType);
            }

            private static void EnqueueNgSave(ChannelVm ch, int chIdx, ulong frameIndex,
                long centerPtsMs, DefectCounts counts,
                byte[]? buffer, int bytes, int imgW, int imgH, int stride, int cvType)
            {
                string day = DateTime.Now.ToString("yyyyMMdd");
                string ts = DateTime.Now.ToString("HHmmss_fff");

                string baseDir = ch.SaveDir;
                if (string.IsNullOrWhiteSpace(baseDir))
                    baseDir = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                        "InferenceClips");

                string chDir = Path.Combine(baseDir, day, $"ch{chIdx + 1}");
                string jpgPath = Path.Combine(chDir, $"{day}_{ts}_NG{counts.Ng}_pts{centerPtsMs}.jpg");
                string mp4Path = Path.Combine(chDir, $"{day}_{ts}_NG{counts.Ng}_pts{centerPtsMs}.mp4");
                string csvPath = Path.Combine(chDir, $"{day}_NG_log.csv");

                // 이미지 버퍼 복사 (buffer는 프리뷰에서도 쓰이므로 별도 복사)
                byte[]? imgBuf = null;
                int imgBytes = 0;
                if (buffer != null && bytes > 0)
                {
                    imgBytes = bytes;
                    imgBuf = ArrayPool<byte>.Shared.Rent(imgBytes);
                    Array.Copy(buffer, imgBuf, imgBytes);
                }

                NgSaveWorker.Enqueue(new NgSaveJob
                {
                    ChannelIndex = chIdx + 1,
                    FrameIndex = frameIndex,
                    CenterPtsMs = centerPtsMs,
                    Ng = counts.Ng,
                    Ok = counts.Ok,
                    None = counts.None,
                    JpegPath = jpgPath,
                    ClipPath = mp4Path,
                    CsvPath = csvPath,
                    ImgBuf = imgBuf,
                    ImgBytes = imgBytes,
                    W = imgW,
                    H = imgH,
                    Stride = stride,
                    CvType = cvType,
                    JpegQuality = ch.JpegQuality,
                });
            }

            // --- 프리뷰 갱신 ---

            private static void UpdatePreview(ChannelVm ch, DefectCounts counts,
                byte[]? buffer, int bytes, int imgW, int imgH, int stride, int cvType, bool canPreview)
            {
                if (canPreview && buffer != null)
                {
                    try
                    {
                        if (cvType == CV_8UC3)
                            ch.UpdateFrameBgr(buffer, imgW, imgH, stride);
                        else
                            ch.UpdateFrameBgra(buffer, imgW, imgH, stride);

                        ch.HasFrame = true;
                        ch.StatusText = $"Result: NG={counts.Ng} OK={counts.Ok} (T={ch.Trigger}, A={ch.Abnormal})";
                    }
                    finally { ArrayPool<byte>.Shared.Return(buffer); }
                }
                else
                {
                    if (buffer != null) ArrayPool<byte>.Shared.Return(buffer);
                    ch.StatusText = $"Result (no image) NG={counts.Ng} OK={counts.Ok}";
                }
            }

            // =================================================================
            //  유틸리티
            // =================================================================

            private static void UpdateUi(Action action)
                => Application.Current?.Dispatcher.Invoke(action);
        }
    }
}
