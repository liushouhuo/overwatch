#include "deus.hpp"
#include <windows.h>
#include <shellapi.h>
#include <ntstatus.h>
#include <tbb/parallel_do.h>
#include <version.h>
#include <winioctl.h>
#include <winternl.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
EXTERN_C NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING key);
EXTERN_C NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING key);
EXTERN_C NTSTATUS AdjustCiOptions(ULONG CiOptionsValue, PULONG OldCiOptionsValue);

namespace {

std::wstring module(HMODULE module = nullptr) {
  std::wstring path;
  DWORD size = 0;
  do {
    path.resize(path.size() + MAX_PATH);
    size = GetModuleFileName(module, path.data(), static_cast<DWORD>(path.size()));
  } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  path.resize(size);
  return path;
}

HANDLE open() noexcept {
  return CreateFile(L"\\\\.\\Deus", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
}

DEUS_ERROR_CODE is_elevated(BOOL& elevated) noexcept {
  PSID group = nullptr;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
    return deus::make_dword_error(GetLastError());
  }
  std::unique_ptr<std::remove_pointer_t<decltype(group)>, decltype(&FreeSid)> group_ptr(group, FreeSid);
  if (!CheckTokenMembership(nullptr, group, &elevated)) {
    return deus::make_dword_error(GetLastError());
  }
  return {};
}

DEUS_ERROR_CODE request_privileges(LPCWSTR privileges) noexcept {
  TOKEN_PRIVILEGES privilege = {};
  privilege.PrivilegeCount = 1;
  privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  if (!LookupPrivilegeValue(nullptr, L"SeLoadDriverPrivilege", &privilege.Privileges[0].Luid)) {
    return deus::make_dword_error(GetLastError());
  }
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) {
    return deus::make_dword_error(GetLastError());
  }
  std::unique_ptr<std::remove_pointer_t<decltype(token)>, decltype(&CloseHandle)> token_ptr(token, CloseHandle);
  if (!AdjustTokenPrivileges(token, FALSE, &privilege, sizeof(privilege), nullptr, nullptr)) {
    return deus::make_dword_error(GetLastError());
  }
  return {};
}

DEUS_ERROR_CODE load(bool load) {
  // Verify that the process is elevated.
  BOOL elevated = FALSE;
  if (const auto ec = is_elevated(elevated); deus::is_error(ec)) {
    return ec;
  }
  if (!elevated) {
    return deus::make_dword_error(ERROR_ACCESS_DENIED);
  }

  // Request permission to load drivers.
  if (const auto ec = request_privileges(L"SeLoadDriverPrivilege"); deus::is_error(ec)) {
    return ec;
  }

  // Driver source.
  auto src = module(reinterpret_cast<HINSTANCE>(&__ImageBase));
  auto dot = src.find_last_of(L'.');
  if (dot == std::wstring::npos) {
    return deus::make_dword_error(ERROR_INVALID_PARAMETER);
  }
  src.erase(dot).append(L".sys");

  // Driver destination.
  std::wstring dst;
  dst.resize(GetSystemDirectory(dst.data(), 0) + 1);
  dst.resize(GetSystemDirectory(dst.data(), static_cast<UINT>(dst.size())));
  if (dst.empty()) {
    return deus::make_dword_error(ERROR_INVALID_PARAMETER);
  }
  dst.append(L"\\drivers\\deus.sys");

  // Driver registry keys.
  std::wstring sub = L"System\\CurrentControlSet\\Services\\deus";
  std::wstring key = L"\\Registry\\Machine\\" + sub;

  // Unload driver.
  UNICODE_STRING path = {};
  RtlInitUnicodeString(&path, key.data());
  NtUnloadDriver(&path);

  // Delete registry key.
  HKEY hkey = nullptr;
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, sub.data(), 0, KEY_ALL_ACCESS, &hkey) == ERROR_SUCCESS) {
    RegCloseKey(hkey);
    if (const auto error = RegDeleteKeyEx(HKEY_LOCAL_MACHINE, sub.data(), KEY_WOW64_64KEY, 0)) {
      return deus::make_dword_error(error);
    }
  }

  // Delete file.
  if (const auto attributes = GetFileAttributes(dst.data()); attributes != INVALID_FILE_ATTRIBUTES) {
    if (!DeleteFile(dst.data())) {
      return deus::make_dword_error(GetLastError());
    }
  }

  // Return if only a driver unload was requested.
  if (!load) {
    return {};
  }

  // Create file.
  if (!CopyFile(src.data(), dst.data(), TRUE)) {
    return deus::make_dword_error(GetLastError());
  }

  // Create registry key.
  DWORD dispositon = 0;
  if (const auto error = RegCreateKeyEx(HKEY_LOCAL_MACHINE, sub.data(), 0, nullptr, 0, KEY_ALL_ACCESS, NULL, &hkey, &dispositon)) {
    return deus::make_dword_error(error);
  }
  std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> hkey_ptr(hkey, RegCloseKey);

  DWORD ErrorControl = 0;
  const auto ErrorControlData = reinterpret_cast<const BYTE*>(&ErrorControl);
  const auto ErrorControlSize = static_cast<DWORD>(sizeof(ErrorControl));
  if (const auto error = RegSetValueEx(hkey, L"ErrorControl", 0, REG_DWORD, ErrorControlData, ErrorControlSize)) {
    return deus::make_dword_error(error);
  }

  const auto ImagePath = L"\\??\\" + dst;
  const auto ImagePathData = reinterpret_cast<const BYTE*>(ImagePath.data());
  const auto ImagePathSize = static_cast<DWORD>(ImagePath.size() * sizeof(wchar_t));
  if (const auto error = RegSetValueEx(hkey, L"ImagePath", 0, REG_EXPAND_SZ, ImagePathData, ImagePathSize)) {
    return deus::make_dword_error(error);
  }

  DWORD Start = 3;
  const auto StartData = reinterpret_cast<const BYTE*>(&Start);
  const auto StartSize = static_cast<DWORD>(sizeof(Start));
  if (const auto error = RegSetValueEx(hkey, L"Start", 0, REG_DWORD, StartData, StartSize)) {
    return deus::make_dword_error(error);
  }

  DWORD Type = 1;
  const auto TypeData = reinterpret_cast<const BYTE*>(&Type);
  const auto TypeSize = static_cast<DWORD>(sizeof(Type));
  if (const auto error = RegSetValueEx(hkey, L"Type", 0, REG_DWORD, TypeData, TypeSize)) {
    return deus::make_dword_error(error);
  }

  // Disable DSE.
  ULONG OldCiOptionsValue = 0;
  if (const auto status = AdjustCiOptions(0, &OldCiOptionsValue)) {
    return deus::make_ntstatus_error(status);
  }

  // Load driver.
  UNICODE_STRING key_path = {};
  RtlInitUnicodeString(&key_path, key.data());
  const auto status = NtLoadDriver(&key_path);

  // Enable DSE.
  AdjustCiOptions(0x6, &OldCiOptionsValue);

  // Report errors.
  if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_COLLISION) {
    return deus::make_ntstatus_error(status);
  }

  return {};
}

