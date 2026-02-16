using InferenceClientUI.Services;
using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Input;

namespace InferenceClientUI.ViewModels
{
    public sealed class ClientDashboardViewModel : ObservableObject
    {
        public ChannelVm Channel { get; }

        private readonly IPollingService? _polling;

        public string Title => Channel.Title;

        public ICommand PlayCommand { get; }
        public ICommand StopCommand { get; }

        public ClientDashboardViewModel(ChannelVm channel, IPollingService? polling)
        {
            Channel = channel;
            _polling = polling;

            PlayCommand = new RelayCommand(OnPlay);
            StopCommand = new RelayCommand(OnStop);
        }

        private void OnPlay()
        {
            if (_polling == null) return;
            if (Channel.ClientId == Guid.Empty) return;
            if (string.IsNullOrWhiteSpace(Channel.RtspUrl)) return;

            _polling.StartStream(Channel.ClientId, Channel.RtspUrl, 10);
        }

        private void OnStop()
        {
            if (_polling == null) return;
            _polling.StopStream(Channel.ClientId);
        }

        private static IPollingService? FindPolling(object? root)
        {
            if (root == null) return null;

            foreach (var p in root.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (p.GetValue(root) is IPollingService ps)
                    return ps;
            }
            return null;
        }


        //public void Init()
        //{
        //    try
        //    {
        //        if (_algo != IntPtr.Zero) return;

        //        if (!File.Exists(DllPath) && !DllPath.Equals("HmStkDLL.dll", StringComparison.OrdinalIgnoreCase))
        //        {
        //            Log($"WARN: DLL path not found: {DllPath} (DllImport는 기본 검색 경로를 사용합니다)");
        //        }

        //        if (!File.Exists(TriggerPath)) Log($"WARN: trigger engine not found: {TriggerPath}");
        //        if (!File.Exists(DefectPath)) Log($"WARN: defect engine not found: {DefectPath}");

        //        _triggerStr = Marshal.StringToHGlobalAnsi(TriggerPath);
        //        _defectStr = Marshal.StringToHGlobalAnsi(DefectPath);

        //        var cfg = new Native.AlgorithmConfig
        //        {
        //            triggerModelPath = _triggerStr,
        //            defectModelPath = _defectStr,
        //            confThreshold = ConfThr,
        //            useCuda = UseCuda ? 1 : 0,
        //            struct_size = (uint)Marshal.SizeOf<Native.AlgorithmConfig>()
        //        };

        //        _algo = Native.CreateAlgorithm(ref cfg);
        //        if (_algo == IntPtr.Zero) throw new Exception("CreateAlgorithm returned null");

        //        _cbKeepAlive = OnNativeResult; // ✅ delegate keep-alive
        //        Native.SetResultCallback(_algo, _cbKeepAlive);

        //        int rc = Native.Initialize(_algo);
        //        if (rc != 0) throw new Exception($"Initialize failed rc={rc}");

        //        StatusText = "READY";
        //        Log("Initialized OK");
        //    }
        //    catch (Exception ex)
        //    {
        //        StatusText = "ERROR";
        //        Log("Init ERROR: " + ex.Message);
        //        SafeDestroy();
        //    }
        //    finally
        //    {
        //        CmdInit.RaiseCanExecuteChanged();
        //        CmdStart.RaiseCanExecuteChanged();
        //        CmdStop.RaiseCanExecuteChanged();
        //    }
        //}

        //public void Start()
        //{
        //    if (_algo == IntPtr.Zero) return;
        //    if (_worker != null) return;

        //    _cts = new CancellationTokenSource();
        //    var token = _cts.Token;

        //    _worker = Task.Run(() => CaptureLoop(token), token);

        //    StatusText = "RUNNING";
        //    Log("Capture started");
        //    CmdStart.RaiseCanExecuteChanged();
        //    CmdStop.RaiseCanExecuteChanged();
        //}

        //public void Stop()
        //{
        //    try
        //    {
        //        _cts?.Cancel();
        //        _worker?.Wait(1000);
        //    }
        //    catch { /* ignore */ }
        //    finally
        //    {
        //        _cts = null;
        //        _worker = null;
        //    }

        //    SafeDestroy();
        //    StatusText = "STOPPED";
        //    Log("Stopped");

        //    CmdInit.RaiseCanExecuteChanged();
        //    CmdStart.RaiseCanExecuteChanged();
        //    CmdStop.RaiseCanExecuteChanged();
        //}
    }
}
