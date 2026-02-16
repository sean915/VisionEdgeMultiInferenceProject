using InferenceClientUI.InfraStructure;
using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Models.Entities;
using InferenceClientUI.Services;
using InferenceClientUI.Services.Database;
using System;
using System.Collections.ObjectModel;
using System.Linq;

namespace InferenceClientUI.ViewModels
{
    public sealed class SettingsViewModel : ObservableObject
    {
        private readonly IControllerService _controller;
        private readonly IPollingService _polling;
        private readonly ClientSettingRepository _clientRepo = new(AppDatabase.Instance.Connection);

        private readonly ChannelVm _targetChannel;
        private readonly Guid _clientId;

        public event Action? RequestClose;

        public event Action? RequestBrowseCameraStream;
        public event Action? RequestImportModel;

        public string ClientName { get; }
        public string ClientIp { get; }

        public ObservableCollection<ClientSettingProfile> ClientSettingOptions { get; } = new();

        private ClientSettingProfile? _selectedClientSettingOption;
        public ClientSettingProfile? SelectedClientSettingOption
        {
            get => _selectedClientSettingOption;
            set
            {
                if (SetProperty(ref _selectedClientSettingOption, value))
                {
                    UpdateSectionVisibility();
                    OnPropertyChanged(nameof(SelectedProfileDescription));
                }
            }
        }

        public string SelectedProfileDescription => SelectedClientSettingOption?.Description ?? "";

        private bool _isCameraSectionVisible;
        public bool IsCameraSectionVisible { get => _isCameraSectionVisible; set => SetProperty(ref _isCameraSectionVisible, value); }

        private bool _isPlcSectionVisible;
        public bool IsPlcSectionVisible { get => _isPlcSectionVisible; set => SetProperty(ref _isPlcSectionVisible, value); }

        private bool _isModelSectionVisible;
        public bool IsModelSectionVisible { get => _isModelSectionVisible; set => SetProperty(ref _isModelSectionVisible, value); }

        private bool _isDashboardSectionVisible;
        public bool IsDashboardSectionVisible { get => _isDashboardSectionVisible; set => SetProperty(ref _isDashboardSectionVisible, value); }

        private string _cameraStream = "";
        public string CameraStream { get => _cameraStream; set => SetProperty(ref _cameraStream, value); }

        private string _cameraReference = "";
        public string CameraReference { get => _cameraReference; set => SetProperty(ref _cameraReference, value); }

        private bool _isCameraAdvancedEnabled;
        public bool IsCameraAdvancedEnabled { get => _isCameraAdvancedEnabled; set => SetProperty(ref _isCameraAdvancedEnabled, value); }

        private string _cameraAdvanced = "";
        public string CameraAdvanced { get => _cameraAdvanced; set => SetProperty(ref _cameraAdvanced, value); }

        private string _plcType = "SIEMENS";
        public string PlcType { get => _plcType; set => SetProperty(ref _plcType, value); }

        private string _plcIp = "";
        public string PlcIp { get => _plcIp; set => SetProperty(ref _plcIp, value); }

        private string _plcAddress = "";
        public string PlcAddress { get => _plcAddress; set => SetProperty(ref _plcAddress, value); }

        private string _modelPath = "";
        public string ModelPath { get => _modelPath; set => SetProperty(ref _modelPath, value); }

        /// <summary>
        /// 현재 선택된 프로필에서 결정되는 ModelType. UI에 읽기전용으로 노출 가능.
        /// </summary>
        public ModelType ResolvedModelType =>
            SelectedClientSettingOption?.DefaultModelType ?? ModelType.CutterMagazine;

        private string _dashboardLocal = "";
        public string DashboardLocal { get => _dashboardLocal; set => SetProperty(ref _dashboardLocal, value); }

        private bool _isDashboardRemoteEnabled;
        public bool IsDashboardRemoteEnabled { get => _isDashboardRemoteEnabled; set => SetProperty(ref _isDashboardRemoteEnabled, value); }

        private string _dashboardRemote = "";
        public string DashboardRemote { get => _dashboardRemote; set => SetProperty(ref _dashboardRemote, value); }

        public RelayCommand BrowseCameraStreamCommand { get; }
        public RelayCommand ImportModelCommand { get; }

        public RelayCommand ConfirmCommand { get; }
        public RelayCommand CancelCommand { get; }

