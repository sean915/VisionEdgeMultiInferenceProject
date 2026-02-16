using InferenceClientUI.InfraStructure;
using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Services;
using InferenceClientUI.Services.Database;
using InferenceClientUI.Views;
using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;

namespace InferenceClientUI.ViewModels
{
    public sealed class MainSettingViewModel : ObservableObject
    {
        private readonly IControllerService _controller;
        private readonly IPollingService _polling;
        private readonly ClientSettingRepository _clientRepo = new(AppDatabase.Instance.Connection);

        public ObservableCollection<ClientRowViewModel> ClientRows { get; } = new();

        private ClientRowViewModel? _selectedClientRow;
        public ClientRowViewModel? SelectedClientRow
        {
            get => _selectedClientRow;
            set
            {
                if (SetProperty(ref _selectedClientRow, value))
                {
                    // RelayCommand가 CanExecuteChanged를 자동으로 안 올려주면 아래 호출이 필요할 수 있음
                    (OpenClientSettingCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (CopyClientCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (DeleteClientCommand as RelayCommand)?.RaiseCanExecuteChanged();
                }
            }
        }

        public ICommand NewClientCommand { get; }
        public ICommand CopyClientCommand { get; }
        public ICommand DeleteClientCommand { get; }
        public ICommand OpenClientSettingCommand { get; }

        public MainSettingViewModel(IControllerService controller, IPollingService polling)
        {
            _controller = controller;
            _polling = polling;

            NewClientCommand = new RelayCommand(OnNewClient);
            CopyClientCommand = new RelayCommand(OnCopyClient, () => SelectedClientRow != null);
            DeleteClientCommand = new RelayCommand(OnDeleteClient, () => SelectedClientRow != null);
            OpenClientSettingCommand = new RelayCommand(OpenClientSetting, () => SelectedClientRow != null);

            // DB에서 저장된 클라이언트 로드
            LoadClientsFromDb();

            // DB에 데이터가 없으면 기본 행 하나 추가
            if (ClientRows.Count == 0)
            {
                var row = new ClientRowViewModel(Guid.NewGuid(), "new_client", "0.0.0.0");
                ClientRows.Add(row);
                SelectedClientRow = row;
            }
        }

        private void LoadClientsFromDb()
        {
            var saved = _clientRepo.FindAll();
            foreach (var e in saved)
            {
                var row = new ClientRowViewModel(Guid.NewGuid(), e.Name, e.Ip)
                {
                    DbId = e.Id
                };
                ClientRows.Add(row);

                // ChannelVm에 DB 설정값 복원
                var ch = DashboardChannelHub.Instance.AssignOrGet(
                    row.ClientId, row.ClientName, row.ClientIp);

                ch.DbId = e.Id;
                ch.RtspUrl = e.CamPath;
                ch.BaseDirPath = e.ModelBaseDirPath;
                ch.ModelType = (ModelType)e.ModelType;
                ch.SaveDir = e.SaveDir;
                ch.SavePreMs = e.SavePreMs;
                ch.SavePostMs = e.SavePostMs;
                ch.SaveCooldownMs = e.SaveCooldownMs;
                ch.JpegQuality = e.JpegQuality;

                // CamPath가 있으면 스트림 자동 시작
                if (!string.IsNullOrWhiteSpace(e.CamPath))
                {
                    ch.StatusText = e.CamPath.Contains("rtsp", StringComparison.OrdinalIgnoreCase)
                        ? "RTSP: Starting..."
                        : "FILE: Starting...";

                    _polling.StartStream(row.ClientId, e.CamPath, 30);
                }
            }
            SelectedClientRow = ClientRows.FirstOrDefault();
        }

        private void OnNewClient()
        {
            // 새 Client Row 추가
            var newId = Guid.NewGuid();

            // 이름은 중복 피하려고 넘버링
            int n = ClientRows.Count + 1;
            string name = $"new_client_{n}";
            string ip = "0.0.0.0";

            var row = new ClientRowViewModel(newId, name, ip);
            ClientRows.Add(row);
            SelectedClientRow = row;
        }

        private void OnCopyClient()
        {
            if (SelectedClientRow == null) return;

            var copyId = Guid.NewGuid();
            string name = $"{SelectedClientRow.ClientName}_copy";
            string ip = SelectedClientRow.ClientIp;

            var row = new ClientRowViewModel(copyId, name, ip);
            ClientRows.Add(row);
            SelectedClientRow = row;
        }

        private void OnDeleteClient()
        {
            if (SelectedClientRow == null) return;

            var toRemove = SelectedClientRow;
            int idx = ClientRows.IndexOf(toRemove);

            // DB에서도 삭제
            if (toRemove.DbId > 0)
                _clientRepo.Delete(toRemove.DbId);

            ClientRows.Remove(toRemove);

            // 선택 유지(가능하면 근처 행 선택)
            if (ClientRows.Count == 0)
            {
                SelectedClientRow = null;
            }
            else
            {
                int next = Math.Clamp(idx, 0, ClientRows.Count - 1);
                SelectedClientRow = ClientRows[next];
            }

            // (옵션) 삭제된 client가 스트리밍 중이었다면 정지
            // _polling.StopStream(toRemove.ClientId);
        }

        private void OpenClientSetting()
        {
            if (SelectedClientRow == null) return;

            var ch = DashboardChannelHub.Instance.AssignOrGet(
                SelectedClientRow.ClientId,
                SelectedClientRow.ClientName,
                SelectedClientRow.ClientIp);

            var vm = new SettingsViewModel(
                _controller,
                _polling,
                ch,
                SelectedClientRow.ClientId,
                SelectedClientRow.ClientName,
                SelectedClientRow.ClientIp);

            var view = new SettingsView { DataContext = vm };

            var win = new SettingsPopupWindow(view);
            vm.RequestClose += () => win.Close();

            win.ShowDialog();
        }

       // public IPollingService Polling => _polling;
    }
}
