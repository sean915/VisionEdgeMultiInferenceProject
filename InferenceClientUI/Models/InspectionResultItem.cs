using System;

namespace InferenceClientUI.Models
{
    /// <summary>
    /// Inspection Result 패널에 표시되는 단일 검출 결과 행.
    /// </summary>
    public sealed class InspectionResultItem
    {
        public int Index { get; init; }
        public string Timestamp { get; init; } = "";
        public string Result { get; init; } = "NG";
        public int NgCount { get; init; }
        public int OkCount { get; init; }
        public ulong FrameIndex { get; init; }
    }
}
