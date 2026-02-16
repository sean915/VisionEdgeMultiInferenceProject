using InferenceClientUI.ViewModels;
using System;
using System.Collections.ObjectModel;
using System.Linq;

namespace InferenceClientUI.Services
{
    public sealed class DashboardChannelHub
    {
        public const int MaxChannels = 10;

        public static DashboardChannelHub Instance { get; } = new DashboardChannelHub();

        public ObservableCollection<ChannelVm> Channels { get; } = new ObservableCollection<ChannelVm>();

        private DashboardChannelHub()
        {
            for (int i = 0; i < MaxChannels; i++)
                Channels.Add(new ChannelVm(i));
        }

        public ChannelVm AssignOrGet(Guid clientId, string clientName, string clientIp)
        {
            if (clientId == Guid.Empty)
                throw new ArgumentException("clientId is empty");

            // 이미 할당된 채널이 있으면 그대로
            var existing = Channels.FirstOrDefault(c => c.IsAssigned && c.ClientId == clientId);
            if (existing != null)
            {
                // 최신 이름/IP 반영
                existing.ClientName = clientName ?? "";
                existing.ClientIp = clientIp ?? "";
                return existing;
            }

            // 빈 슬롯 할당
            var empty = Channels.FirstOrDefault(c => !c.IsAssigned);
            if (empty == null)
            {
                // 정책: 꽉 차면 0번 덮어쓰기(원하면 다른 정책으로 바꿔도 됨)
                empty = Channels[0];
                empty.ClearAssignment();
            }

            empty.IsAssigned = true;
            empty.ClientId = clientId;
            empty.ClientName = clientName ?? "";
            empty.ClientIp = clientIp ?? "";

            // 초기 상태
            empty.StatusText = string.IsNullOrWhiteSpace(empty.RtspUrl) ? "RTSP: (empty)" : "Ready";
            empty.HasFrame = false;

            return empty;
        }

        public bool TryGetChannel(Guid clientId, out ChannelVm channel)
        {
            channel = Channels.FirstOrDefault(c => c.IsAssigned && c.ClientId == clientId)!;
            return channel != null;
        }

        public void RemoveForClient(Guid clientId)
        {
            var ch = Channels.FirstOrDefault(c => c.IsAssigned && c.ClientId == clientId);
            if (ch != null) ch.ClearAssignment();
        }
    }
}
