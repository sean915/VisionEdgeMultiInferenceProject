using System;
using System.Collections.Generic;
using System.Text;

namespace InferenceClientUI.Models
{
    public class DailyStatsDto
    {
        public ulong ClientId { get; set; }
        public int Total { get; set; }
        public int Defect { get; set; }
        public int Normal { get; set; }
    }
}
