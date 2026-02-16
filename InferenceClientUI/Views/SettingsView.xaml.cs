using InferenceClientUI.ViewModels;
using Microsoft.Win32;
using System.Windows;
using System.Windows.Controls;

namespace InferenceClientUI.Views
{
    public partial class SettingsView : UserControl
    {
        public SettingsView()
        {
            InitializeComponent();
            Loaded += SettingsView_Loaded;
            Unloaded += SettingsView_Unloaded;
        }

        private void SettingsView_Loaded(object sender, RoutedEventArgs e)
        {
            if (DataContext is not SettingsViewModel vm) return;

            vm.RequestBrowseCameraStream += OnBrowseCameraStream;
            vm.RequestImportModel += OnImportModel;
        }

        private void SettingsView_Unloaded(object sender, RoutedEventArgs e)
        {
            if (DataContext is not SettingsViewModel vm) return;

            vm.RequestBrowseCameraStream -= OnBrowseCameraStream;
            vm.RequestImportModel -= OnImportModel;
        }

        private void OnBrowseCameraStream()
        {
            var dlg = new OpenFileDialog
            {
                Title = "Select video file",
                Filter = "Video Files|*.mp4;*.avi;*.mkv;*.mov|All Files|*.*",
                CheckFileExists = true
            };

            if (dlg.ShowDialog(Application.Current?.MainWindow) == true)
            {
                if (DataContext is SettingsViewModel vm)
                    vm.CameraStream = dlg.FileName;
            }
        }

        private void OnImportModel()
        {
            var dlg = new OpenFolderDialog
            {
                Title = "Select model base directory"
            };

            if (dlg.ShowDialog(Application.Current?.MainWindow) == true)
            {
                if (DataContext is SettingsViewModel vm)
                    vm.ModelPath = dlg.FolderName;
            }
        }
    }
}
