//using InferenceClientUI.Infrastructure.Interop;
//using InferenceClientUI.Models;
//using System;
//using System.Collections.Generic;
//using System.Linq;
//using System.Text;
//using System.Threading.Tasks;

//namespace InferenceClientUI.Services.Inference
//{
//    public sealed class DefectClassifier  
//    {
//        public readonly record struct DefectSummary(int NgCount, int OkCount, int NoneCount)
//        {
//            public bool HasDefect => NgCount > 0;
//            public int TotalCount => NgCount + OkCount + NoneCount;
//        }
//        public DefectSummary ClassifyDefect(ReadOnlySpan<InferenceNative.ResultItemC> items)
//        {
//            int ng = 0, ok = 0, none = 0;

//            for (int i = 0; i < items.Length; i++)
//            {
//                switch ((DefectTypeEnum)items[i].defect_type)
//                {
//                    case DefectTypeEnum.NG: ng++; break;
//                    case DefectTypeEnum.OK: ok++; break;
//                    case DefectTypeEnum.NONE:
//                    default: none++; break;
//                }
//            }

//            return new DefectSummary(ng, ok, none);
//        }
//    }
//}