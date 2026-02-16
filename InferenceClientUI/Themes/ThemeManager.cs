using System;
using System.Linq;
using System.Windows;

namespace InferenceClientUI.Themes
{
    public static class ThemeManager
    {
        private static readonly Uri DarkUri = new Uri("Themes/Theme.Dark.xaml", UriKind.Relative);
        private static readonly Uri LightUri = new Uri("Themes/Theme.Light.xaml", UriKind.Relative);

        public static bool IsDark { get; private set; } = true;

        public static void ApplyDark() => Apply(DarkUri, true);
        public static void ApplyLight() => Apply(LightUri, false);

        public static void Toggle()
        {
            if (IsDark) ApplyLight();
            else ApplyDark();
        }

        private static void Apply(Uri themeUri, bool isDark)
        {
            var app = Application.Current;
            if (app == null) return;

            var dicts = app.Resources.MergedDictionaries;

            // 기존 테마 제거
            var oldTheme = dicts.FirstOrDefault(d =>
                d.Source != null &&
                d.Source.OriginalString.Contains("Themes/Theme.", StringComparison.OrdinalIgnoreCase));

            if (oldTheme != null)
                dicts.Remove(oldTheme);

            // 새 테마를 최상단에 삽입 (우선순위 보장)
            dicts.Insert(0, new ResourceDictionary { Source = themeUri });

            IsDark = isDark;
        }
    }
}
