using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using InferenceClientUI.ViewModels;

namespace InferenceClientUI.Views
{
    public sealed class SettingsPopupWindow : Window
    {
        private SettingsViewModel? _vm;

        public SettingsPopupWindow(UserControl content)
        {
            Title = "Client Setting";
            Width = 980;
            Height = 680;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            ResizeMode = ResizeMode.CanResize;

            // ✅ 테마 리소스 상속(Owner 중요)
            Owner = Application.Current?.MainWindow;

            // ✅ "실행하면 흰 배경" 방지: Window/Root에 확실히 배경을 깔아준다
            // (Designer에서는 투명으로도 보이지만, 런타임에서 Host가 흰색이면 그대로 비침)
            Background = TryFindBrush("AppBg") ?? Brushes.Black;
            AllowsTransparency = false;

            Content = new Border
            {
                Padding = new Thickness(12),
                Background = Brushes.Transparent,
                Child = content
            };

            Loaded += (_, __) => HookVm(content);
            content.DataContextChanged += (_, __) => HookVm(content);

            Closing += OnClosing;
        }

        private void HookVm(UserControl content)
        {
            // 기존 VM 구독 해제
            if (_vm != null)
            {
                _vm.RequestClose -= Vm_RequestClose;
                _vm = null;
            }

            // 새 VM 구독
            _vm = content.DataContext as SettingsViewModel;
            if (_vm != null)
            {
                _vm.RequestClose -= Vm_RequestClose;
                _vm.RequestClose += Vm_RequestClose;
            }
        }

        private void Vm_RequestClose()
        {
            // ✅ VM에서 이벤트 쏘면 UI 스레드에서 닫기
            if (!Dispatcher.CheckAccess())
            {
                Dispatcher.Invoke(Close);
                return;
            }
            Close();
        }

        private void OnClosing(object? sender, CancelEventArgs e)
        {
            // 메모리 누수 방지(이벤트 구독 해제)
            if (_vm != null)
            {
                _vm.RequestClose -= Vm_RequestClose;
                _vm = null;
            }
        }

        private static Brush? TryFindBrush(string key)
        {
            try
            {
                if (Application.Current == null) return null;
                var obj = Application.Current.TryFindResource(key);
                return obj as Brush;
            }
            catch
            {
                return null;
            }
        }
    }
}
