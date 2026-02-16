using System.Windows.Media;
using System.Windows.Media.Imaging;

public sealed class FrameDto
{
    public ulong ClientId { get; init; }
    public ImageSource Image { get; init; } = default!;

}
