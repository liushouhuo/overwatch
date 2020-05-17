// Deus Driver
// https://docs.microsoft.com/en-us/windows/desktop/api/winnt/ns-winnt-_memory_basic_information
//
#include "deus.hpp"
#include <ntifs.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <version.h>
#include <wchar.h>
#include <new>

extern "C" {

constexpr ULONG KEYBOARD_CLASS_CONNECT_REQUEST = 0x0B0203;
constexpr ULONG POINTER_CLASS_CONNECT_REQUEST = 0x0F0203;

typedef enum _MEMORY_INFORMATION_CLASS_EX {
  MemoryBasicInformationEx = 0,
  MemoryWorkingSetInformation = 1,
  MemoryMappedFilenameInformation = 2,
  MemoryRegionInformation = 3,
  MemoryWorkingSetExInformation = 4,
} MEMORY_INFORMATION_CLASS_EX;

// clang-format off

NTKERNELAPI NTSTATUS NTAPI MmCopyVirtualMemory(
  IN PEPROCESS FromProcess,
  IN PVOID FromAddress,
  IN PEPROCESS ToProcess,
  OUT PVOID ToAddress,
  IN SIZE_T BufferSize,
  IN KPROCESSOR_MODE PreviousMode,
  OUT PSIZE_T NumberOfBytesCopied);

NTKERNELAPI NTSTATUS NTAPI ZwQueryDirectoryObject(
  IN HANDLE DirectoryHandle,
  OUT PVOID Buffer,
  IN ULONG BufferLength,
  IN BOOLEAN ReturnSingleEntry,
  IN BOOLEAN RestartScan,
  IN OUT PULONG Context,
  OUT PULONG ReturnLength OPTIONAL);

typedef void(__fastcall* KeyboardService)(
  PDEVICE_OBJECT device,
  PKEYBOARD_INPUT_DATA begin,
  PKEYBOARD_INPUT_DATA end,
  PULONG consumed);

typedef void(__fastcall* MouseService)(
  PDEVICE_OBJECT device,
  PMOUSE_INPUT_DATA begin,
  PMOUSE_INPUT_DATA end,
  PULONG consumed);

typedef NTSTATUS(__fastcall* MouseCallback)(
  PVOID p0,
  PVOID p1,
  PVOID p2,
  PVOID p3,
  PVOID p4);

// clang-format on

}  // extern "C"

// Events
LONG64 g_process = 0;
PSLIST_HEADER g_events = nullptr;

// Control
PDEVICE_OBJECT g_control = nullptr;

// Keyboard
PDEVICE_OBJECT g_keyboard = nullptr;
PDEVICE_OBJECT g_keyboard_target = nullptr;
KeyboardService g_keyboard_service = nullptr;
constexpr USHORT g_keyboard_id = 0;

// Mouse
PDEVICE_OBJECT g_mouse = nullptr;
PDEVICE_OBJECT g_mouse_target = nullptr;
PDRIVER_DISPATCH g_mouse_read = nullptr;
MouseService g_mouse_service = nullptr;
MouseCallback g_mouse_callback = nullptr;
PMOUSE_INPUT_DATA g_mouse_data = nullptr;
unsigned char g_mouse_state[5] = {};
constexpr USHORT g_mouse_id = 0;
LONG g_mouse_offset = 0;
LONG g_mouse_last_x = 0;
LONG g_mouse_last_y = 0;

