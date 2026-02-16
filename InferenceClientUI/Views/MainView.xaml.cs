using InferenceClientUI.Infrastructure.Interop;
using InferenceClientUI.Services;
using InferenceClientUI.Themes;
using InferenceClientUI.ViewModels;
using InferenceClientUI.Views;
using System;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace InferenceClientUI.Views
{
    public partial class MainView : UserControl
    {
        public MainView()
        {
            InitializeComponent();
        }

        private void ClientList_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            if (DataContext == null) return;

            var selected = GetSelectedClientRow(DataContext);
            if (selected == null) return;

            var clientId = GetGuid(selected, "ClientId", "Id");
            if (clientId == Guid.Empty)
            {
                MessageBox.Show("ClientId를 찾을 수 없습니다.");
                return;
            }

            var clientName = GetString(selected, "ClientName", "Name") ?? "client";
            var clientIp   = GetString(selected, "ClientIp", "Ip") ?? "";

            // ✅ 채널 확보
            var channel = DashboardChannelHub.Instance.AssignOrGet(clientId, clientName, clientIp);

            // ✅ 우측 View 생성
            var polling = (DataContext as MainViewModel)?.Polling;
            var vm = new ClientDashboardViewModel(channel, polling);
            var view = new ClientDashboardView { DataContext = vm };

            SetContentView(DataContext, view);
        }

        private static object? GetSelectedClientRow(object root)
            => root.GetType().GetProperty("MainSettingVM")?
                   .GetValue(root)?
                   .GetType().GetProperty("SelectedClientRow")?
                   .GetValue(
                       root.GetType().GetProperty("MainSettingVM")?.GetValue(root)
                   );

        private static void SetContentView(object root, object view)
        {
            var prop = root.GetType().GetProperty("ContentViewModel");
            prop?.SetValue(root, view);
        }

        private static Guid GetGuid(object o, params string[] names)
        {
            foreach (var n in names)
            {
                var p = o.GetType().GetProperty(n);
                if (p?.GetValue(o) is Guid g) return g;
            }
            return Guid.Empty;
        }

        private static string? GetString(object o, params string[] names)
        {
            foreach (var n in names)
            {
                var p = o.GetType().GetProperty(n);
                if (p?.GetValue(o) is string s) return s;
            }
            return null;
        }

        private void OnToggleTheme(object sender, RoutedEventArgs e)
        {
            ThemeManager.Toggle();
        }

        private void NativeTest_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var ch = TryGetActiveChannelVm();
                if (ch == null)
                {
                    MessageBox.Show("현재 표시중인 채널을 찾지 못했습니다. (우측 뷰가 열려있는지 확인)");
                    return;
                }

                // ✅ (옵션) 저장 폴더/앞뒤 길이 지정
                if (string.IsNullOrWhiteSpace(ch.SaveDir))
                {
                    ch.SaveDir = System.IO.Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                        "InferenceClips"
                    );
                }
                ch.SavePreMs = 5000;
                ch.SavePostMs = 5000;

                // ✅ 1회성 저장 요청 올리기
                ch.SaveRequested = true;

                // (옵션) 지금 프레임 스냅샷 PNG도 같이 저장하고 싶으면
                // SaveCurrentFramePng(ch);

                MessageBox.Show($"저장요청 OK: CH{ch.ChannelIndex + 1}\n(다음 프레임 들어오면 클립 저장 )");
            }
            catch (Exception ex)
            {
                MessageBox.Show($"요청 실패: {ex.Message}");
            }
        }

        private ChannelVm? TryGetActiveChannelVm()
        {
            if (DataContext == null) return null;

            // ContentViewModel에 들어간 뷰(=UserControl) 또는 VM을 꺼내기
            // 너 코드에서 SetContentView(DataContext, view)로 넣고 있으니
            // ContentViewModel 프로퍼티에 "View"가 들어있음.
            var root = DataContext;

            var cvProp = root.GetType().GetProperty("ContentViewModel");
            var contentObj = cvProp?.GetValue(root);
            if (contentObj == null) return null;

            // contentObj가 View(UserControl)라면 DataContext가 VM일 가능성이 큼
            object? vm = contentObj;
            var dcProp = contentObj.GetType().GetProperty("DataContext");
            if (dcProp != null)
            {
                var dc = dcProp.GetValue(contentObj);
                if (dc != null) vm = dc;
            }

            if (vm == null) return null;

            // VM 안에서 ChannelVm 찾아보기 (프로퍼티 이름 후보들)
            // 1) ChannelVm 직접 노출
            var chVm =
                GetPropValue<ChannelVm>(vm, "Channel") ??
                GetPropValue<ChannelVm>(vm, "ChannelVm") ??
                GetPropValue<ChannelVm>(vm, "SelectedChannel") ??
                GetPropValue<ChannelVm>(vm, "CurrentChannel");

            if (chVm != null) return chVm;

            // 2) Hub/Wrapper 같은 객체 안에 ChannelVm이 있을 수도 있어서 한 번 더 파고듦
            var hub =
                GetPropValue<object>(vm, "ChannelHub") ??
                GetPropValue<object>(vm, "Hub") ??
                GetPropValue<object>(vm, "ChannelHolder") ??
                GetPropValue<object>(vm, "ChannelContext");

            if (hub != null)
            {
                chVm =
                    GetPropValue<ChannelVm>(hub, "Channel") ??
                    GetPropValue<ChannelVm>(hub, "ChannelVm");
                if (chVm != null) return chVm;
            }

            return null;
        }

        private static T? GetPropValue<T>(object obj, string propName) where T : class
        {
            var p = obj.GetType().GetProperty(propName);
            if (p == null) return null;
            return p.GetValue(obj) as T;
        }



    }
}
