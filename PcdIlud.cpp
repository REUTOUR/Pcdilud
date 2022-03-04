#include "PcdIlud.h"

Global Globals;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) 
{
	auto status = STATUS_SUCCESS;
	InitializeListHead(&Globals.ItemsHead);
	Globals.Mutex.Init();
	PDEVICE_OBJECT deviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\pcdilud");
	bool symLinkCreated = false;
	bool processCallbacks = false;
	do 
	{
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\pcdilud");
		status = IoCreateDevice(driverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &deviceObject);
		if (!NT_SUCCESS(status)) 
		{
			KdPrint((DRIVER_PREFIX "pcdilud init failed (0x%08X)\n", status));
			break;
		}
		deviceObject->Flags |= DO_DIRECT_IO;
		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) 
		{
			KdPrint((DRIVER_PREFIX "create sym link failed (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status)) 
		{
			KdPrint((DRIVER_PREFIX "register process callback failed (0x%08X)\n", status));
			break;
		}
		processCallbacks = true;
		status = PsSetLoadImageNotifyRoutine(OnImageLoadNotify);
		if (!NT_SUCCESS(status)) 
		{
			KdPrint((DRIVER_PREFIX "set image load callback failed (0x%08X)\n", status));
			break;
		}
	} while (false);
	if (!NT_SUCCESS(status)) 
	{
		if (processCallbacks)
		{
			PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
		}
		if (symLinkCreated)
		{
			IoDeleteSymbolicLink(&symLink);
		}
		if (deviceObject)
		{
			IoDeleteDevice(deviceObject);
		}
	}
	driverObject->DriverUnload = PcdiludUnload;
	driverObject->MajorFunction[IRP_MJ_CREATE] = ObjectCreateClose;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = ObjectCreateClose;
	driverObject->MajorFunction[IRP_MJ_READ] = SendToUm;
	return status;
}

NTSTATUS ObjectCreateClose(PDEVICE_OBJECT, PIRP irp) 
{
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, 0);
	return STATUS_SUCCESS;
}

NTSTATUS SendToUm(PDEVICE_OBJECT, PIRP irp) 
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(irp->MdlAddress);
	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
	if (!buffer) 
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else 
	{
		AutomatedLock<AutoMutex> lock(Globals.Mutex);
		while (true) 
		{
			if (IsListEmpty(&Globals.ItemsHead))
			{
				break;
			}
			auto entry = RemoveHeadList(&Globals.ItemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.Size;
			if (len < size) 
			{
				InsertHeadList(&Globals.ItemsHead, entry);
				break;
			}
			Globals.ItemCount--;
			::memcpy(buffer, &info->Data, size);
			len -= size;
			buffer += size;
			count += size;
			ExFreePool(info);
		}
	}
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = count;
	IoCompleteRequest(irp, 0);
	return status;
}

void PcdiludUnload(PDRIVER_OBJECT DriverObject) 
{
	PsRemoveLoadImageNotifyRoutine(OnImageLoadNotify);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\pcdilud");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
	while (!IsListEmpty(&Globals.ItemsHead)) 
	{
		auto entry = RemoveHeadList(&Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	}
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo) 
	{
		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = 0;
		if (CreateInfo->CommandLine) 
		{
			commandLineSize = CreateInfo->CommandLine->Length;
			allocSize += commandLineSize;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (info == nullptr) 
		{
			KdPrint((DRIVER_PREFIX "ExAllocatePoolWithTag failed\r\n"));
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.Size = sizeof(ProcessCreateInfo) + commandLineSize;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		if (commandLineSize > 0) 
		{
			::memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);	// length in WCHARs
			item.CommandLineOffset = sizeof(item);
		}
		else {
			item.CommandLineLength = 0;
		}
		PushItem(&info->Entry);
	}
	else 
	{
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr) 
		{
			KdPrint((DRIVER_PREFIX "ExAllocatePoolWithTag failed\r\n"));
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.Size = sizeof(ProcessExitInfo);
		PushItem(&info->Entry);
	}
}

void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create) {
	auto size = sizeof(FullItem<ThreadCreateExitInfo>);
	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory\n"));
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Size = sizeof(item);
	item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
	item.ProcessId = HandleToULong(ProcessId);
	item.ThreadId = HandleToULong(ThreadId);

	PushItem(&info->Entry);
}

void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo) 
{
	if (ProcessId == nullptr) 
	{
		return;
	}
	auto size = sizeof(FullItem<ImageLoadInfo>);
	auto info = (FullItem<ImageLoadInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr) 
	{
		KdPrint((DRIVER_PREFIX "ExAllocatePoolWithTag failed\r\n"));
		return;
	}
	::memset(info, 0, size);
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Size = sizeof(item);
	item.Type = ItemType::ImageLoad;
	item.ProcessId = HandleToULong(ProcessId);
	item.ImageSize = ImageInfo->ImageSize;
	item.LoadAddress = ImageInfo->ImageBase;
	if (FullImageName) 
	{
		::memcpy(item.ImageFileName, FullImageName->Buffer, min(FullImageName->Length, MaxImageFileSize * sizeof(WCHAR)));
	}
	else 
	{
		::wcscpy_s(item.ImageFileName, L"(Unknown executable image)");
	}
	PushItem(&info->Entry);
}

void PushItem(LIST_ENTRY* entry)
{
	AutomatedLock<AutoMutex> lock(Globals.Mutex);
	if (Globals.ItemCount > 1024) 
	{
		auto head = RemoveHeadList(&Globals.ItemsHead);
		Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&Globals.ItemsHead, entry);
	Globals.ItemCount++;
}
