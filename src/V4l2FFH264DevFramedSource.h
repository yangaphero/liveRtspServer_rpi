#ifndef _V4L2_H264_DEV_FRAMED_SOURCE_H_
#define _V4L2_H264_DEV_FRAMED_SOURCE_H_
#include <queue>

#include "V4l2DevFramedSource.h"


class V4l2FFH264DevFramedSource : public V4l2DevFramedSource
{
public:
    static V4l2FFH264DevFramedSource* createNew(UsageEnvironment& env, const char* dev,
                                                int width=320, int height=240, int fps=15, int bitrate=1024);

protected:
    struct Nalu
    {
        Nalu(uint8_t* data, uint32_t size, timeval time, uint32_t duration) :
            mData(data), mSize(size), mTime(time), mDurationInMicroseconds(duration)
            {  }

        uint8_t* mData;
        uint32_t mSize;
        timeval mTime;
        uint32_t mDurationInMicroseconds;
    };

    V4l2FFH264DevFramedSource(UsageEnvironment& env, const char* dev,
                            int width, int height, int fps, int bitrate);
    virtual ~V4l2FFH264DevFramedSource();
 
    virtual bool getFrame(Frame* frame);
    virtual bool encode(struct v4l2_buf_unit* v4l2BufUnit, Frame* frame);


	
private:
	
	struct timeval first_time, second_time;//用时间差统计帧速率
	FILE *f1;
	FILE *f2;
    int mPts;
    int num=0;//用时间差统计帧速率
	//uint8_t *H264data;
    Frame* mSPSFrame;
    Frame* mPPSFrame;

    std::queue<Nalu> mNaluQueue;
};

#endif //_V4L2_DEV_FRAMED_SOURCE_H_
