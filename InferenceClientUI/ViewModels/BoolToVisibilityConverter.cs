using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace InferenceClientUI.InfraStructure
{
    public sealed class BoolToVisibilityConverter : IValueConverter
    {
        public bool Invert { get; set; } = false;
        public bool CollapseWhenFalse { get; set; } = true;

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            var flag = value is bool b && b;

            if (parameter is string p)
            {
                // XAML에서 parameter="invert" 같은 식으로도 사용 가능
                if (string.Equals(p, "invert", StringComparison.OrdinalIgnoreCase))
                    flag = !flag;
            }

            if (Invert) flag = !flag;

            if (flag) return Visibility.Visible;
            return CollapseWhenFalse ? Visibility.Collapsed : Visibility.Hidden;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => value is Visibility v && v == Visibility.Visible;
    }
}
