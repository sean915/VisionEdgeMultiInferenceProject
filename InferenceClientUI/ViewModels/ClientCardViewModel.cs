using InferenceClientUI.InfraStructure;
using System;
using System.Windows.Media;

namespace InferenceClientUI.ViewModels
{
    public sealed class ClientCardViewModel : ObservableObject
    {
        public ulong Id { get; }
        public string Name { get; }

        private ImageSource? _frameImage;
        public ImageSource? FrameImage
        {
            get => _frameImage;
            set => SetProperty(ref _frameImage, value);
        }

        private int _total;
        public int Total { get => _total; set => SetProperty(ref _total, value); }

        private int _defect;
        public int Defect { get => _defect; set => SetProperty(ref _defect, value); }

        private int _normal;
        public int Normal { get => _normal; set => SetProperty(ref _normal, value); }

        public RelayCommand OpenDetailCommand { get; }

        public ClientCardViewModel(ulong id, string name, Action<ulong> openDetail)
        {
            Id = id;
            Name = name;
            OpenDetailCommand = new RelayCommand(() => openDetail?.Invoke(Id));
        }

    }
}
