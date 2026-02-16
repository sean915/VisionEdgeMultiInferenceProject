using InferenceClientUI.InfraStructure;
using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Models;
using System;
using System.Buffers;
using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Windows.Storage.Streams;

namespace InferenceClientUI.ViewModels
{
    public sealed class ChannelVm : ObservableObject
    {
        public int ChannelIndex { get; } // 0-based

        /// <summary>
        /// SQLite client_setting 테이블의 PK. 0이면 아직 DB에 저장 안 된 상태.
        /// </summary>
        public long DbId { get; set; }

        private bool _isAssigned;
        public bool IsAssigned
        {
            get => _isAssigned;
            internal set => SetProperty(ref _isAssigned, value);
        }

        private Guid _clientId = Guid.Empty;
        public Guid ClientId
        {
            get => _clientId;
            internal set => SetProperty(ref _clientId, value);
        }

        private string _clientName = "";
        public string ClientName
        {
            get => _clientName;
            internal set
            {
                if (SetProperty(ref _clientName, value))
                    OnPropertyChanged(nameof(Title));
            }
        }

        private string _clientIp = "";
        public string ClientIp
        {
            get => _clientIp;
            internal set => SetProperty(ref _clientIp, value);
        }

        // ✅ Settings에서 넣는 스트림 소스(RTSP/파일 경로 모두)
        private string? _rtspUrl;
        public string? RtspUrl
        {
            get => _rtspUrl;
            set => SetProperty(ref _rtspUrl, value);
        }

        public string Title => !string.IsNullOrWhiteSpace(ClientName)
            ? ClientName
            : $"CH-{ChannelIndex + 1}";

        private string _statusText = "RTSP: (empty)";
        public string StatusText
        {
            get => _statusText;
            set => SetProperty(ref _statusText, value);
        }

        private int _trigger;
        public int Trigger
        {
            get => _trigger;
            set => SetProperty(ref _trigger, value);
        }

        private int _abnormal;
        public int Abnormal
        {
            get => _abnormal;
            set => SetProperty(ref _abnormal, value);
        }

        private bool _hasFrame;
        public bool HasFrame
        {
            get => _hasFrame;
            set => SetProperty(ref _hasFrame, value);
        }

        private string _baseDirPath = "";
        public string BaseDirPath
        {
            get => _baseDirPath;
            set => SetProperty(ref _baseDirPath, value);
        }

        private ModelType _modelType = ModelType.CutterMagazine;
        public ModelType ModelType
        {
            get => _modelType;
            set => SetProperty(ref _modelType, value);
        }

        // =========================
        // ✅ 1회성 "클립 저장 요청" 플래그
        // =========================
        private bool _saveRequested;
        /// <summary>
        /// UI 버튼/AI 트리거에서 true로 올리면, receiver(OnFrameEx)에서 한 번 처리 후 false로 내리는 1회성 플래그
        /// </summary>
        public bool SaveRequested
        {
            get => _saveRequested;
            set => SetProperty(ref _saveRequested, value);
        }

        /// <summary>
        /// 외부(버튼/AI 이벤트)에서 호출: 1회성 저장 요청 올리기
        /// </summary>
        public void RequestSaveClip()
        {
            SaveRequested = true;
        }

        /// <summary>
        /// receiver가 처리 후 호출: 중복 저장 방지
        /// </summary>
        public void ClearSaveRequest()
        {
            SaveRequested = false;
        }

        private WriteableBitmap? _frame;
        public ImageSource? Frame
        {
            get => _frame;
            private set
            {
                // ImageSource로 바인딩하지만 실제는 WriteableBitmap 유지
                _frame = value as WriteableBitmap;
                OnPropertyChanged();
            }
        }

        public ChannelVm(int channelIndex)
        {
            ChannelIndex = channelIndex;
        }

        public void EnsureBitmap(int width, int height, PixelFormat fmt)
        {
            if (width <= 0 || height <= 0) return;

            if (_frame != null && _frame.PixelWidth == width && _frame.PixelHeight == height && _frame.Format == fmt)
                return;

            if (!Application.Current.Dispatcher.CheckAccess())
            {
                Application.Current.Dispatcher.Invoke(() => EnsureBitmap(width, height, fmt));
                return;
            }

            _frame = new WriteableBitmap(width, height, 96, 96, fmt, null);
            OnPropertyChanged(nameof(Frame));
        }

        public void UpdateFrameBgr(byte[] bgr, int width, int height, int stride)
        {
            if (bgr == null || bgr.Length == 0) return;

            EnsureBitmap(width, height, PixelFormats.Bgr24);
            if (_frame == null) return;

            if (stride <= 0) stride = width * 3;

            var rect = new Int32Rect(0, 0, width, height);
            _frame.Lock();
            try { _frame.WritePixels(rect, bgr, stride, 0); }
            finally { _frame.Unlock(); }
        }

