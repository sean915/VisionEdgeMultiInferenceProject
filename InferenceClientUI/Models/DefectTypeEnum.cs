using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace InferenceClientUI.Models
{
    /// <summary>
    /// C++ DLL <c>DefectTypeEnum</c> 과 1:1 매핑.
    /// HmCutterDLL / HmStkDLL 의 ResultItem.defect_type 값과 동일합니다.
    /// </summary>
    public enum DefectTypeEnum
    {
        NONE = 0,
        NG = 1,
        OK = 2,
    }
}
