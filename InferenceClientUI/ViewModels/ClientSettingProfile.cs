using System;
using System.Collections.Generic;
using System.Text;

namespace InferenceClientUI.ViewModels
{
    // ✅ Settings / MainSetting / ClientRow 어디서든 공통으로 쓰는 타입은 별도 파일로 분리하는 게 안전
    public enum ClientSettingProfile
    {
        FingerGripper,
        PreWelder,
        AnvilView,
        HornView,
        SortingMachine,
        TabFolding,
        StackMagazine,
        CutterMagazine
    }

}
