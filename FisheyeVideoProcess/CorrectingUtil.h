#pragma once

#include "Config.h"
#include <hash_map>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>

enum CorrectingType {
	/* Copy from the very first version */
	BASIC_FORWARD,
	BASIC_REVERSED,

	/* LL group should act the same as above */
	LONG_LAT_MAPPING_FORWARD,
	LONG_LAT_MAPPING_REVERSED,

	PERSPECTIVE_LONG_LAT_MAPPING_CAM_LENS_MOD_FORWARD,
	PERSPECTIVE_LONG_LAT_MAPPING_CAM_LENS_MOD_REVERSED,

	LONG_LAT_MAPPING_CAM_LENS_MOD_UNFIXED_FORWARD,
	LONG_LAT_MAPPING_CAM_LENS_MOD_UNFIXED_REVERSED,

	OPENCV,
};

enum DistanceMappingType {
	LONG_LAT,
	PERSPECTIVE,
};


struct CorrectingParams {
	CorrectingType ctype;
	Point2i centerOfCircle;
	int radiusOfCircle;
	DistanceMappingType dmType;
	Point2d w;
	bool use_reMap;
	/*
		const double theta_left = 0;
		const double phi_up = 0;
		const double camFieldAngle = PI;  // TOSOLVE: remains to be tuned
	*/

	bool operator == (const CorrectingParams &obj) const {
		return ctype == obj.ctype && centerOfCircle == obj.centerOfCircle
			&& radiusOfCircle == obj.radiusOfCircle
			&& dmType == obj.dmType
			&& use_reMap == obj.use_reMap
			&& ((ctype == LONG_LAT_MAPPING_CAM_LENS_MOD_UNFIXED_FORWARD || ctype == LONG_LAT_MAPPING_CAM_LENS_MOD_UNFIXED_REVERSED)
				&& w == obj.w);
	}

	int hashcode() {
		int ret = ctype;
		ret += 0x9e3779b9+(centerOfCircle.x<<6)+(centerOfCircle.x>>2);
		ret += 0x9e3779b9+(centerOfCircle.y<<6)+(centerOfCircle.y>>2);
		ret += 0x9e3779b9+(radiusOfCircle<<6)+(radiusOfCircle>>2);
		ret += 0x9e3779b9+(dmType<<6)+(dmType>>2);
		if (ctype == LONG_LAT_MAPPING_CAM_LENS_MOD_UNFIXED_FORWARD ) {
			ret += ret += 0x9e3779b9+((int)round(w.x*10000)<<6)+((int)round(w.x*10000)>>2);
			ret += 0x9e3779b9+((int)round(w.y*10000)<<6)+((int)round(w.y*10000)>>2);
		}

		return ret;
	}

	CorrectingParams(
		CorrectingType		_ctype = BASIC_REVERSED,
		Point2i				_center = Point2i(0,0),
		int					_radius = 0,
		DistanceMappingType	_dmType = LONG_LAT,
		bool 				_use_ReMap = true){
			ctype = _ctype;
			centerOfCircle = _center;
			radiusOfCircle = _radius;
			dmType = _dmType;
			use_reMap = _use_ReMap;
	}
};

struct ReMapping{
	// For memorization   map[dst] = src;
	bool bMapped;
	struct pairhash {//double hash function for pair key
	public:
		template <typename T, typename U>
		size_t operator()(const std::pair<T, U> &rhs) const {
			size_t l = std::hash<T>()(rhs.first);
			size_t r = std::hash<U>()(rhs.second);
			return l + 0x9e3779b9 + (r << 6) + (r >> 2);
		}
	};


	std::unordered_map<std::pair<int,int>, std::pair<int,int>, pairhash> map;

	ReMapping(){clear();}
	void clear() {map.clear(); bMapped = false;}
	bool isMapped() {return bMapped && !(map.size() == 0);}
	std::pair<int,int> get(std::pair<int,int> dstPos) {
		return map[dstPos];
	}
	void set(std::pair<int,int>srcPos, std::pair<int,int>dstPos) {
		bMapped = true;
		map[dstPos] = srcPos;
	}

	bool reMap(Mat &srcImage, Mat &dstImage) {
		if (!isMapped()) return false;
		for (auto it=map.begin(); it!=map.end(); ++it) {
			int i,j,i_dst,j_dst;
			i_dst = (it->first).first, j_dst = (it->first).second;
			i = (it->second).first, j = (it->second).second;
			dstImage.at<Vec3b>(i_dst,j_dst) = srcImage.at<Vec3b>(i,j);
		}
		std::cout << "[Message] ReMapping used." << std::endl;
		return true;
	}

	inline std::string getPersistFilename(int cpHash) {
		std::string fname = TEMP_PATH +(std::string)"REMAP";
		char hash[20];
		sprintf(hash, "%x", cpHash);
		fname += (std::string) hash + ".dat";
		return fname;
	}

	bool load(int cpHash) {
		if (isMapped()) return true;
		try {
			FILE *fpSrc = NULL;
			int a,b,c,d;
			if ((fpSrc = fopen(getPersistFilename(cpHash).c_str(), "r")) == NULL) {
				std::cout << "[Warning] Load ReMapping cannot be found." << std::endl;
				return false;
			}
			map.clear();
			while (fscanf(fpSrc, "%d%d%d%d", &a, &b, &c, &d)!=EOF) {
				map[std::make_pair(a,b)] = std::make_pair(c,d);
			}
			bMapped = true;
			std::cout << "[Message] Successfully Load ReMapping data." << std::endl;
			fclose(fpSrc);
			return true;
		} catch(...) {
			std::cout << "[Error] Load ReMapping data UNKNOWN error." << std::endl;
			return false;
		}
	}

	void persist(int cpHash) {
		assert(isMapped());
		try {
			FILE *fpDst;
			if ((fpDst = fopen(getPersistFilename(cpHash).c_str(), "w+")) == NULL) {
				std::cout << "[Warning] Persist ReMapping cannot be found." << std::endl;
				return;
			}
			for (auto it=map.begin(); it!=map.end(); ++it) {
				fprintf(fpDst,"%d %d %d %d ", (it->first).first, (it->first).second, (it->second).first,(it->second).second);
			}
			fclose(fpDst);
		} catch(...) {
			std::cout << "[Error] Persist ReMapping data UNKNOWN error." << std::endl;
			return;
		}
	}


};

class CorrectingUtil {
private:
	#define camFieldAngle PI

	ReMapping pixelReMapping;
	CorrectingParams _cParams;
	void basicCorrecting(Mat &src, Mat &dst, CorrectingType ctype);
	void LLMCorrecting(Mat &src, Mat &dst, Point2i center, int radius, CorrectingType ctype);
	void PLLMCLMCorrentingForward(Mat &src, Mat &dst, Point2i center, int radius, DistanceMappingType dmtype);
	void PLLMCLMCorrentingReversed(
		Mat &src, Mat &dst, Point2i center, int radius, DistanceMappingType dmtype);	// w = PI/2
	void LLMCLMUFCorrecting(Mat &src, Mat &dst, Point2i center, int radius, DistanceMappingType dmtype);	// w unfixed
	// helper function
	double getPhiFromV(double v);
	void rotateEarth(double &x, double &y, double &z);
public:
	CorrectingUtil(){pixelReMapping = ReMapping();};
	~CorrectingUtil(){};
	void doCorrect(Mat &srcImage, Mat &dstImage, CorrectingParams cParams = CorrectingParams());
};