void Sanitize(PMOUSE_INPUT_DATA data) {
  if (data->ButtonFlags & MOUSE_LEFT_BUTTON_DOWN && g_mouse_state[0]) {
    data->ButtonFlags &= ~MOUSE_LEFT_BUTTON_DOWN;
  }
  if (data->ButtonFlags & MOUSE_LEFT_BUTTON_UP && !g_mouse_state[0]) {
    data->ButtonFlags &= ~MOUSE_LEFT_BUTTON_UP;
  }
  if (data->ButtonFlags & MOUSE_RIGHT_BUTTON_DOWN && g_mouse_state[1]) {
    data->ButtonFlags &= ~MOUSE_RIGHT_BUTTON_DOWN;
  }
  if (data->ButtonFlags & MOUSE_RIGHT_BUTTON_UP && !g_mouse_state[1]) {
    data->ButtonFlags &= ~MOUSE_RIGHT_BUTTON_UP;
  }
  if (data->ButtonFlags & MOUSE_MIDDLE_BUTTON_DOWN && g_mouse_state[2]) {
    data->ButtonFlags &= ~MOUSE_MIDDLE_BUTTON_DOWN;
  }
  if (data->ButtonFlags & MOUSE_MIDDLE_BUTTON_UP && !g_mouse_state[2]) {
    data->ButtonFlags &= ~MOUSE_MIDDLE_BUTTON_UP;
  }
  if (data->ButtonFlags & MOUSE_BUTTON_4_DOWN && g_mouse_state[3]) {
    data->ButtonFlags &= ~MOUSE_BUTTON_4_DOWN;
  }
  if (data->ButtonFlags & MOUSE_BUTTON_4_UP && !g_mouse_state[3]) {
    data->ButtonFlags &= ~MOUSE_BUTTON_4_UP;
  }
  if (data->ButtonFlags & MOUSE_BUTTON_5_DOWN && g_mouse_state[4]) {
    data->ButtonFlags &= ~MOUSE_BUTTON_5_DOWN;
  }
  if (data->ButtonFlags & MOUSE_BUTTON_5_UP && !g_mouse_state[4]) {
    data->ButtonFlags &= ~MOUSE_BUTTON_5_UP;
  }
  if (data->Flags & MOUSE_MOVE_ABSOLUTE) {
    if (g_mouse_offset) {
      data->LastX = g_mouse_last_x;
      data->LastY = g_mouse_last_y;
    } else {
      g_mouse_last_x = data->LastX;
      g_mouse_last_y = data->LastY;
    }
  } else {
    if (g_mouse_offset) {
      data->LastX = 0;
      data->LastY = 0;
    } else {
      g_mouse_last_x += data->LastX;
      g_mouse_last_y += data->LastY;
    }
  }
}

NTSTATUS OnVersion(deus::io::version* version) {
  version->major = PROJECT_VERSION_MAJOR;
  version->minor = PROJECT_VERSION_MINOR;
  version->patch = PROJECT_VERSION_PATCH;
  return STATUS_SUCCESS;
}

NTSTATUS OnOpen() {
  const auto process = reinterpret_cast<LONG64>(PsGetCurrentProcess());
  if (!process) {
    return STATUS_PROCESS_IN_JOB;
  }
  InterlockedExchange64(&g_process, process);
  return STATUS_SUCCESS;
}

NTSTATUS OnClose() {
  if (const auto process = reinterpret_cast<LONG64>(PsGetCurrentProcess())) {
    InterlockedCompareExchange64(&g_process, 0, process);
  } else {
    InterlockedExchange64(&g_process, 0);
  }
  return STATUS_SUCCESS;
}

