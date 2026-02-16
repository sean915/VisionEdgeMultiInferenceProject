using InferenceClientUI.Services;
using InferenceClientUI.Services.Database;
using InferenceClientUI.Themes;
using InferenceClientUI.ViewModels;
using System.Windows;

namespace InferenceClientUI
{
    public partial class App : Application
    {
        protected override async void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            ThemeManager.ApplyDark();

            // SQLite 테이블 생성 (없으면 CREATE)
            AppDatabase.Instance.EnsureTables();

            var mainVm = new MainViewModel();

            var window = new MainWindow
            {
                DataContext = mainVm
            };
            window.Show();

            await mainVm.InitializeAsync();
        }

    }
}
