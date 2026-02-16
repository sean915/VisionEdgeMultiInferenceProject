using System;
using System.Threading.Tasks;

namespace InferenceClientUI.Services
{
    public interface IPollingService
    {
        void StartStream(Guid clientId, string source, int fps = 10);
        void StopStream(Guid clientId);

      
    }
}
