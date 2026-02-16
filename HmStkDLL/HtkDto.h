#pragma once
//#include "vacl_define.h"
//#include "defines/Define_OpenCV.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>


	enum class DefectTypeEnum {
		NONE = 0,
		NG = 1,
		OK = 2,
	};

	struct Threshold {
		DefectTypeEnum defect_type; // defect type
		unsigned int size; // size threshold
		unsigned int q_score; // questionable score. 100점 기준
		unsigned int ab_score; // abnormal score. 100점 기준
	};

	class Frame {
	public:
		uint64_t id;
		cv::Mat data; //원본 사이즈 or 640
		std::chrono::system_clock::time_point timestamp;
		// bool is_protected = false;
		int ref_count = 0;

		Frame(uint64_t id, cv::Mat data) {
			this->id = id;
			this->data = data;
			this->timestamp = std::chrono::system_clock::now();
		}

		size_t getMemSize() {
			return sizeof(this) + this->data.total() * this->data.elemSize();
		}
	};


	struct PlcInfo {
		std::string cell_id;
		unsigned int pallete_num;
	};


	struct ResultItem {
		DefectTypeEnum defect_type; // 결함 유형
		float score; // 점수
		Box box; // 결함 위치
	};

	struct Box {
		unsigned int x1;
		unsigned int y1;
		unsigned int x2;
		unsigned int y2;
	};
	//}



	enum class ErrorCodeEnum {
		SUCCESS = 0,
		MODEL_FILE_NOT_EXIST = 1,
		MODEL_INIT_FAIL = 2,
		FRAME_INVALID = 3,
		ROI_INVALID = 4,
		DNN_PROCESS_FAIL = 5,
		UNKNOWN_ERROR = 99,
	};

	struct DetectInputDTO {
		std::shared_ptr<Frame> frame;
		PlcInfo plc_info;
	};

	struct DetectOutputDTO {
		std::shared_ptr<Frame> frame;
		cv::Mat visualized_image;
		unsigned int takt_time;
		std::vector<ResultItem> ab_results;
		std::vector<ResultItem> q_results;
		std::vector<ResultItem> ok_results;
		ErrorCodeEnum error_code;
		std::string error_msg;
		PlcInfo plc_info;
	};
	struct Roi {
		unsigned int x1; // x1 좌표
		unsigned int y1; // y1 좌표
		unsigned int width; // 너비
		unsigned int height; // 높이
	};

	inline std::string to_string(DefectTypeEnum defect_type) {
		switch (defect_type) {
		case DefectTypeEnum::NONE:
			return "NONE";
		case DefectTypeEnum::NG:
			return "NG";
		default:
			return "UNKNOWN";
		}
	}


	inline std::string to_string(ErrorCodeEnum error_code) {
		switch (error_code) {
		case ErrorCodeEnum::SUCCESS:
			return "SUCCESS";
		case ErrorCodeEnum::MODEL_FILE_NOT_EXIST:
			return "MODEL_FILE_NOT_EXIST";
		case ErrorCodeEnum::MODEL_INIT_FAIL:
			return "MODEL_INIT_FAIL";
		case ErrorCodeEnum::FRAME_INVALID:
			return "FRAME_INVALID";
		case ErrorCodeEnum::ROI_INVALID:
			return "ROI_INVALID";
		case ErrorCodeEnum::DNN_PROCESS_FAIL:
			return "DNN_PROCESS_FAIL";
		case ErrorCodeEnum::UNKNOWN_ERROR:
			return "UNKNOWN_ERROR";
		default:
			return "UNDEFINED_ERROR_CODE";
		}
	}



//}



