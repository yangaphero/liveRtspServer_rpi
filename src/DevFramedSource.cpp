#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "DevFramedSource.h"

DevFramedSource::DevFramedSource(UsageEnvironment& env, const char* dev, int frameSize) :
    FramedSource(env),
    mDev(dev),
    mFps(20)
{
    for(int i = 0; i < sizeof(mFrameArray)/sizeof(mFrameArray[0]); ++i)
    {
        mFrameArray[i] = new Frame(frameSize);
        mInputQueue.push(mFrameArray[i]);
    }

    pthread_mutex_init(&mMutex, NULL);
    pthread_cond_init(&mCond, NULL);

    mTaskToken = envir().taskScheduler().scheduleDelayedTask(0, startCreateFrame, this);//开启线程createFrame
}

DevFramedSource::~DevFramedSource()
{
    envir().taskScheduler().unscheduleDelayedTask(mTaskToken);
    envir().taskScheduler().unscheduleDelayedTask(mNextTaskToken);

	pthread_mutex_destroy(&mMutex);
	pthread_cond_destroy(&mCond);

    for(int i = 0; i < sizeof(mFrameArray)/sizeof(mFrameArray[0]); ++i)
        delete mFrameArray[i];
}

void DevFramedSource::startCreateFrame(void* data)
{
    DevFramedSource* source = (DevFramedSource*)data;
    source->startCreateFrame();//开启线程threadFunc，
}

void DevFramedSource::startCreateFrame()
{
    mThreadRun = true;
    pthread_create(&mThreadId, NULL, threadFunc, this);//开启线程createFrame
}

void DevFramedSource::afterGetNextFrame(void* data)
{
    DevFramedSource* source = (DevFramedSource*)data;
    source->doGetNextFrame();
}

void DevFramedSource::doGetNextFrame()
{
    pthread_mutex_lock(&mMutex);

    if(mOutputQueue.empty())
    {
        pthread_mutex_unlock(&mMutex);
		//循环10ms调用afterGetNextFrame，再doGetNextFrame();
        mNextTaskToken = envir().taskScheduler().scheduleDelayedTask(5*1000, afterGetNextFrame, this);//10ms,单位是微秒
        return;
    }

    mNextTaskToken = 0;
    Frame* frame = mOutputQueue.front();
    mOutputQueue.pop();

    pthread_mutex_unlock(&mMutex);

    if(frame->mFrameSize > fMaxSize)
    {
        fFrameSize = fMaxSize;
        fNumTruncatedBytes = frame->mFrameSize - fMaxSize;
    }
    else
    {
        fFrameSize = frame->mFrameSize;
        fNumTruncatedBytes = 0;
    }

    fPresentationTime = frame->mTime;
    fDurationInMicroseconds = frame->mDurationInMicroseconds;
	
    memcpy(fTo, frame->mFrame, fFrameSize);//fTo就是OutPacketBuffer中的缓冲区)

    pthread_mutex_lock(&mMutex);
    mInputQueue.push(frame);
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    
    FramedSource::afterGetting(this);
}

void DevFramedSource::doStopGettingFrames()
{
    stopCreateFrame();
}

void* DevFramedSource::threadFunc(void* data)
{
    DevFramedSource* source = (DevFramedSource*)data;
    source->createFrame();//调用对象的asla或v4l2的createFrame(frame)

    return NULL;
}

void DevFramedSource::stopCreateFrame()
{
    if(mThreadRun == false)
        return;

    mThreadRun = false;
    pthread_cond_broadcast(&mCond);
    pthread_join(mThreadId, NULL);
}

void DevFramedSource::createFrame()
{
    while(mThreadRun == true)
    {
        pthread_mutex_lock(&mMutex);

        if(mInputQueue.empty())
        {
            pthread_cond_wait(&mCond, &mMutex);
            if(mThreadRun == false)
            {
                pthread_mutex_unlock(&mMutex);
                break;
            }
        }

        Frame* frame = mInputQueue.front();//从输入队列取出一帧
        mInputQueue.pop();
        pthread_mutex_unlock(&mMutex);
        while(createFrame(frame) == false);//对应到aac和h264的createFrame---编码后的帧

        pthread_mutex_lock(&mMutex);
        mOutputQueue.push(frame);//编码后的帧存入输出队列
        pthread_mutex_unlock(&mMutex);
    }
}

