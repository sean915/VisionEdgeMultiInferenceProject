

#pragma once
namespace HmCutter {

	enum class DefectTypeEnum {
		NONE = 0,
		NG = 1,
		OK = 2,
	};

	struct Box {
		unsigned int x1;
		unsigned int y1;
		unsigned int x2;
		unsigned int y2;
	};


	struct ResultItem {
		DefectTypeEnum defect_type; // 결함 유형
		float score; // 점수
		Box box; // 결함 위치
	};

} // namespace HMSTACK