namespace InferenceClientUI.Infrastructure.Interop
{
    /// <summary>
    /// 추론 DLL 선택용 모델 타입.
    /// </summary>
    public enum ModelType
    {
        /// <summary>Stack Magazine → HmStkDLL.dll</summary>
        StackMagazine,

        /// <summary>Cutter Magazine → HmCutterDLL.dll</summary>
        CutterMagazine
    }

    public static class ModelTypeExtensions
    {
        public static string ToDllName(this ModelType type) => type switch
        {
            ModelType.StackMagazine => "HmStkDLL.dll",
            ModelType.CutterMagazine => "HmCutterDLL.dll",
            _ => "HmCutterDLL.dll"
        };

        public static string ToDisplayName(this ModelType type) => type switch
        {
            ModelType.StackMagazine => "Stack Magazine (HmStkDLL)",
            ModelType.CutterMagazine => "Cutter Magazine (HmCutterDLL)",
            _ => type.ToString()
        };
    }
}
