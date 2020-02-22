#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "V4l2DevFramedSource.h"

V4l2DevFramedSource::V4l2DevFramedSource(UsageEnvironment& env, const char* dev,
                                        int width, int height, int fps, int bitrate, uint32_t pixelFmt) :
    DevFramedSource(env, dev),
    mWidth(width),
    mHeight(height),
    mPixelFmt(pixelFmt),
    mFps(fps),
    mBitrate(bitrate)
{
    bool ret;
    ret = videoInit();
    //assert(ret == true);
    if(ret < 0) videoExit();
}

V4l2DevFramedSource::~V4l2DevFramedSource()
{
    videoExit();
}

bool V4l2DevFramedSource::createFrame(Frame* frame)
{
    bool ret;
    ret = getFrame(frame);//如果队列中还有帧，直接取出返回
    if(ret == true)
        return true;
	
    v4l2_poll(mFd);

    mV4l2BufUnit = v4l2_dqbuf(mFd, mV4l2Buf);//从缓存队列读取数据帧

    ret = encode(mV4l2BufUnit, frame);//编码后 存入frame
    
    v4l2_qbuf(mFd, mV4l2BufUnit);//从摄像头继续采集数据帧，存入缓存队列

    return ret;
}

bool V4l2DevFramedSource::videoInit()
{
	printf("[V4l2DevFramedSource]:videoInit\n");
    int ret;
    char devName[100];
    struct v4l2_capability cap;

    mFd = v4l2_open(mDev.c_str(), O_RDWR | O_NONBLOCK);////camera 打开由阻塞打开改为了非阻塞方式打开
    if(mFd < 0)
        return false;

    ret = v4l2_querycap(mFd, &cap);
    if(ret < 0)
        return false;

    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
        return false;
    
    ret = v4l2_enuminput(mFd, 0, devName);
    if(ret < 0)
        return false;

    ret = v4l2_s_input(mFd, 0);
    if(ret < 0)
        return false;
    
    ret = v4l2_enum_fmt(mFd, mPixelFmt, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    if(ret < 0)
        return false;
        
    
    ret = v4l2_s_fmt(mFd, &mWidth, &mHeight, mPixelFmt, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    if(ret < 0)
        return false;
        
	//添加h264编码profile控制：
	ret = v4l2_s_profile(mFd, 1);//参数未设置   固定V4L2_MPEG_VIDEO_H264_PROFILE_MAIN
	if(ret < 0)		printf("v4l2_s_profile fail\n");
	//添加h264编码level控制：失败
	//ret = v4l2_s_level(mFd, 1); //参数未设置   固定V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
	//if(ret < 0)		printf("v4l2_s_level fail\n");
	//添加h264编码速率控制：
	if(mBitrate > 10240){
		printf("mBitrate too large：%d\n",mBitrate);
		return false;
	}
	ret = v4l2_s_bitrate(mFd, mBitrate);
	if(ret < 0)		printf("v4l2_s_bitrate fail\n");
	//添加h264编码关键帧间隔控制：
	ret = v4l2_s_frameinterval(mFd, mFps); 
	if(ret < 0)		printf("v4l2_s_frameinterval fail\n");  
	//添加h264编码sps/pps控制：失败
	//ret = v4l2_s_sps(mFd, 1); 
	//if(ret < 0)		printf("v4l2_s_sps fail\n"); 
	//添加fps设置
	ret = v4l2_s_fps(mFd, mFps);
	if(ret < 0)		printf("v4l2_s_fps fail\n");
	    
    mV4l2Buf = v4l2_reqbufs(mFd, V4L2_BUF_TYPE_VIDEO_CAPTURE, 4);
    if(!mV4l2Buf)
        return false;
    
    ret = v4l2_querybuf(mFd, mV4l2Buf);
    if(ret < 0)
        return false;
    
    ret = v4l2_mmap(mFd, mV4l2Buf);
    if(ret < 0)
        return false;
    
    ret = v4l2_qbuf_all(mFd, mV4l2Buf);
    if(ret < 0)
        return false;

    ret = v4l2_streamon(mFd);
    if(ret < 0)
        return false;
    
    ret = v4l2_poll(mFd);
    if(ret < 0)
        return false;
    
    return true;
}

bool V4l2DevFramedSource::videoExit()
{
printf("[V4l2DevFramedSource]:videoExit\n");
    int ret;

    ret = v4l2_streamoff(mFd);
    if(ret < 0)
        return false;

    ret = v4l2_munmap(mFd, mV4l2Buf);
    if(ret < 0)
        return false;

    ret = v4l2_relbufs(mV4l2Buf);
    if(ret < 0)
        return false;

    v4l2_close(mFd);

    return true;
}
