using InferenceClientUI.InfraStructure;
using InferenceClientUI.Services;
using System.Threading.Tasks;

namespace InferenceClientUI.ViewModels
{
    public sealed class MainViewModel : ObservableObject
    {
        private readonly IControllerService _controller;
        private readonly IPollingService _polling;

        public IPollingService Polling => _polling;

        public DashboardViewModel DashboardVM { get; }
        public MainSettingViewModel MainSettingVM { get; }

        private object? _contentViewModel;
        public object? ContentViewModel
        {
            get => _contentViewModel;
            set => SetProperty(ref _contentViewModel, value);
        }

        public RelayCommand OpenMainSettingsCommand { get; }
        public RelayCommand BackToMainCommand { get; }

        public MainViewModel()
            : this(new ControllerService())
        {
        }

        public MainViewModel(IControllerService controller)
        {
            _controller = controller;
            _polling = new PollingService(_controller);

            DashboardVM = new DashboardViewModel(_controller, _polling);
            MainSettingVM = new MainSettingViewModel(_controller, _polling);

            ContentViewModel = DashboardVM;

            OpenMainSettingsCommand = new RelayCommand(() => ContentViewModel = MainSettingVM);
            BackToMainCommand = new RelayCommand(() => ContentViewModel = DashboardVM);
        }

        public async Task InitializeAsync()
        {
            // Dashboard가 먼저 Polling 초기화하도록
            await DashboardVM.InitializeAsync();

            // (원래 너가 하던 초기화가 있으면 이어서)
            // await _controller.InitializeAsync();  <- 이런게 있으면 유지
        }


    }
}
