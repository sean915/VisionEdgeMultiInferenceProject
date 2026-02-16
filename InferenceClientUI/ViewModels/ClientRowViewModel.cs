using InferenceClientUI.InfraStructure;
using System;

namespace InferenceClientUI.ViewModels
{
    public sealed class ClientRowViewModel : ObservableObject
    {
        public Guid ClientId { get; }

        /// <summary>
        /// SQLite client_setting 테이블의 PK. 0이면 아직 DB에 저장 안 된 상태.
        /// </summary>
        public long DbId { get; set; }

        private string _clientName;
        public string ClientName
        {
            get => _clientName;
            set => SetProperty(ref _clientName, value);
        }

        private string _clientIp;
        public string ClientIp
        {
            get => _clientIp;
            set => SetProperty(ref _clientIp, value);
        }

        public ClientRowViewModel(Guid clientId, string clientName, string clientIp)
        {
            ClientId = clientId;
            _clientName = clientName;
            _clientIp = clientIp;
        }
    }
}
