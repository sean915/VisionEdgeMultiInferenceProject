using InferenceClientUI.Models;
using System;
using System.Collections.Generic;
using System.Text;

namespace InferenceClientUI.Services
{
    public class ControllerService : IControllerService
    {
        // 구현...
        public Task<List<ClientInfoDto>> GetClientsAsync()
    => Task.FromResult(new List<ClientInfoDto>());

        public Task<List<DailyStatsDto>> GetDailyStatsAsync()
            => Task.FromResult(new List<DailyStatsDto>());
    }
}