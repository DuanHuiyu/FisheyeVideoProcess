#include "Processor.h"
#include "CorrectingUtil.h"

Processor::Processor() {
	correctingUtil = CorrectingUtil();
	stitchingUtil = StitchingUtil();
}

Processor::~Processor() {
}

void Processor::findFisheyeCircleRegion() {
	// TOSOLVE: Simplest estimation,
	// but in fact circle region may move slightly from time to time
	radiusOfCircle = (int)round(vCapture[0].get(CV_CAP_PROP_FRAME_HEIGHT)/2);
	centerOfCircleBeforeResz.y = radiusOfCircle;
	centerOfCircleBeforeResz.x = (int)round(vCapture[0].get(CV_CAP_PROP_FRAME_WIDTH)/2);
}

void Processor::setPaths(std::string inputPaths[], int inputCnt, std::string outputPath) {
	for (int i=0; i<inputCnt; ++i) vCapture[i].open(inputPaths[i]);
	assert(camCnt == inputCnt);
	
	// Currently assumes every len has the same situation
	// that the height(col) of video frame indicates d of circle region
	findFisheyeCircleRegion();
	
	vWriter = VideoWriter(
		outputPath, CV_FOURCC('D', 'I', 'V', 'X'),
		vCapture[0].get(CV_CAP_PROP_FPS), Size(radiusOfCircle*4,radiusOfCircle));
}

void Processor::fisheyeShirnk(Mat &frm) {
	Mat tmpFrm = frm.clone();
	const int param1 = 200, param2 = 235;

	int col, row;
	int i, j;	//�С���ѭ������
	int u0, v0;	//Բ��������
	int u, v;	//Բ����ϵ����
	int u_235, v_235;
	int R0;	//Բ�뾶
	double r;	//��ʱСԲ�뾶
	double R;	//��ʱ��Բ�뾶
	int i_235, j_235;	//ӳ�䵽235��Բ�ڶ�Ӧ�ĵ������
	double alpha;

	col = tmpFrm.cols; //������x��
	row = tmpFrm.rows; //������y��
	u0 = round(col / 2);
	v0 = round(row / 2);	//����
	R0 = col - u0;	//���ΰ뾶

	for (i = 0; i < row; i++) {
		for (j = 0; j < col; j++) {
			u = j - u0;		//������,ԭ����Բ��
			v = v0 - i;		//������,ԭ����Բ��
			R = sqrt(pow(u, 2) + pow(v, 2));	//��Բ�ĵľ���
			if (R > R0)	{	//�����߽�
				continue;
			}
			r = R * param1 / param2;
			//��alpha��
			if (r == 0) {
				alpha = 0;
			}
			else if (u > 0)	{
				alpha = asin(v / R);	//��atan2(v,u) or asin(v/r)���ԣ���atan(v/u)����
			}
			else if (u < 0)	{
				alpha = M_PI - asin(v / R);
			}
			else if (u == 0 && v>0) {
				alpha = M_PI / 2;
			}
			else
				alpha = 3 * M_PI / 2;

			//��ӳ�䵽Բ�ϵĵ�
			u_235 = round(r*cos(alpha));
			v_235 = round(r*sin(alpha));
			//����ת������������
			i_235 = v0 - v_235;
			j_235 = u_235 + u0;

			frm.at<Vec3b>(i, j)[0] = tmpFrm.at<Vec3b>(i_235, j_235)[0];
			frm.at<Vec3b>(i, j)[1] = tmpFrm.at<Vec3b>(i_235, j_235)[1];
			frm.at<Vec3b>(i, j)[2] = tmpFrm.at<Vec3b>(i_235, j_235)[2];
		}
	}

}

void Processor::fisheyeCorrect(Mat &src, Mat &dst) {
	//TODO: To apply different type of correction
	correctingUtil.doCorrect(src, dst, 
		CorrectingParams(
			PERSPECTIVE_LONG_LAT_MAPPING_CAM_LENS_MOD_REVERSED,
			centerOfCircleAfterResz,
			radiusOfCircle,
			LONG_LAT));
}

void Processor::panoStitch(std::vector<Mat> &srcs, Mat &dst) {
	stitchingUtil.doStitch(srcs, dst, StitchingPolicy::DIRECT, StitchingType::OPENCV_DEFAULT);
}

void Processor::process() {
	Mat srcFrms[camCnt];
	Mat dstFrms[camCnt];

	int fIndex = 0;
	while(1) {
		// frame by frame
		++fIndex;
		std::cout << ">>>>> Processing No." << fIndex << " frame..." <<std::endl;
		Mat tmpFrms[camCnt];
		for (int i=0; i<camCnt; ++i) {
			vCapture[i] >> tmpFrms[i];
			if (tmpFrms[i].empty()) break;
		
			/* Resize to square frame */
			srcFrms[i] = tmpFrms[i](
				/* row */
				Range(centerOfCircleBeforeResz.y-radiusOfCircle, centerOfCircleBeforeResz.y+radiusOfCircle),
				/* col */
				Range(centerOfCircleBeforeResz.x-radiusOfCircle, centerOfCircleBeforeResz.x+radiusOfCircle))
				.clone();	// must use clone()
			
			//TOSOLVE dst.size() set to 0.9 of src.size() ?
			dstFrms[i].create(srcFrms[i].rows, srcFrms[i].cols, srcFrms[i].type());
		}

		// Hardcode: Use 1st to set centerOfCircleAfterResz
		if (fIndex == 1) {
			centerOfCircleAfterResz.x = srcFrms[0].cols/2;
			centerOfCircleAfterResz.y = srcFrms[0].rows/2;
		}

		for (int i=0; i<camCnt; ++i) {
			// fisheyeShirnk(srcFrms[i]); // No need, confirmed.
			fisheyeCorrect(srcFrms[i], dstFrms[i]);
		}
		

	}

}