using InferenceClientUI.Models;
using System;
using System.Collections.Generic;
using System.Text;

namespace InferenceClientUI.Services
{
    public interface IControllerService
    {
        Task<List<ClientInfoDto>> GetClientsAsync();
        Task<List<DailyStatsDto>> GetDailyStatsAsync();
        // 프레임/알림은 나중에
    }
}
