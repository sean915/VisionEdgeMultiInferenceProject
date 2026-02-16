namespace InferenceClientUI.Models.Entities
{
    /// <summary>
    /// client_setting 테이블에 대응하는 엔티티.
    /// C++ InferenceClient 엔티티의 WPF 측 간소화 버전.
    /// </summary>
    public sealed class ClientSettingEntity
    {
        public long Id { get; set; }
        public string Name { get; set; } = "";
        public string CamPath { get; set; } = "";
        public string Ip { get; set; } = "0.0.0.0";
        public int Port { get; set; }
        public string ModelBaseDirPath { get; set; } = "";
        public int ModelType { get; set; }
        public float ConfThreshold { get; set; } = 0.5f;
        public bool UseCuda { get; set; } = true;
        public string SaveDir { get; set; } = "";
        public int SavePreMs { get; set; } = 5000;
        public int SavePostMs { get; set; } = 5000;
        public int SaveCooldownMs { get; set; } = 3000;
        public int JpegQuality { get; set; } = 80;
    }
}
