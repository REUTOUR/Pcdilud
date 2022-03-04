#pragma once
#include "AutoMutex.h"
#include "AutomatedLock.h"

#define DRIVER_PREFIX "PcdIlud: "
#define DRIVER_TAG 'nmys'

enum class ItemType : short
{
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

struct ItemHeader
{
	ItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
};

struct ProcessExitInfo : ItemHeader
{
	ULONG ProcessId;
};

struct ProcessCreateInfo : ItemHeader
{
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
};

struct ThreadCreateExitInfo : ItemHeader
{
	ULONG ThreadId;
	ULONG ProcessId;
};

const int MaxImageFileSize = 500;

struct ImageLoadInfo : ItemHeader
{
	ULONG ProcessId;
	void* LoadAddress;
	ULONG_PTR ImageSize;
	WCHAR ImageFileName[MaxImageFileSize + 1];
};

class Global
{
	public: 
		LIST_ENTRY ItemsHead;
		int ItemCount;
		AutoMutex Mutex;
};

template<typename T> class FullItem
{
	public: 
		LIST_ENTRY Entry;
		T Data;
};

DRIVER_UNLOAD PcdiludUnload;
DRIVER_DISPATCH ObjectCreateClose, SendToUm;
void OnProcessNotify(PEPROCESS process, HANDLE processId, PPS_CREATE_NOTIFY_INFO createInfo);
void OnImageLoadNotify(PUNICODE_STRING fullImageName, HANDLE processId, PIMAGE_INFO imageInfo);
void PushItem(LIST_ENTRY* entry);
