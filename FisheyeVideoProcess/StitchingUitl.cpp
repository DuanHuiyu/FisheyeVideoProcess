#include "StitchingUtil.h"
#include <opencv2\features2d\features2d.hpp>
#include <opencv2\nonfree\features2d.hpp>
#include <opencv2\nonfree\nonfree.hpp>

using namespace cv::detail;

void StitchingUtil::matchWithBRISK(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	static const int kFlannMaxDistScale = 3;
	static const double kFlannMaxDistThreshold = 0.04;

	std::vector<KeyPoint> kptsL, kptsR;
	Mat descL, descR;
	std::vector<DMatch> goodMatches;

#ifdef OPENCV_3
	Ptr<BRISK> brisk = BRISK::create();
	brisk->detectAndCompute(imageL, noArray(), kptsL, descL);
	brisk->detectAndCompute(imageR, noArray(), kptsR, descR);
#else
	//TOSOLVE: 2.4.9-style incovation, not sure it works
	const int Thresh = 60, Octave = 4;
	const float PatternScales = 1.0f;
	cv::BRISK briskDetector(Thresh, Octave, PatternScales);
	briskDetector.create("Feature2D.BRISK");
	briskDetector.detect(left, kptsL);
	briskDetector.compute(left, kptsL, descL);
	briskDetector.detect(right, kptsR);
	briskDetector.compute(right, kptsR, descR);
#endif

	descL.convertTo(descL, CV_32F);
	descR.convertTo(descR, CV_32F);

	static const int kFlannNumTrees = 4;
	FlannBasedMatcher matcher(new flann::KDTreeIndexParams(kFlannNumTrees));
	vector<DMatch> flannMatches;
	matcher.match(descL, descR, flannMatches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max();
	for(int i = 0; i < flannMatches.size(); ++i) {
		double dist = flannMatches[i].distance;
		maxDist = max(maxDist, dist);
		minDist = min(minDist, dist);
	}

	for(int i = 0; i < flannMatches.size(); ++i) {
		double distThresh = kFlannMaxDistScale * minDist;
		if (flannMatches[i].distance <= max(distThresh, kFlannMaxDistThreshold)) {
			goodMatches.push_back(flannMatches[i]);
		}
	}

	for (const DMatch& match : goodMatches) {
		const Point2f& kptL = kptsL[match.queryIdx].pt;
		const Point2f& kptR = kptsR[match.trainIdx].pt;
		matchedPair.push_back(std::make_pair(kptL, kptR));
	}

}
void StitchingUtil::matchWithORB(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	static const bool kUseGPU = false;
	static const float kMatchConfidence = 0.4;

	OrbFeaturesFinder finder;

	ImageFeatures imgFeaturesL;
	finder(left, imgFeaturesL);
	imgFeaturesL.img_idx = 0;

	ImageFeatures imgFeaturesR;
	finder(right, imgFeaturesR);
	imgFeaturesR.img_idx = 1;

	vector<ImageFeatures> features;
	features.push_back(imgFeaturesL);
	features.push_back(imgFeaturesR);
	vector<MatchesInfo> pairwiseMatches;
	BestOf2NearestMatcher matcher(kUseGPU, kMatchConfidence);
	matcher(features, pairwiseMatches);

	for (const MatchesInfo& matchInfo : pairwiseMatches) {
		if (matchInfo.src_img_idx != 0 || matchInfo.dst_img_idx != 1) {
			continue;
		}
		for (const DMatch& match : matchInfo.matches) {
			const Point2f& kptL = imgFeaturesL.keypoints[match.queryIdx].pt;
			const Point2f& kptR = imgFeaturesR.keypoints[match.trainIdx].pt;
			matchedPair.push_back(std::make_pair(kptL, kptR));
		}
	}
}

#ifdef OPENCV_3	/* AKAZE Required OPENCV 3.0+ */
void StitchingUtil::matchWithAKAZE(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	static const int kFlannMaxDistScale = 3;
	static const double kFlannMaxDistThreshold = 0.04;

	Mat descL, descR;
	vector<KeyPoint> kptsL, kptsR;
	vector<DMatch> goodMatches;

	Ptr<AKAZE> akaze = AKAZE::create();
	akaze->detectAndCompute(left, noArray(), kptsL, descL);
	akaze->detectAndCompute(right, noArray(), kptsR, descR);

	// FlannBasedMatcher with KD-Trees needs CV_32
	descL.convertTo(descL, CV_32F);
	descR.convertTo(descR, CV_32F);

	// KD-Tree param: # of parallel kd-trees
	static const int kFlannNumTrees = 4;
	FlannBasedMatcher matcher(new flann::KDTreeIndexParams(kFlannNumTrees));
	vector<DMatch> flannMatches;
	matcher.match(descL, descR, flannMatches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max() ;
	for(int i = 0; i < flannMatches.size(); ++i) {
		double dist = flannMatches[i].distance;
		maxDist = max(maxDist, dist);
		minDist = min(minDist, dist);
	}

	for(int i = 0; i < flannMatches.size(); ++i) {
		double distThresh = kFlannMaxDistScale * minDist;
		if (flannMatches[i].distance <= max(distThresh, kFlannMaxDistThreshold)) {
			goodMatches.push_back(flannMatches[i]);
		}
	}

	for (const DMatch& match : goodMatches) {
		const Point2f& kptL = kptsL[match.queryIdx].pt;
		const Point2f& kptR = kptsR[match.trainIdx].pt;
		matchedPair.push_back(std::make_pair(kptL, kptR));
	}

}
#endif

void StitchingUtil::facebookKeyPointMatching(Mat &left, Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	// input Mat must be grayscale
	assert(left.channels() == 1);
	assert(right.channels() == 1);

	vector<std::pair<Point2f, Point2f>> matchPointPairsLRAll;
	matchWithBRISK(left, right, matchPointPairsLRAll);
	matchWithORB(left, right, matchPointPairsLRAll);
#ifdef OPENCV_3
	matchWithAKAZE(left, right, matchPointPairsLRAll);
#endif


	// Remove duplicate keypoints
	sort(matchPointPairsLRAll.begin(), matchPointPairsLRAll.end());
	matchPointPairsLRAll.erase(
		std::unique(matchPointPairsLRAll.begin(), matchPointPairsLRAll.end()),
		matchPointPairsLRAll.end());

	// Apply RANSAC to filter weak matches (like, really weak)
	vector<Point2f> matchesL, matchesR;
	vector<uchar> inlinersMask;
	for(int i = 0; i < matchPointPairsLRAll.size(); ++i) {
		matchesL.push_back(matchPointPairsLRAll[i].first);
		matchesR.push_back(matchPointPairsLRAll[i].second);
	}

	static const int kRansacReprojThreshold = 100;
	findHomography(
		matchesL,
		matchesR,
		CV_RANSAC,
		kRansacReprojThreshold,
		inlinersMask);

	for (int i = 0; i < inlinersMask.size(); ++i) {
		if (inlinersMask[i]) {
			matchedPair.push_back(std::make_pair(matchesL[i], matchesR[i]));
		}
	}
}

void StitchingUtil::selfKeyPointMatching(Mat &left, Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair, StitchingType sType) {
	// input Mat must be grayscale
	assert(left.channels() == 1);
	assert(right.channels() == 1);

	const int minHessian = 600;

	SurfFeatureDetector detector(minHessian);
	std::vector<KeyPoint> kptL, kptR;
	detector.detect(left, kptL);
	detector.detect(right, kptR);

	Mat desL, desR;
	SurfDescriptorExtractor extractor;
	extractor.compute(left, kptL, desL);
	extractor.compute(right, kptR, desR);

	FlannBasedMatcher matcher;
	std::vector<DMatch> matches;
	matcher.match(desL, desR, matches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max();
	for (int i=0, double dis; i<desL.rows; ++i) {
		dis = matches[i].distance;
		minDist = min(dis, minDist);
		maxDist = max(dis, maxDist);
	}

	// USe only Good macthes (distance less than 3*minDist)
	std::vector<DMatch> goodMatches;
	for (int i=0; i<desL.rows; ++i) {
		if (matches[i].distance < 3*minDist)
			goodMatches.push_back(matches[i]);
	}
	
	for (int i=0; i<goodMatches.size(); ++i) {
		matchedPair.push_back(std::make_pair(kptL[goodMatches[i].queryIdx].pt, kptR[goodMatches[i].trainIdx].pt));
	}
}

void StitchingUtil::selfStitchingSAfterMatching(
	Mat &left, Mat &right, Mat &leftOri, Mat &rightOri, std::vector<std::pair<Point2f, Point2f>> &matchedPair, Mat &dstImage) {
	// input mat should be colored ones
	std::vector<Point2f> matchedL, matchedR;
	unzipMatchedPair(matchedPair, matchedL, matchedR);

	Mat H = findHomography(left, right, CV_RANSAC);
	warpPerspective(leftOri, dstImage, H, Size(leftOri.cols+rightOri.cols, leftOri.rows));
	Mat half(dstImage, Rect(0,0,rightOri.cols,rightOri.rows));
	rightOri.copyTo(half);

	// TOSOLVE: how to add blend manually
}

Stitcher StitchingUtil::opencvStitcherBuild(StitchingType sType) {
	Stitcher s = Stitcher::createDefault(0);
	if (sType == OPENCV_DEFAULT) return s;
	s.setRegistrationResol(0.3);					//0.6 by default, smaller the faster
	s.setPanoConfidenceThresh(1);					//1 by default, 0.6 or 0.4 worth a try
	s.setWaveCorrection(false);						//true by default, set false for acceleration
	const bool useORB = false;						// ORB is faster but less stable
	s.setFeaturesFinder(useORB ? (FeaturesFinder*)(new OrbFeaturesFinder()) : (FeaturesFinder*)(new SurfFeaturesFinder()));
	s.setFeaturesMatcher(new BestOf2NearestMatcher(false, 0.5f /* 0.65 by default */));
	s.setBundleAdjuster(new BundleAdjusterRay());	// faster
	s.setSeamFinder(new NoSeamFinder);
	s.setExposureCompensator(new NoExposureCompensator);
	s.setBlender(new FeatherBlender);				// multiBandBlender byb default, this is faster
	return s;
}


void StitchingUtil::opencvStitching(std::vector<Mat> &srcs, Mat &dstImage, StitchingType sType) {
	assert(sType <= OPENCV_TUNED);
	static Stitcher s = opencvStitcherBuild(sType);
	Stitcher::Status status;
	switch (sType) {
	case OPENCV_DEFAULT:
		status = s.stitch(srcs, dstImage);
		if (Stitcher::OK != status) {
			std::cout << "Cannot stitch the image, errCode = " << status << std::endl;
			assert(false);
		}
		break;
	case OPENCV_TUNED:
		status = s.estimateTransform(srcs);
		if (Stitcher::OK != status) {
			std::cout << "Cannot stitch the image, error in estimateTranform, errCode = " << status << std::endl;
		}
		status = s.composePanorama(dstImage);
				if (Stitcher::OK != status) {
			std::cout << "Cannot stitch the image, error in composePanorama, errCode = " << status << std::endl;
		}
		break;
	default:
		assert(false);
	}
}


void StitchingUtil::unzipMatchedPair(
	std::vector<std::pair<Point2f, Point2f>> &matchedPair, std::vector<Point2f> &matchedL, std::vector<Point2f> &matchedR) {
		matchedL.clear(); matchedR.clear();
		for (int i=0; i<matchedPair.size(); ++i) {
			matchedL.push_back(matchedPair[i].first);
			matchedR.push_back(matchedPair[i].second);
}