        public SettingsViewModel(
            IControllerService controller,
            IPollingService polling,
            ChannelVm targetChannel,
            Guid clientId,
            string clientName,
            string clientIp)
        {
            _controller = controller;
            _polling = polling;

            _targetChannel = targetChannel;
            _clientId = clientId;

            ClientName = clientName;
            ClientIp = clientIp;

            // ✅ 창 열 때 채널에 저장돼있던 값 로드
            CameraStream = _targetChannel.RtspUrl ?? "";
            ModelPath = _targetChannel.BaseDirPath ?? "";

            ClientSettingOptions.Add(new ClientSettingProfile(
                "Stack Magazine",
                "Stack Magazine 검출 — 카메라 + 모델(HmStkDLL) + PLC + 대시보드",
                useCamera: true, usePlc: true, useModel: true, useDashboard: true,
                defaultModelType: ModelType.StackMagazine));

            ClientSettingOptions.Add(new ClientSettingProfile(
                "Cutter Magazine",
                "Cutter Magazine 검출 — 카메라 + 모델(HmCutterDLL) + PLC + 대시보드",
                useCamera: true, usePlc: true, useModel: true, useDashboard: true,
                defaultModelType: ModelType.CutterMagazine));

            ClientSettingOptions.Add(new ClientSettingProfile(
                "Camera Only",
                "카메라 스트림/파일만 설정 (추론 없음)",
                useCamera: true, usePlc: false, useModel: false, useDashboard: false));

            // ✅ 채널에 저장된 ModelType에 맞는 프로필 자동 선택
            SelectedClientSettingOption =
                ClientSettingOptions.FirstOrDefault(p => p.DefaultModelType == _targetChannel.ModelType)
                ?? ClientSettingOptions.FirstOrDefault();

            BrowseCameraStreamCommand = new RelayCommand(() => RequestBrowseCameraStream?.Invoke());
            ImportModelCommand = new RelayCommand(() => RequestImportModel?.Invoke());

            ConfirmCommand = new RelayCommand(OnConfirm);
            CancelCommand = new RelayCommand(() => RequestClose?.Invoke());
        }

        private void UpdateSectionVisibility()
        {
            var p = SelectedClientSettingOption;
            IsCameraSectionVisible = p?.UseCamera == true;
            IsPlcSectionVisible = p?.UsePlc == true;
            IsModelSectionVisible = p?.UseModel == true;
            IsDashboardSectionVisible = p?.UseDashboard == true;

            IsCameraAdvancedEnabled = false;
            IsDashboardRemoteEnabled = false;

            OnPropertyChanged(nameof(ResolvedModelType));
        }

        private void OnConfirm()
        {
            // ✅ 모델 경로도 채널에 저장 (PollingService가 이걸 읽어서 추론 세션 생성)
            _targetChannel.BaseDirPath = ModelPath ?? "";

            // ✅ 모델 타입은 프로필에서 결정
            _targetChannel.ModelType = ResolvedModelType;

            // 카운터 초기화(원하면 유지해도 됨)
            _targetChannel.Trigger = 0;
            _targetChannel.Abnormal = 0;

            // ✅ "저장" (런타임 저장: 채널에 유지)
            _targetChannel.RtspUrl = CameraStream;

            // ✅ DB 저장 (Insert or Update)
            SaveToDb();

            // ✅ 스트림 시작/중지
            if (!string.IsNullOrWhiteSpace(CameraStream))
            {
                _targetChannel.StatusText = CameraStream.Contains("rtsp", StringComparison.OrdinalIgnoreCase)
                    ? "RTSP: Starting..."
                    : "FILE: Starting...";

                _polling.StartStream(_clientId, CameraStream, 30);
            }
            else
            {
                _polling.StopStream(_clientId);
                _targetChannel.StatusText = "RTSP: (empty)";
                _targetChannel.HasFrame = false;
            }

            RequestClose?.Invoke();
        }

        private void SaveToDb()
        {
            var entity = new ClientSettingEntity
            {
                Id = _targetChannel.DbId,
                Name = ClientName ?? "",
                CamPath = CameraStream ?? "",
                Ip = ClientIp ?? "",
                ModelBaseDirPath = ModelPath ?? "",
                ModelType = (int)ResolvedModelType,
                SaveDir = _targetChannel.SaveDir ?? "",
                SavePreMs = _targetChannel.SavePreMs,
                SavePostMs = _targetChannel.SavePostMs,
                SaveCooldownMs = _targetChannel.SaveCooldownMs,
                JpegQuality = _targetChannel.JpegQuality,
            };

            if (entity.Id > 0)
            {
                _clientRepo.Update(entity);
            }
            else
            {
                long newId = _clientRepo.Insert(entity);
                _targetChannel.DbId = newId;
            }
        }

        public sealed class ClientSettingProfile
        {
            public string Name { get; }
            public string DisplayName { get; }
            public string Description { get; }

            public bool UseCamera { get; }
            public bool UsePlc { get; }
            public bool UseModel { get; }
            public bool UseDashboard { get; }

            public ModelType? DefaultModelType { get; }

            public ClientSettingProfile(
                string name,
                string description,
                bool useCamera,
                bool usePlc,
                bool useModel,
                bool useDashboard,
                ModelType? defaultModelType = null)
            {
                Name = name;
                Description = description;
                UseCamera = useCamera;
                UsePlc = usePlc;
                UseModel = useModel;
                UseDashboard = useDashboard;
                DefaultModelType = defaultModelType;

                if (defaultModelType.HasValue)
                    DisplayName = $"{name}  [{defaultModelType.Value.ToDllName()}]";
                else
                    DisplayName = name;
            }

            public override string ToString() => DisplayName;
        }
    }
}