        public void EnsureBitmap(int width, int height)
        {
            if (width <= 0 || height <= 0) return;

            if (_frame != null && _frame.PixelWidth == width && _frame.PixelHeight == height)
                return;

            // ✅ 반드시 UI 스레드에서 생성/교체
            if (!Application.Current.Dispatcher.CheckAccess())
            {
                Application.Current.Dispatcher.Invoke(() => EnsureBitmap(width, height));
                return;
            }

            _frame = new WriteableBitmap(width, height, 96, 96, PixelFormats.Bgra32, null);
            OnPropertyChanged(nameof(Frame));
        }

        // ✅ PollingService에서 UI스레드로 InvokeAsync한 상태에서 호출해야 함
        public void UpdateFrameBgra(byte[] bgra, int width, int height, int stride)
        {
            if (bgra == null || bgra.Length == 0) return;

            EnsureBitmap(width, height);
            if (_frame == null) return;

            // 방어: stride가 이상하면 계산해서 보정
            if (stride <= 0) stride = width * 4;

            var rect = new Int32Rect(0, 0, width, height);

            _frame.Lock();
            try
            {
                _frame.WritePixels(rect, bgra, stride, 0);
            }
            finally
            {
                _frame.Unlock();
            }
        }

        internal void ClearAssignment()
        {
            IsAssigned = false;
            ClientId = Guid.Empty;
            ClientName = "";
            ClientIp = "";
            RtspUrl = null;

            StatusText = "RTSP: (empty)";
            HasFrame = false;

            // ✅ 채널 초기화 시 저장 요청도 초기화
            SaveRequested = false;
            ModelType = ModelType.CutterMagazine;
        }

        private WriteableBitmap? _wb;

        public void UpdateFrame(int width, int height, int stride, byte[] bgra)
        {
            if (width <= 0 || height <= 0 || stride <= 0 || bgra == null || bgra.Length == 0)
                return;

            if (_wb == null || _wb.PixelWidth != width || _wb.PixelHeight != height)
            {
                _wb = new WriteableBitmap(width, height, 96, 96, PixelFormats.Bgra32, null);
                Frame = _wb;
                HasFrame = true;
            }

            _wb.WritePixels(new Int32Rect(0, 0, width, height), bgra, stride, 0);
        }

        // =========================
        // ✅ 클립 저장 설정(경로/앞뒤 ms)
        // =========================

        private string _saveDir = "C:\\Users\\rtm\\RTM";
        /// <summary>
        /// 저장 폴더. 비어있으면 receiver에서 Desktop\InferenceClips 기본값으로 채움.
        /// </summary>
        public string SaveDir
        {
            get => _saveDir;
            set => SetProperty(ref _saveDir, value);
        }

        private int _savePreMs = 5000;
        /// <summary>
        /// 이벤트(센터) 기준 앞쪽 저장 길이(ms). 기본 5000.
        /// </summary>
        public int SavePreMs
        {
            get => _savePreMs;
            set => SetProperty(ref _savePreMs, value);
        }

        private int _savePostMs = 5000;
        /// <summary>
        /// 이벤트(센터) 기준 뒤쪽 저장 길이(ms). 기본 5000.
        /// </summary>
        public int SavePostMs
        {
            get => _savePostMs;
            set => SetProperty(ref _savePostMs, value);
        }

        // ✅ 연속 NG 폭주 방지(예: 3초 쿨다운)
        private int _saveCooldownMs = 3000;
        public int SaveCooldownMs
        {
            get => _saveCooldownMs;
            set => SetProperty(ref _saveCooldownMs, value);
        }

        // ✅ “용량/속도 작은 쪽” = JPG 권장 (품질 75~85 보통)
        private int _jpegQuality = 80;
        public int JpegQuality
        {
            get => _jpegQuality;
            set => SetProperty(ref _jpegQuality, value);
        }

        private const int MaxInspectionResults = 100;

        /// <summary>
        /// Abnormal 발생 시 누적되는 검출 이력. UI의 Inspection Result 패널에 바인딩.
        /// </summary>
        public ObservableCollection<InspectionResultItem> InspectionResults { get; } = new();

        /// <summary>
        /// Abnormal 결과를 Inspection Result 목록에 추가합니다. UI 스레드에서 호출해야 합니다.
        /// </summary>
        public void AddInspectionResult(int ngCount, int okCount, ulong frameIndex)
        {
            var item = new InspectionResultItem
            {
                Index = InspectionResults.Count + 1,
                Timestamp = DateTime.Now.ToString("HH:mm:ss.fff"),
                Result = ngCount > 0 ? "NG" : "OK",
                NgCount = ngCount,
                OkCount = okCount,
                FrameIndex = frameIndex,
            };

            InspectionResults.Insert(0, item);

            while (InspectionResults.Count > MaxInspectionResults)
                InspectionResults.RemoveAt(InspectionResults.Count - 1);
        }
    }
}
