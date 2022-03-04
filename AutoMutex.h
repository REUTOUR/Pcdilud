#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

class AutoMutex
{
	public:
		void Init();
		void Lock();
		void Unlock();

	private:
		FAST_MUTEX mutex;
};
