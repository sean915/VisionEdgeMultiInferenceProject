#include <memory>
#include "Include/Data/Frame.h"
#include "PlcInfo.h"

struct DetectInputDTO
{
	std::shared_ptr<HMSTACK::Frame> frame;
	PlcInfo plc_info;
};
