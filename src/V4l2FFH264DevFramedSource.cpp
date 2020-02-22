#include <assert.h>

#include "V4l2FFH264DevFramedSource.h"

V4l2FFH264DevFramedSource* V4l2FFH264DevFramedSource::createNew(UsageEnvironment& env, const char* dev,
                                                            int width, int height, int fps, int bitrate)
{
	printf("V4l2FFH264DevFramedSource::createNew\n");
    return new V4l2FFH264DevFramedSource(env, dev, width, height, fps, bitrate);
}

V4l2FFH264DevFramedSource::V4l2FFH264DevFramedSource(UsageEnvironment& env, const char* dev,
                                                int width, int height, int fps, int bitrate) :
    V4l2DevFramedSource(env, dev, width, height,fps, bitrate, V4L2_PIX_FMT_H264),//添加了V4L2_PIX_FMT_H264
    mPts(0)
{
    bool ret;
    setFps(fps);
	num=0;
	//H264data = (uint8_t *)malloc(1024*40);
    mSPSFrame = new Frame(100);
    mPPSFrame = new Frame(100);
}

V4l2FFH264DevFramedSource::~V4l2FFH264DevFramedSource()
{

	num=0;
    delete mSPSFrame;
    delete mPPSFrame;
}

bool V4l2FFH264DevFramedSource::getFrame(Frame* frame)//取多余的帧，一般都是pps或idr帧
{

    if(mNaluQueue.empty())
        return false;
    
    Nalu nalu = mNaluQueue.front();
    mNaluQueue.pop();
    memcpy(frame->mFrame, nalu.mData, nalu.mSize);
    frame->mTime = nalu.mTime;
    frame->mDurationInMicroseconds = nalu.mDurationInMicroseconds;
    frame->mFrameSize = nalu.mSize;
	nalu.mData = NULL;//添加后内存没有极速增加
    return true;
}

static inline int startCode3(uint8_t* buf)
{
    if(buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

static inline int startCode4(uint8_t* buf)
{
    if(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}

static inline int find_sps(uint8_t* buf,int len)
{	
	int temp_startCode;
	if(startCode3(buf))            temp_startCode = 3;
    if(startCode4(buf))            temp_startCode = 4;
    if(temp_startCode==0) return 0;
    for(int i=temp_startCode;i<len-temp_startCode;i++)
		if(buf[i] == 0 && buf[i+1] == 0 && buf[i+2] ==0 && buf[i+3]== 1) return i;	
	return 0;
}

bool V4l2FFH264DevFramedSource::encode(struct v4l2_buf_unit* v4l2BufUnit, Frame* frame)
{
	int  startCode;
	timeval time;
	uint8_t byte;

	if(v4l2BufUnit->bytesused==0) return false;
	//printf("v4l2H264BufUnit->length=%d bytesused=%d\n",v4l2BufUnit->length,v4l2BufUnit->bytesused);
	uint8_t *H264data = (uint8_t *)malloc(v4l2BufUnit->bytesused);
	memcpy(H264data,v4l2BufUnit->start,v4l2BufUnit->bytesused);//H264data设置为成员变量，初始化在构造函数中，释放在析构
	
	if(startCode3((uint8_t *)v4l2BufUnit->start))      startCode = 3;
	else                          startCode = 4;
	
	//if(first){ gettimeofday(&first_time, NULL);first=false;}//开始计时
	if(num==0) {gettimeofday(&first_time, NULL);printf("start timer\n");}//开始计时
	gettimeofday(&time, NULL);//获取当前时间
	
	byte = *((uint8_t *)v4l2BufUnit->start+startCode);
    if((byte&0x1F) == 7) //sps
    {
		printf("sps\n");
		mSPSFrame->mFrameSize = find_sps(H264data,v4l2BufUnit->bytesused)-startCode;
		mSPSFrame->mDurationInMicroseconds = 0;
		mSPSFrame->mTime = time;
		memcpy(mSPSFrame->mFrame, H264data+startCode, mSPSFrame->mFrameSize);
		mPPSFrame->mFrameSize = v4l2BufUnit->bytesused-mSPSFrame->mFrameSize-2*startCode;
		mPPSFrame->mDurationInMicroseconds = 0;
		mPPSFrame->mTime = time;
		memcpy(mPPSFrame->mFrame, H264data+2*startCode+mSPSFrame->mFrameSize, mPPSFrame->mFrameSize);

		/*//打印sps和pps信息
		for(int i=0;i<mSPSFrame->mFrameSize;i++)
			printf("%02x ",mSPSFrame->mFrame[i]);
		printf("\n");
		for(int i=0;i<mPPSFrame->mFrameSize;i++)
			printf("%02x ",mPPSFrame->mFrame[i]);
		printf("\n");
		*/
	}else //关键帧和其他帧
        {
			
            if((byte&0x1F) == 5)//如果是关键帧，把sps pps存入队列
            {
				mNaluQueue.push(Nalu(mSPSFrame->mFrame, mSPSFrame->mFrameSize, time, 0));
				mNaluQueue.push(Nalu(mPPSFrame->mFrame, mPPSFrame->mFrameSize, time, 0));				
				//printf("--------------keyframe---------len=[%d]----startCode=[%d]----\n",v4l2BufUnit->bytesused,startCode);
            }
            
            //把帧存入队列
			mNaluQueue.push(Nalu(H264data+startCode, v4l2BufUnit->bytesused-startCode, time, 1000000/fps()));
        }
    num++;
    if(!(num % 30)){//统计帧率
		gettimeofday(&second_time, NULL);
		double time_val = (second_time.tv_sec - first_time.tv_sec) * 1000000 + second_time.tv_usec - first_time.tv_usec;
		printf("-encode %d (size=%6d) time(ms) = %lf fps=%lf\n", num, v4l2BufUnit->bytesused,time_val/1000.0, num*1000.0/(time_val/1000.0));
	}

        //下面是取出一帧H264数据，如果队列中有多余的数据，先使用getFrame先取出，详见V4l2DevFramedSource::createFrame()方法
        if(mNaluQueue.empty())        return false;
		Nalu nalu = mNaluQueue.front();
		mNaluQueue.pop();
		memcpy(frame->mFrame, nalu.mData, nalu.mSize);
		frame->mFrameSize = nalu.mSize;
		frame->mTime = nalu.mTime;
		frame->mDurationInMicroseconds = nalu.mDurationInMicroseconds;
		nalu.mData = NULL;//添加后内存没有极速增加
		
		free(H264data);
		return true;

}


