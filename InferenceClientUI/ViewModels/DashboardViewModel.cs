using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Services;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using System.Windows;

namespace InferenceClientUI.ViewModels
{
    public sealed class DashboardViewModel : ObservableObject
    {
        private readonly IControllerService _controller;
        private readonly IPollingService _polling;

        public ObservableCollection<ChannelVm> Channels { get; }

        public DashboardViewModel(IControllerService controller, IPollingService polling)
        {
            _controller = controller;
            _polling = polling;

            Channels = DashboardChannelHub.Instance.Channels;
        }

        public Task InitializeAsync()
        {
            //// 모델 파일 위치: 실행 폴더 기준 예시
            //var exeDir = "D:\\wa\\defect";
            //var modelPath = Path.Combine(exeDir, "yolonas_s_v0.2.1.onnx");

            //int r = InferenceNative.infer_init(modelPath, useCuda: 0, deviceId: 0, inputW: 640, inputH: 640);
            //if (r != 0)
            //{
            //    var msg = InferenceNative.GetLastError();
            //    // 너 UI 스타일에 맞춰 StatusText로 뿌리거나 MessageBox
            //    MessageBox.Show($"infer_init failed: {r}\n{msg}\nmodelPath={modelPath}");
            //}

            return Task.CompletedTask;
        }
    }
}