DEUS_ERROR_CODE execute(PCWSTR cmd) {
  const auto file = module();
  SHELLEXECUTEINFO sei = {};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  sei.lpFile = file.data();
  sei.lpParameters = cmd;
  sei.nShow = SW_HIDE;
  if (!ShellExecuteEx(&sei)) {
    return deus::make_dword_error(ERROR_ACCESS_DENIED);
  }
  if (!sei.hProcess) {
    return deus::make_dword_error(ERROR_INVALID_HANDLE);
  }
  DWORD result = ERROR_ABANDONED_WAIT_0;
  WaitForSingleObject(sei.hProcess, INFINITE);
  GetExitCodeProcess(sei.hProcess, &result);
  CloseHandle(sei.hProcess);
  return deus::make_dword_error(result);
}

bool is_project_version(USHORT major, USHORT minor, USHORT patch) {
  return major == PROJECT_VERSION_MAJOR && minor == PROJECT_VERSION_MINOR && patch == PROJECT_VERSION_PATCH;
}

}  // namespace

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_load_cmd(PCWSTR cmd, BOOL reload) {
  // Handle execute commands.
  if (std::wstring_view(cmd) == L"/load" || std::wstring_view(cmd) == L"/unload") {
    const auto ec = load(std::wstring_view(cmd) == L"/load");
    ExitProcess(static_cast<UINT>(0x00000000'FFFFFFFF & ec));
    return ec;
  }

  // Load driver.
  if (reload) {
    if (const auto ec = execute(L"/load"); deus::is_error(ec)) {
      return ec;
    }
  }

  // Open device.
  std::unique_ptr<std::remove_pointer_t<HANDLE>, void (*)(HANDLE)> device(open(), [](HANDLE handle) {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  });
  if (device.get() == INVALID_HANDLE_VALUE) {
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND || reload) {
      return deus::make_dword_error(error);
    }
    if (const auto ec = execute(L"/load"); deus::is_error(ec)) {
      return ec;
    }
    device.reset(open());
    if (device.get() == INVALID_HANDLE_VALUE) {
      return deus::make_dword_error(GetLastError());
    }
  }

  // Verify version.
  DWORD size = 0;
  deus::io::version version = {};
  if (const auto status = deus::control(device.get(), deus::io::code::version, version); status != STATUS_SUCCESS) {
    return deus::make_ntstatus_error(status);
  }
  if (!is_project_version(version.major, version.minor, version.patch)) {
    if (reload) {
      return deus::make_dword_error(ERROR_VERSION_PARSE_ERROR);
    }
    device.reset(INVALID_HANDLE_VALUE);
    if (const auto ec = execute(L"/load"); deus::is_error(ec)) {
      return ec;
    }
    device.reset(open());
    if (device.get() == INVALID_HANDLE_VALUE) {
      return deus::make_dword_error(GetLastError());
    }
    version = {};
    if (const auto status = deus::control(device.get(), deus::io::code::version, version); status != STATUS_SUCCESS) {
      return deus::make_ntstatus_error(status);
    }
    if (!is_project_version(version.major, version.minor, version.patch)) {
      return deus::make_dword_error(ERROR_VERSION_PARSE_ERROR);
    }
  }

  return {};
}

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_load_args(INT argc, CHAR* argv[], BOOL reload) {
  if (argc > 1) {
    if (std::string_view(argv[1]) == "/load") {
      return deus_load_cmd(L"/load", reload);
    }
    if (std::string_view(argv[1]) == "/unload") {
      return deus_load_cmd(L"/unload", reload);
    }
  }
  return deus_load_cmd(L"", reload);
}

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_device_create(HANDLE* handle) {
  // Verify parameters.
  if (!handle) {
    return deus::make_dword_error(ERROR_INVALID_PARAMETER);
  }
  if (*handle != INVALID_HANDLE_VALUE) {
    return deus::make_dword_error(ERROR_DEVICE_ALREADY_ATTACHED);
  }

  // Open device.
  std::unique_ptr<std::remove_pointer_t<HANDLE>, void (*)(HANDLE)> device(open(), [](HANDLE handle) {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  });
  if (device.get() == INVALID_HANDLE_VALUE) {
    return deus::make_dword_error(GetLastError());
  }

  // Verify version.
  DWORD size = 0;
  deus::io::version version = {};
  if (const auto status = deus::control(device.get(), deus::io::code::version, version); status != STATUS_SUCCESS) {
    return deus::make_ntstatus_error(status);
  }
  if (!is_project_version(version.major, version.minor, version.patch)) {
    return deus::make_dword_error(ERROR_VERSION_PARSE_ERROR);
  }

  *handle = device.release();
  return {};
}

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_device_destroy(HANDLE handle) {
  if (handle != INVALID_HANDLE_VALUE && !CloseHandle(handle)) {
    return deus::make_dword_error(GetLastError());
  }
  return {};
}

namespace {

class stop_iterator {
public:
  using iterator_category = std::input_iterator_tag;
  using difference_type = std::size_t;
  using value_type = std::atomic_bool;
  using reference = value_type&;
  using pointer = value_type*;

  constexpr stop_iterator() noexcept = default;
  constexpr stop_iterator(std::atomic_bool& stop) noexcept : stop_(std::addressof(stop)) {
  }

  constexpr bool operator==(const stop_iterator& other) const noexcept {
    return stop_ == other.stop_;
  }

  constexpr bool operator!=(const stop_iterator& other) const noexcept {
    return !(*this == other);
  }

  stop_iterator& operator++() noexcept {
    if (stop_->load(std::memory_order_relaxed)) {
      stop_ = nullptr;
    }
    return *this;
  }

  stop_iterator operator++(int) = delete;

  constexpr reference operator*() noexcept {
    return *stop_;
  }

  constexpr pointer operator->() noexcept {
    return stop_;
  }

  void stop() noexcept {
    if (stop_) {
      stop_->exchange(true, std::memory_order_release);
    }
  }

private:
  std::atomic_bool* stop_ = nullptr;
};

// clang-format off
__forceinline void deus_scan(
  UINT_PTR base,                // region base address
  const BYTE* beg,              // copied data begin()
  const BYTE* end,              // copied data end()
  const BYTE* scan,             // binary representation of the scan signature
  const BYTE* mask,             // binary representation of the signature mask
  SIZE_T size,                  // number of bytes in 'scan' and 'mask'
  SIZE_T count,                 // maximum number of hits
  std::mutex& mutex,            // mutex for the callback function
  std::atomic_size_t& counter,  // current number of hits
  deus_scan_callback callback,  // callback function for reporting hits
  PVOID context) noexcept       // context passed to the callback function
// clang-format on
{
  std::size_t mask_index = 0;
  const auto searcher = std::default_searcher(scan, scan + size, [&](const auto lhs, const auto rhs) noexcept {
    if ((lhs & mask[mask_index++]) == rhs) {
      return true;
    }
    mask_index = 0;
    return false;
  });
  for (auto cur = beg; cur < end && (!count || counter.load(std::memory_order_relaxed) < count); cur++) {
    cur = std::search(cur, end, searcher);
    if (cur != end && (!count || counter.fetch_add(1, std::memory_order_release) < count)) {
      std::lock_guard lock(mutex);
      callback(context, base + (cur - beg));
    }
  }
}

// clang-format off
template <typename Searcher>
__forceinline void deus_scan(
  UINT_PTR base,                // region base
  const BYTE* beg,              // copied data begin()
  const BYTE* end,              // copied data end()
  const Searcher& searcher,     // e.g. std::boyer_moore_horspool_searcher(scan, scan + size);
  SIZE_T count,                 // maximum number of hits
  std::mutex& mutex,            // mutex for the callback function
  std::atomic_size_t& counter,  // current number of hits
  deus_scan_callback callback,  // callback function for reporting hits
  PVOID context) noexcept       // context passed to the callback function
// clang-format on
{
  for (auto cur = beg; cur < end && (!count || counter.load(std::memory_order_relaxed) < count); cur++) {
    cur = std::search(cur, end, searcher);
    if (cur != end && (!count || counter.fetch_add(1, std::memory_order_release) < count)) {
      std::lock_guard lock(mutex);
      callback(context, base + (cur - beg));
    }
  }
}

}  // namespace

// clang-format off
DEUS_API void DEUS_CALL deus_scan_memory(
  PSLIST_HEADER memory,
  const BYTE* scan,
  const BYTE* mask,
  SIZE_T size,
  SIZE_T count,
  deus_scan_callback callback,
  PVOID context)
// clang-format on
{
  if (!memory || !scan || !callback) {
    return;
  }

  std::mutex mutex;
  std::atomic_size_t counter = 0;
  const auto searcher = std::boyer_moore_horspool_searcher(scan, scan + size);

  SLIST_HEADER regions;
  InitializeSListHead(&regions);
  std::atomic_bool stop = false;
  tbb::parallel_do(stop_iterator(stop), stop_iterator(), [&](stop_iterator& it) {
    const auto entry = InterlockedPopEntrySList(memory);
    if (!entry) {
      it.stop();
      return;
    }
    const auto region = static_cast<deus::io::region*>(entry);
    if (!count || counter.load(std::memory_order_relaxed) < count) {
      const auto beg = region->data();
      const auto end = region->data() + region->size();
      if (mask) {
        deus_scan(region->base_address, beg, end, scan, mask, size, count, mutex, counter, callback, context);
      } else {
        deus_scan(region->base_address, beg, end, searcher, count, mutex, counter, callback, context);
      }
    }
    InterlockedPushEntrySList(&regions, region);
  });

  for (auto entry = InterlockedFlushSList(&regions); entry;) {
    const auto next = entry->Next;
    InterlockedPushEntrySList(memory, entry);
    entry = next;
  }
}

// clang-format off
DEUS_API void DEUS_CALL deus_scan_process(
  HANDLE handle,
  HANDLE pid,
  const BYTE* scan,
  const BYTE* mask,
  SIZE_T size,
  SIZE_T count,
  UINT_PTR min,
  UINT_PTR max,
  deus_scan_callback callback,
  PVOID context)
// clang-format on
{
  if (handle == INVALID_HANDLE_VALUE || !pid || !scan || !callback) {
    return;
  }

  deus::memory memory;

  deus::io::query query;
  query.pid = pid;
  query.min = min;
  query.max = max;
  query.memory = memory.header();
  if (const auto status = deus::control(handle, deus::io::code::query, query); status) {
    std::puts(("query: " + deus::make_ntstatus_error_code(status).message()).data());
    return;
  }

  std::mutex mutex;
  std::atomic_size_t counter = 0;
  const auto searcher = std::boyer_moore_horspool_searcher(scan, scan + size);

  std::atomic_bool stop = false;
  tbb::parallel_do(stop_iterator(stop), stop_iterator(), [&](stop_iterator& it) {
    const auto entry = InterlockedPopEntrySList(memory.header());
    if (!entry) {
      it.stop();
      return;
    }
    const auto region = static_cast<deus::io::region*>(entry);
    if (!count || counter.load(std::memory_order_relaxed) < count) {
      if (auto data = std::unique_ptr<BYTE[]>(new (std::nothrow) BYTE[region->region_size])) {
        deus::io::copy copy;
        copy.pid = pid;
        copy.src = region->base_address;
        copy.dst = reinterpret_cast<UINT_PTR>(data.get());
        copy.size = region->region_size;
        if (const auto status = deus::control(handle, deus::io::code::read, copy); !status) {
          const auto beg = data.get();
          const auto end = data.get() + region->region_size;
          if (mask) {
            deus_scan(region->base_address, beg, end, scan, mask, size, count, mutex, counter, callback, context);
          } else {
            deus_scan(region->base_address, beg, end, searcher, count, mutex, counter, callback, context);
          }
        } else {
          std::puts(("read: " + deus::make_ntstatus_error_code(status).message()).data());
        }
      }
    }
    VirtualFree(region, 0, MEM_RELEASE);
  });
}
