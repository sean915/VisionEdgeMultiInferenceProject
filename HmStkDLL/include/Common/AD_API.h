#pragma once

#ifdef HMSTKDLL_EXPORTS
#define AD_API __declspec(dllexport)
#else
#define AD_API __declspec(dllimport)
#endif
