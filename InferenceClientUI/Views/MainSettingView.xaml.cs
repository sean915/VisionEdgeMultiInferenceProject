using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace InferenceClientUI.Views
{
    public partial class MainSettingView : UserControl
    {
        public MainSettingView()
        {
            InitializeComponent();
        }

        private void MainSettingView_Loaded(object sender, RoutedEventArgs e)
        {
            // 초기 표시: General
            ShowGeneral();
        }

        private void TabGeneral_Click(object sender, RoutedEventArgs e)
        {
            ShowGeneral();
        }

        private void TabClient_Click(object sender, RoutedEventArgs e)
        {
            ShowClient();
        }

        private void ShowGeneral()
        {
            if (TabGeneral == null || TabClient == null || GeneralPanel == null || ClientPanel == null)
                return;

            TabGeneral.IsChecked = true;
            TabClient.IsChecked = false;

            GeneralPanel.Visibility = Visibility.Visible;
            ClientPanel.Visibility = Visibility.Collapsed;
        }

        private void ShowClient()
        {
            if (TabGeneral == null || TabClient == null || GeneralPanel == null || ClientPanel == null)
                return;

            TabGeneral.IsChecked = false;
            TabClient.IsChecked = true;

            GeneralPanel.Visibility = Visibility.Collapsed;
            ClientPanel.Visibility = Visibility.Visible;
        }

        /// <summary>
        /// DataGrid 안의 버튼(Setting 등)을 눌렀을 때:
        /// - 해당 행을 먼저 선택(SelectedItem 세팅)
        /// - BUT 이벤트를 먹지 않음(e.Handled = false 유지) => 버튼 Click/Command 정상 동작
        /// </summary>
        private void SettingButton_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is not FrameworkElement fe)
                return;

            var rowItem = fe.DataContext;
            if (rowItem == null)
                return;

            var dataGrid = FindAncestor<DataGrid>(fe);
            if (dataGrid == null)
                return;

            // ✅ 버튼 누르는 순간 해당 Row를 선택시켜서
            //    Copy/Delete 같은 "SelectedItem 기반 Command"도 안 막히게 함
            if (!Equals(dataGrid.SelectedItem, rowItem))
            {
                dataGrid.SelectedItem = rowItem;
                dataGrid.ScrollIntoView(rowItem);
            }

            // ❌ 절대 e.Handled = true 하지 말 것 (버튼 클릭/커맨드가 죽음)
            // e.Handled = false; // 기본값이 false라 명시 안 해도 됨
        }

        private static T? FindAncestor<T>(DependencyObject child) where T : DependencyObject
        {
            DependencyObject? current = child;
            while (current != null)
            {
                if (current is T match)
                    return match;
                current = VisualTreeHelper.GetParent(current);
            }
            return null;
        }
    }
}
