namespace InferenceClientUI.Models.Entities
{
    /// <summary>
    /// global_setting 테이블에 대응하는 엔티티.
    /// C++ GlobalSetting 엔티티의 WPF 측 포팅.
    /// </summary>
    public sealed class GlobalSettingEntity
    {
        public long Id { get; set; }
        public string LogDir { get; set; } = "";
        public bool AutoStart { get; set; }
        public int Language { get; set; }
        public int DiskThreshold { get; set; } = 80;
    }
}
