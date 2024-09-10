// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "GameTimer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

GameTimer::GameTimer()
{
	LARGE_INTEGER countsPerSec;
	QueryPerformanceFrequency(&countsPerSec);
	mSecondsPerCount = 1.0 / (double)countsPerSec.QuadPart;
}

// Returns the total time elapsed since Reset() was called, NOT counting any
// time when the clock is stopped.
float GameTimer::TotalTime()const
{
	// If we are stopped, do not count the time that has passed since we stopped.
	// Moreover, if we previously already had a pause, the distance 
	// mStopTime - mBaseTime includes paused time, which we do not want to count.
	// To correct this, we can subtract the paused time from mStopTime:  
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mStopTime    mCurrTime

	if( mStopped )
	{
		return (float)(((mStopTime - mPausedTime)-mBaseTime)*mSecondsPerCount);
	}

	// The distance mCurrTime - mBaseTime includes paused time,
	// which we do not want to count.  To correct this, we can subtract 
	// the paused time from mCurrTime:  
	//
	//  (mCurrTime - mPausedTime) - mBaseTime 
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mCurrTime
	
	else
	{
		return (float)(((mCurrTime-mPausedTime)-mBaseTime)*mSecondsPerCount);
	}
}

float GameTimer::DeltaTime()const
{
	return (float)mDeltaTime;
}

void GameTimer::Reset()
{
	LARGE_INTEGER currTime;
	QueryPerformanceCounter(&currTime);

	mBaseTime = currTime.QuadPart;
	mPrevTime = currTime.QuadPart;
	mStopTime = 0;
	mStopped  = false;
}

void GameTimer::Start()
{
	// Accumulate the time elapsed between stop and start pairs.
	//
	//                     |<-------d------->|
	// ----*---------------*-----------------*------------> time
	//  mBaseTime       mStopTime        startTime     

	if( mStopped )
	{
		LARGE_INTEGER startTime;
		QueryPerformanceCounter(&startTime);
		mPausedTime += (startTime.QuadPart - mStopTime);	

		mPrevTime = startTime.QuadPart;
		mStopTime = 0;
		mStopped  = false;
	}
}

void GameTimer::Stop()
{
	if( !mStopped )
	{
		LARGE_INTEGER currTime;
		QueryPerformanceCounter(&currTime);

		mStopTime = currTime.QuadPart;
		mStopped  = true;
	}
}

void GameTimer::Tick()
{
	if( mStopped )
	{
		mDeltaTime = 0.0;
		return;
	}

	LARGE_INTEGER currTime;
	QueryPerformanceCounter(&currTime);
	mCurrTime = currTime.QuadPart;

	// Time difference between this frame and the previous.
	mDeltaTime = (mCurrTime - mPrevTime)*mSecondsPerCount;

	// Prepare for next frame.
	mPrevTime = mCurrTime;

	// Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
	// processor goes into a power save mode or we get shuffled to another
	// processor, then mDeltaTime can be negative.
	if(mDeltaTime < 0.0)
	{
		mDeltaTime = 0.0;
	}
}