NTSTATUS OnPoll(SLIST_HEADER* header) {
  if (!g_events) {
    return STATUS_NO_MEMORY;
  }
  NTSTATUS status = STATUS_SUCCESS;
  for (auto entry = InterlockedFlushSList(g_events); entry;) {
    const auto next = entry->Next;
    const auto event = static_cast<deus::io::event*>(entry);
    const auto event_size = sizeof(deus::io::event) + event->path_size * sizeof(WCHAR);
    if (NT_SUCCESS(status)) {
      PVOID buffer = nullptr;
      SIZE_T size = event_size;
      status = ZwAllocateVirtualMemory(NtCurrentProcess(), &buffer, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (NT_SUCCESS(status)) {
        RtlCopyMemory(buffer, event, event_size);
        InterlockedPushEntrySList(header, static_cast<deus::io::event*>(buffer));
      }
    }
    MmFreeNonCachedMemory(event, event_size);
    entry = next;
  }
  return status;
}

NTSTATUS OnCopy(deus::io::copy* copy, bool read) {
  // Verify address validity.
  if (copy->src < deus::io::min || copy->dst < deus::io::min) {
    return STATUS_INVALID_PARAMETER;
  }
  if (copy->src + copy->size > deus::io::max || copy->dst + copy->size > deus::io::max) {
    return STATUS_INVALID_PARAMETER;
  }

  // Acquire target process handle.
  PEPROCESS target = nullptr;
  if (const auto status = PsLookupProcessByProcessId(copy->pid, &target); !NT_SUCCESS(status)) {
    return STATUS_NOT_FOUND;
  }

  const auto current = PsGetCurrentProcess();
  const auto src = reinterpret_cast<PVOID>(copy->src);
  const auto dst = reinterpret_cast<PVOID>(copy->dst);

  SIZE_T size = 0;
  NTSTATUS status = STATUS_SUCCESS;
  if (read) {
    status = MmCopyVirtualMemory(target, src, current, dst, copy->size, KernelMode, &size);
  } else {
    status = MmCopyVirtualMemory(current, src, target, dst, copy->size, KernelMode, &size);
  }
  if (NT_SUCCESS(status) && size != copy->size) {
    status = STATUS_BUFFER_TOO_SMALL;
  }

  // Release process handle.
  ObDereferenceObject(target);

  return STATUS_SUCCESS;
}

NTSTATUS OnQuery(deus::io::query* query) {
  // Verify address validity.
  if (query->min < deus::io::min || query->max > deus::io::max) {
    return STATUS_INVALID_PARAMETER;
  }

  // Verify list header.
  if (!query->memory) {
    return STATUS_INVALID_PARAMETER;
  }

  // Acquire target process handle.
  PEPROCESS target = nullptr;
  if (const auto status = PsLookupProcessByProcessId(query->pid, &target); !NT_SUCCESS(status)) {
    return STATUS_NOT_FOUND;
  }

  // Initialize IPC list header.
  SLIST_HEADER regions;
  InitializeSListHead(&regions);

  const auto min = query->min;
  const auto max = query->max;

  // Attach to process.
  KAPC_STATE state = {};
  KeStackAttachProcess(target, &state);

  NTSTATUS status = STATUS_SUCCESS;
  MEMORY_BASIC_INFORMATION mbi = {};
  for (auto pos = min; pos < max; pos = reinterpret_cast<ULONG_PTR>(mbi.BaseAddress) + mbi.RegionSize) {
    // Get next region.
    SIZE_T size = 0;
    constexpr auto info = static_cast<MEMORY_INFORMATION_CLASS>(MemoryBasicInformationEx);
    status = ZwQueryVirtualMemory(ZwCurrentProcess(), reinterpret_cast<PVOID>(pos), info, &mbi, sizeof(mbi), &size);
    if (!NT_SUCCESS(status)) {
      status = status == STATUS_INVALID_PARAMETER ? STATUS_SUCCESS : status;
      break;
    }

    // Skip non-committed, non-accessible, guarded, and iamge regions.
    if (!(mbi.State & MEM_COMMIT) || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || (mbi.Type & 0x1000000)) {
      continue;
    }

    // WARNING: Hard-coded entity page filter.
    if (mbi.RegionSize != 1572864 || mbi.State != 0x1000 || mbi.Protect != 0x4 || mbi.Type != 0x20000) {
      continue;
    }

    // Copy region information.
    const auto region = static_cast<deus::io::region*>(MmAllocateNonCachedMemory(sizeof(deus::io::region)));
    if (!region) {
      status = STATUS_NO_MEMORY;
      break;
    }
    new (region) deus::io::region{
      nullptr,
      reinterpret_cast<UINT_PTR>(mbi.BaseAddress),
      reinterpret_cast<UINT_PTR>(mbi.AllocationBase),
      mbi.AllocationProtect,
      mbi.RegionSize,
      mbi.State,
      mbi.Protect,
      mbi.Type,
      0,
    };
    InterlockedPushEntrySList(&regions, region);
  }

  // Detach from process.
  KeUnstackDetachProcess(&state);

  // Release process handle.
  ObDereferenceObject(target);

  // Copy region information and region data.
  const auto current = PsGetCurrentProcess();
  for (auto entry = InterlockedFlushSList(&regions); entry;) {
    if (NT_SUCCESS(status)) {
      PVOID data = nullptr;
      SIZE_T size = sizeof(deus::io::region);
      status = ZwAllocateVirtualMemory(NtCurrentProcess(), &data, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (NT_SUCCESS(status)) {
        const auto region = static_cast<deus::io::region*>(data);
        new (region) deus::io::region(*static_cast<deus::io::region*>(entry));
        InterlockedPushEntrySList(query->memory, region);
      }
    }
    const auto next = entry->Next;
    MmFreeNonCachedMemory(static_cast<deus::io::region*>(entry), sizeof(deus::io::region));
    entry = next;
  }

  return status;
}

NTSTATUS OnKeyboard(KEYBOARD_INPUT_DATA* data) {
  if (g_keyboard_service && g_keyboard_target) {
    data->UnitId = g_keyboard_id;
    ULONG consumed = 1;
    g_keyboard_service(g_keyboard_target, data, data + 1, &consumed);
  }
  return STATUS_SUCCESS;
}

NTSTATUS OnMouse(MOUSE_INPUT_DATA* data) {
  Sanitize(data);
  if (g_mouse_service && g_mouse_target) {
    data->UnitId = g_mouse_id;
    ULONG consumed = 1;
    g_mouse_service(g_mouse_target, data, data + 1, &consumed);
  }
  return STATUS_SUCCESS;
}

// Create process callback.
static VOID CreateProcessNotifyRoutine(HANDLE parent, HANDLE pid, BOOLEAN create) {
  UNREFERENCED_PARAMETER(parent);

  // Verify parameters and global process handle.
  if (!pid || !g_process) {
    return;
  }

  // Acquire process handle.
  PEPROCESS process = nullptr;
  if (const auto status = PsLookupProcessByProcessId(pid, &process); !NT_SUCCESS(status)) {
    return;
  }

  // Allocate and push event.
  PUNICODE_STRING path = {};
  if (NT_SUCCESS(SeLocateProcessImageName(process, &path)) && path->Buffer && path->Length) {
    const auto path_size = wcsnlen(path->Buffer, path->Length);
    const auto event_size = sizeof(deus::io::event) + path_size * sizeof(WCHAR);
    if (const auto event = reinterpret_cast<deus::io::event*>(MmAllocateNonCachedMemory(event_size))) {
      new (event) deus::io::event{
        nullptr,
        create ? deus::io::event::type::process_created : deus::io::event::type::process_terminated,
        pid,
        0,
        0,
        path_size,
      };
      RtlCopyMemory(reinterpret_cast<PVOID>(event + 1), path->Buffer, path_size * sizeof(WCHAR));
      if (g_process && g_events) {
        InterlockedPushEntrySList(g_events, static_cast<PSLIST_ENTRY>(event));
      } else {
        MmFreeNonCachedMemory(event, event_size);
      }
    }
  }

  // Release process handle.
  ObDereferenceObject(process);
}

NTSTATUS OnDelay(LONG* offset) {
  g_mouse_offset = *offset;
  return STATUS_SUCCESS;
}

// Load image callback.
static VOID LoadImageNotifyRoutine(PUNICODE_STRING path, HANDLE pid, PIMAGE_INFO info) {
  // Verify parameters and global process handle.
  if (!path || !path->Buffer || !path->Length || !pid || !info || !info->ImageBase || !info->ImageSize || !g_process) {
    return;
  }

  // Allocate and push event.
  const auto path_size = wcsnlen(path->Buffer, path->Length);
  const auto event_size = sizeof(deus::io::event) + path_size * sizeof(WCHAR);
  if (const auto event = reinterpret_cast<deus::io::event*>(MmAllocateNonCachedMemory(event_size))) {
    new (event) deus::io::event{
      nullptr,
      deus::io::event::type::image_loaded,
      pid,
      reinterpret_cast<UINT_PTR>(info->ImageBase),
      info->ImageSize,
      path_size,
    };
    RtlCopyMemory(reinterpret_cast<PVOID>(event + 1), path->Buffer, path_size * sizeof(WCHAR));
    if (g_process && g_events) {
      InterlockedPushEntrySList(g_events, static_cast<PSLIST_ENTRY>(event));
    } else {
      MmFreeNonCachedMemory(event, event_size);
    }
  }
}

inline bool Verify(PIRP irp, PIO_STACK_LOCATION stack, SIZE_T size = 0) {
  if (stack->Parameters.DeviceIoControl.InputBufferLength != size) {
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
    return false;
  }
  if (stack->Parameters.DeviceIoControl.OutputBufferLength != size) {
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
    return false;
  }
  if (size && !irp->AssociatedIrp.SystemBuffer) {
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    return false;
  }
  irp->IoStatus.Information = size;
  return true;
}

static DRIVER_DISPATCH OnDispatch;
static NTSTATUS OnDispatch(PDEVICE_OBJECT device, PIRP irp) {
  UNREFERENCED_PARAMETER(device);
  irp->IoStatus.Status = STATUS_SUCCESS;
  irp->IoStatus.Information = 0;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

static DRIVER_DISPATCH OnInternalDeviceControl;
static NTSTATUS OnInternalDeviceControl(PDEVICE_OBJECT device, PIRP irp) {
  UNREFERENCED_PARAMETER(device);
  const auto stack = IoGetCurrentIrpStackLocation(irp);
  switch (stack->Parameters.DeviceIoControl.IoControlCode) {
  case KEYBOARD_CLASS_CONNECT_REQUEST:
    if (const auto cd = reinterpret_cast<PCONNECT_DATA>(stack->Parameters.DeviceIoControl.Type3InputBuffer)) {
      g_keyboard_service = reinterpret_cast<decltype(g_keyboard_service)>(cd->ClassService);
    }
    break;
  case POINTER_CLASS_CONNECT_REQUEST:
    if (const auto cd = reinterpret_cast<PCONNECT_DATA>(stack->Parameters.DeviceIoControl.Type3InputBuffer)) {
      g_mouse_service = reinterpret_cast<decltype(g_mouse_service)>(cd->ClassService);
    }
    break;
  }
  irp->IoStatus.Status = STATUS_SUCCESS;
  irp->IoStatus.Information = 0;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

static DRIVER_DISPATCH OnDeviceControl;
static NTSTATUS OnDeviceControl(PDEVICE_OBJECT device, PIRP irp) {
  UNREFERENCED_PARAMETER(device);
  const auto stack = IoGetCurrentIrpStackLocation(irp);
  const auto buffer = irp->AssociatedIrp.SystemBuffer;
  switch (const auto code = static_cast<deus::io::code>(stack->Parameters.DeviceIoControl.IoControlCode)) {
  case deus::io::code::version:
    if (Verify(irp, stack, sizeof(deus::io::version))) {
      irp->IoStatus.Status = OnVersion(static_cast<deus::io::version*>(buffer));
    }
    break;
  case deus::io::code::open:
    if (Verify(irp, stack)) {
      irp->IoStatus.Status = OnOpen();
    }
    break;
  case deus::io::code::close:
    if (Verify(irp, stack)) {
      irp->IoStatus.Status = OnClose();
    }
    break;
  case deus::io::code::poll:
    if (Verify(irp, stack, sizeof(SLIST_HEADER))) {
      irp->IoStatus.Status = OnPoll(static_cast<SLIST_HEADER*>(buffer));
    }
    break;
  case deus::io::code::read:
    [[fallthrough]];
  case deus::io::code::write:
    if (Verify(irp, stack, sizeof(deus::io::copy))) {
      irp->IoStatus.Status = OnCopy(static_cast<deus::io::copy*>(buffer), code == deus::io::code::read);
    }
    break;
  case deus::io::code::query:
    if (Verify(irp, stack, sizeof(deus::io::query))) {
      irp->IoStatus.Status = OnQuery(static_cast<deus::io::query*>(buffer));
    }
    break;
  case deus::io::code::keyboard:
    if (Verify(irp, stack, sizeof(KEYBOARD_INPUT_DATA))) {
      irp->IoStatus.Status = OnKeyboard(static_cast<KEYBOARD_INPUT_DATA*>(buffer));
    }
    break;
  case deus::io::code::mouse:
    if (Verify(irp, stack, sizeof(MOUSE_INPUT_DATA))) {
      irp->IoStatus.Status = OnMouse(static_cast<MOUSE_INPUT_DATA*>(buffer));
    }
    break;
  case deus::io::code::delay:
    if (Verify(irp, stack, sizeof(LONG))) {
      irp->IoStatus.Status = OnDelay(static_cast<LONG*>(buffer));
    }
    break;
  default:
    irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    irp->IoStatus.Information = 0;
    break;
  }
  const auto status = irp->IoStatus.Status;
  IoCompleteRequest(irp, IO_NO_INCREMENT);
  return status;
}

static NTSTATUS __fastcall OnMouseCallback(PVOID p0, PVOID p1, PVOID p2, PVOID p3, PVOID p4) {
  Sanitize(g_mouse_data);
  if (g_mouse_data->ButtonFlags & MOUSE_LEFT_BUTTON_DOWN) {
    g_mouse_state[0] = 0x1;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_LEFT_BUTTON_UP) {
    g_mouse_state[0] = 0x0;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_RIGHT_BUTTON_DOWN) {
    g_mouse_state[1] = 0x1;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_RIGHT_BUTTON_UP) {
    g_mouse_state[1] = 0x0;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_MIDDLE_BUTTON_DOWN) {
    g_mouse_state[2] = 0x1;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_MIDDLE_BUTTON_UP) {
    g_mouse_state[2] = 0x0;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_BUTTON_4_DOWN) {
    g_mouse_state[3] = 0x1;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_BUTTON_4_UP) {
    g_mouse_state[3] = 0x0;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_BUTTON_5_DOWN) {
    g_mouse_state[4] = 0x1;
  }
  if (g_mouse_data->ButtonFlags & MOUSE_BUTTON_5_UP) {
    g_mouse_state[4] = 0x0;
  }
  return g_mouse_callback(p0, p1, p2, p3, p4);
}

static DRIVER_DISPATCH OnMouseRead;
static NTSTATUS OnMouseRead(PDEVICE_OBJECT device, PIRP irp) {
  auto callback = reinterpret_cast<MouseCallback*>(reinterpret_cast<UINT_PTR*>(irp) + 0xB);
  if (!g_mouse_callback) {
    g_mouse_callback = *callback;
  }
  g_mouse_data = reinterpret_cast<PMOUSE_INPUT_DATA>(irp->UserBuffer);
  Sanitize(g_mouse_data);  // is this really necessary?
  *callback = OnMouseCallback;
  return g_mouse_read(device, irp);
}

PVOID FindDeviceNode(PDEVICE_OBJECT device) {
  if (device->DeviceObjectExtension->DeviceNode) {
    return device->DeviceObjectExtension->DeviceNode;
  }
  if (device->DeviceObjectExtension->AttachedTo) {
    return FindDeviceNode(device->DeviceObjectExtension->AttachedTo);
  }
  return nullptr;
}

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path) {
  UNREFERENCED_PARAMETER(path);

  NTSTATUS status = STATUS_SUCCESS;

  // Reset global process.
  InterlockedExchange64(&g_process, 0);

  // Register dispatch callbacks.
  for (unsigned int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
    driver->MajorFunction[i] = OnDispatch;
  }
  driver->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = OnInternalDeviceControl;
  driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = OnDeviceControl;

  // Register unload callback.
  driver->DriverUnload = [](PDRIVER_OBJECT driver) {
    // Reset global process.
    InterlockedExchange64(&g_process, 0);

    // Unregister load image callback.
    PsRemoveLoadImageNotifyRoutine(LoadImageNotifyRoutine);

    // Unregister create process callback.
    PsSetCreateProcessNotifyRoutine(CreateProcessNotifyRoutine, TRUE);

    // Delete symbolic link.
    UNICODE_STRING symbolic_link_name = {};
    RtlInitUnicodeString(&symbolic_link_name, L"\\DosDevices\\Deus");
    IoDeleteSymbolicLink(&symbolic_link_name);

    // Unhook mouse device.
    if (g_mouse_target && g_mouse_read) {
      g_mouse_target->DriverObject->MajorFunction[IRP_MJ_READ] = g_mouse_read;
    }

    // Delete mouse device.
    if (g_mouse) {
      IoDeleteDevice(g_mouse);
    }

    // Delete keyboard device.
    if (g_keyboard) {
      IoDeleteDevice(g_keyboard);
    }

    // Delete control device.
    IoDeleteDevice(g_control);
  };

  // Create control device.
  UNICODE_STRING device_name = {};
  RtlInitUnicodeString(&device_name, L"\\Device\\Deus");
  status = IoCreateDevice(driver, 0, &device_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_control);
  if (!NT_SUCCESS(status) || !g_control) {
    return !NT_SUCCESS(status) ? status : STATUS_UNEXPECTED_IO_ERROR;
  }
  g_control->Flags &= ~DO_DEVICE_INITIALIZING;
  g_control->Flags |= DO_DIRECT_IO;

  // Create symbolic link.
  UNICODE_STRING symbolic_link_name = {};
  RtlInitUnicodeString(&symbolic_link_name, L"\\DosDevices\\Deus");
  status = IoCreateSymbolicLink(&symbolic_link_name, &device_name);
  if (!NT_SUCCESS(status)) {
    IoDeleteDevice(g_control);
    return status;
  }

  // Allocate and initialize events header.
  g_events = reinterpret_cast<PSLIST_HEADER>(MmAllocateNonCachedMemory(sizeof(SLIST_HEADER)));
  if (g_events) {
    InitializeSListHead(g_events);
  }

  // Register create process callback.
  (void)PsSetCreateProcessNotifyRoutine(CreateProcessNotifyRoutine, FALSE);

  // Register load image callback.
  (void)PsSetLoadImageNotifyRoutine(LoadImageNotifyRoutine);

  // Create keyboard device.
  UNICODE_STRING keyboard_name = {};
  RtlInitUnicodeString(&keyboard_name, L"\\Device\\DeusKeyboard");
  IoCreateDevice(driver, 0, &keyboard_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_keyboard);
  if (NT_SUCCESS(status) && g_keyboard) {
    g_keyboard->Flags |= DO_BUFFERED_IO;
    g_keyboard->Flags &= ~DO_DEVICE_INITIALIZING;
    PFILE_OBJECT file_object = nullptr;
    UNICODE_STRING keyboard_class_name = {};
    RtlInitUnicodeString(&keyboard_class_name, L"\\Device\\KeyboardClass0");  // reg query HKLM\HARDWARE\DeviceMap\KeyboardClass
    status = IoGetDeviceObjectPointer(&keyboard_class_name, 0, &file_object, &g_keyboard_target);
    if (NT_SUCCESS(status)) {
      ObDereferenceObject(file_object);
      if (const auto device_node = FindDeviceNode(g_keyboard_target)) {
        // Add keyboard device to the KeyboardClass0 chain.
        g_keyboard->DeviceObjectExtension->DeviceNode = device_node;
        g_keyboard_target->DriverObject->DriverExtension->AddDevice(g_keyboard_target->DriverObject, g_keyboard);
      }
    }
  }

  // Create mouse device.
  UNICODE_STRING mouse_name = {};
  RtlInitUnicodeString(&mouse_name, L"\\Device\\DeusMouse");
  IoCreateDevice(driver, 0, &mouse_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_mouse);
  if (NT_SUCCESS(status) && g_mouse) {
    g_mouse->Flags |= DO_BUFFERED_IO;
    g_mouse->Flags &= ~DO_DEVICE_INITIALIZING;
    PFILE_OBJECT file_object = nullptr;
    UNICODE_STRING mouse_class_name = {};
    RtlInitUnicodeString(&mouse_class_name, L"\\Device\\PointerClass0");  // reg query HKLM\HARDWARE\DeviceMap\PointerClass
    status = IoGetDeviceObjectPointer(&mouse_class_name, 0, &file_object, &g_mouse_target);
    if (NT_SUCCESS(status)) {
      ObDereferenceObject(file_object);
      RtlZeroMemory(g_mouse_state, sizeof(g_mouse_state));
      if (const auto device_node = FindDeviceNode(g_mouse_target)) {
        // Add mouse device to the MouseClass0 chain.
        g_mouse->DeviceObjectExtension->DeviceNode = device_node;
        g_mouse_target->DriverObject->DriverExtension->AddDevice(g_mouse_target->DriverObject, g_mouse);

        // Hook mouse device.
        g_mouse_read = g_mouse_target->DriverObject->MajorFunction[IRP_MJ_READ];
        g_mouse_target->DriverObject->MajorFunction[IRP_MJ_READ] = OnMouseRead;
      }
    }
  }

  return STATUS_SUCCESS;
}
