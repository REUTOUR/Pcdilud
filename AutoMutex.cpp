#include "AutoMutex.h"

void AutoMutex::Init()
{
	ExInitializeFastMutex(&mutex);
}

void AutoMutex::Lock()
{
	ExAcquireFastMutex(&mutex);
}

void AutoMutex::Unlock()
{
	ExReleaseFastMutex(&mutex);
}
