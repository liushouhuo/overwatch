#pragma once
#ifdef DEUS_DRIVER
#  include <ntifs.h>
#  include <ntddk.h>
#else
#  include <windows.h>
#  include <ntddkbd.h>
#  include <ntddmou.h>
#  include <ntstatus.h>
#  include <winioctl.h>
#  include <winternl.h>
#  include <functional>
#  include <string>
#  include <system_error>
#  include <type_traits>
#  include <vector>
#  include <cassert>
#  include <cstdint>
#endif

#ifndef DEUS_EXPORTS
#  define DEUS_API _declspec(dllimport)
#else
#  define DEUS_API _declspec(dllexport)
#endif

#define DEUS_CALL __vectorcall

#ifndef DEUS_DRIVER

extern "C" {

enum DEUS_ERROR_CATEGORY : UINT32 {
  DEUS_ERROR_CATEGORY_DWORD = 0,
  DEUS_ERROR_CATEGORY_HRESULT = 1,
  DEUS_ERROR_CATEGORY_NTSTATUS = 2,
};

typedef UINT64 DEUS_ERROR_CODE;

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_load_cmd(PCWSTR cmd, BOOL reload);
DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_load_args(INT argc, CHAR* argv[], BOOL reload);

DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_device_create(HANDLE* handle);
DEUS_API DEUS_ERROR_CODE DEUS_CALL deus_device_destroy(HANDLE handle);

typedef void(DEUS_CALL* deus_scan_callback)(PVOID context, UINT_PTR address);

// clang-format off

DEUS_API void DEUS_CALL deus_scan_memory(
  PSLIST_HEADER memory,
  const BYTE* scan,
  const BYTE* mask,
  SIZE_T size,
  SIZE_T count,
  deus_scan_callback callback,
  PVOID context);

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
  PVOID context);

// clang-format on

}  // extern "C"

#endif  // DEUS_DRIVER

namespace deus {
namespace io {

#ifndef BYTE
typedef unsigned char BYTE;
#endif

static constexpr UINT_PTR min = 0x00000000'00400000;
static constexpr UINT_PTR max = 0x000F0000'00000000;

// clang-format off
enum class code : ULONG {
  version  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  open     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  close    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  poll     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  read     = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  write    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  query    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  keyboard = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  mouse    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
  delay    = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_SPECIAL_ACCESS),
};
// clang-format on

struct alignas(16) version {
  USHORT major;
  USHORT minor;
  USHORT patch;
};

struct alignas(16) event : SLIST_ENTRY {
  enum class type : UINT64 {
    process_created = 0,
    process_terminated = 1,
    image_loaded = 2,
    log = 3,
  } type;

  HANDLE pid;
  UINT_PTR image_base;
  UINT_PTR image_size;
  SIZE_T path_size;

  WCHAR* data() noexcept {
    return reinterpret_cast<WCHAR*>(this + 1);
  }

  const WCHAR* data() const noexcept {
    return reinterpret_cast<const WCHAR*>(this + 1);
  }

  constexpr SIZE_T size() const noexcept {
    return path_size;
  }
};

struct alignas(16) copy {
  HANDLE pid;
  UINT_PTR src;
  UINT_PTR dst;
  SIZE_T size;
};

struct alignas(16) query {
  HANDLE pid;
  UINT_PTR min;
  UINT_PTR max;
  PSLIST_HEADER memory;
};

struct alignas(16) region : SLIST_ENTRY {
  UINT_PTR base_address;
  UINT_PTR allocation_base;
  ULONG allocation_protect;
  SIZE_T region_size;
  ULONG state;
  ULONG protect;
  ULONG type;
  SIZE_T copy;

  BYTE* data() noexcept {
    return reinterpret_cast<BYTE*>(this + 1);
  }

  const BYTE* data() const noexcept {
    return reinterpret_cast<const BYTE*>(this + 1);
  }

  constexpr SIZE_T size() const noexcept {
    return copy;
  }
};

}  // namespace io

#ifndef DEUS_DRIVER

template <typename T, typename V>
inline constexpr T pointer_cast(V value) noexcept {
  using std::uintptr_t;
  using std::is_pointer_v;
  using std::is_integral_v;
  using std::is_floating_point_v;
  using std::is_null_pointer_v;

  static_assert(is_pointer_v<T> || (is_integral_v<T> && !is_floating_point_v<T> && sizeof(T) >= sizeof(uintptr_t)));
  static_assert(is_pointer_v<V> || (is_integral_v<V> && !is_floating_point_v<V>) || is_null_pointer_v<V>);

  if constexpr (is_pointer_v<T>) {
    if constexpr (is_pointer_v<V> || is_null_pointer_v<V>) {
      return static_cast<T>(value);
    } else {
      return reinterpret_cast<T>(static_cast<uintptr_t>(value));
    }
  } else {
    if constexpr (is_pointer_v<V> || is_null_pointer_v<V>) {
      return static_cast<T>(reinterpret_cast<uintptr_t>(value));
    } else {
      return static_cast<T>(value);
    }
  }
}

template <typename T>
inline constexpr bool valid(T address) noexcept {
  return pointer_cast<UINT_PTR>(address) >= io::min && pointer_cast<UINT_PTR>(address) <= io::max;
}

const std::error_category& error_category_dword() noexcept;
const std::error_category& error_category_hresult() noexcept;
const std::error_category& error_category_ntstatus() noexcept;

inline constexpr bool is_error(DEUS_ERROR_CODE ec) noexcept {
  return 0x00000000'FFFFFFFF & ec ? true : false;
}

inline constexpr DEUS_ERROR_CODE make_dword_error(DWORD value) noexcept {
  return (static_cast<DEUS_ERROR_CODE>(DEUS_ERROR_CATEGORY_DWORD) << 32) | value;
}

inline std::error_code make_dword_error_code(DWORD value) noexcept {
  return { static_cast<int>(value), error_category_dword() };
}

inline constexpr DEUS_ERROR_CODE make_hresult_error(HRESULT value) noexcept {
  return (static_cast<DEUS_ERROR_CODE>(DEUS_ERROR_CATEGORY_HRESULT) << 32) | value;
}

inline std::error_code make_hresult_error_code(HRESULT value) noexcept {
  return { static_cast<int>(value), error_category_hresult() };
}

inline constexpr DEUS_ERROR_CODE make_ntstatus_error(NTSTATUS value) noexcept {
  return (static_cast<DEUS_ERROR_CODE>(DEUS_ERROR_CATEGORY_NTSTATUS) << 32) | value;
}

inline std::error_code make_ntstatus_error_code(NTSTATUS value) noexcept {
  return { static_cast<int>(value), error_category_ntstatus() };
}

inline std::error_code make_error_code(DEUS_ERROR_CODE ec) noexcept {
  if (const auto value = static_cast<int>(0x00000000'FFFFFFFF & ec)) {
    switch (static_cast<DEUS_ERROR_CATEGORY>(ec >> 32)) {
    case DEUS_ERROR_CATEGORY_DWORD:
      return { value, error_category_dword() };
    case DEUS_ERROR_CATEGORY_HRESULT:
      return { value, error_category_hresult() };
    case DEUS_ERROR_CATEGORY_NTSTATUS:
      return { value, error_category_ntstatus() };
    }
  }
  return {};
}

inline std::error_code load(PCWSTR cmd, BOOL reload = FALSE) noexcept {
  return make_error_code(deus_load_cmd(cmd, reload));
}

inline std::error_code load(int argc, char* argv[], bool reload = false) noexcept {
  return make_error_code(deus_load_args(argc, argv, reload ? TRUE : FALSE));
}

inline NTSTATUS control(HANDLE device, io::code code) noexcept {
  // clang-format off
  DWORD size = 0;
  if (!DeviceIoControl(device, static_cast<ULONG>(code), nullptr, 0, nullptr, 0, &size, nullptr)) [[unlikely]] {
    return static_cast<NTSTATUS>(GetLastError());
  }
  // clang-format on
  return {};
}

template <typename T>
inline NTSTATUS control(HANDLE device, io::code code, T& buffer) noexcept {
  // clang-format off
  DWORD size = 0;
  if (!DeviceIoControl(device, static_cast<ULONG>(code), &buffer, sizeof(buffer), &buffer, sizeof(buffer), &size, nullptr)) [[unlikely]] {
    return static_cast<NTSTATUS>(GetLastError());
  }
  if (size != sizeof(buffer)) [[unlikely]] {
    return STATUS_BUFFER_TOO_SMALL;
  }
  // clang-format on
  return {};
}

template <typename T>
class iterator {
public:
  using iterator_category = std::input_iterator_tag;
  using difference_type = std::size_t;
  using value_type = T;
  using reference = value_type&;
  using pointer = value_type*;

  constexpr iterator() noexcept = default;
  constexpr iterator(PSLIST_ENTRY entry) noexcept : entry_(entry) {
  }

  constexpr bool operator==(const iterator& other) const noexcept {
    return entry_ == other.entry_;
  }

  constexpr bool operator!=(const iterator& other) const noexcept {
    return !(*this == other);
  }

  constexpr iterator& operator++() noexcept {
    entry_ = entry_->Next;
    return *this;
  }

  iterator operator++(int) = delete;

  constexpr reference operator*() noexcept {
    return *operator->();
  }

  constexpr pointer operator->() noexcept {
    return static_cast<T*>(entry_);
  }

private:
  PSLIST_ENTRY entry_ = nullptr;
};

template <typename T>
class list {
public:
  list() noexcept {
    header_ = static_cast<PSLIST_HEADER>(_aligned_malloc(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT));
    if (header_) {
      InitializeSListHead(header_);
    }
  }

  list(list&& other) noexcept : header_(other.header_) {
    other.header_ = nullptr;
  }

  list(const list& other) = delete;

  list& operator=(list&& other) noexcept {
    clear();
    if (header_) {
      _aligned_free(header_);
    }
    header_ = other.header_;
    other.header_ = nullptr;
    return *this;
  }

  list& operator=(const list& other) = delete;

  ~list() {
    clear();
    if (header_) {
      _aligned_free(header_);
    }
  }

  iterator<T> begin() const noexcept {
    if (header_) {
      if (const auto entry = InterlockedPopEntrySList(header_)) {
        InterlockedPushEntrySList(header_, entry);
        return entry;
      }
    }
    return {};
  }

  iterator<T> end() const noexcept {
    return {};
  }

  std::size_t size() const noexcept {
    std::size_t size = 0;
    for (const auto& e : *this) {
      size++;
    }
    return size;
  }

  void clear() noexcept {
    if (header_) {
      for (auto entry = InterlockedFlushSList(header_); entry;) {
        const auto next = entry->Next;
        VirtualFree(static_cast<T*>(entry), 0, MEM_RELEASE);
        entry = next;
      }
    }
  }

  constexpr PSLIST_HEADER header() const noexcept {
    return header_;
  }

private:
  PSLIST_HEADER header_ = nullptr;
};

using memory = list<io::region>;

class signature {
public:
  signature(const void* data, SIZE_T size) noexcept : size(size) {
    scan.assign(reinterpret_cast<const BYTE*>(data), reinterpret_cast<const BYTE*>(data) + size);
  }

  signature(std::string_view signature) noexcept : size((signature.size() + 1) / 3) {
    assert((signature.size() + 1) / 3 > 0);
    assert((signature.size() + 1) % 3 == 0);

    scan.resize(size);
    mask.resize(size);

    for (SIZE_T i = 0; i < size; i++) {
      scan[i] = byte_cast(signature[i * 3]) << 4 | byte_cast(signature[i * 3 + 1]);
      mask[i] = mask_cast(signature[i * 3]) << 4 | mask_cast(signature[i * 3 + 1]);
      if (mask[i] != 0xFF) {
        apply = true;
      }
    }
  }

  std::vector<BYTE> scan;
  std::vector<BYTE> mask;
  const SIZE_T size = 0;
  bool apply = false;

private:
  static constexpr BYTE byte_cast(CHAR c) noexcept {
    if (c >= '0' && c <= '9') {
      return static_cast<BYTE>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
      return static_cast<BYTE>(c - 'A' + 0xA);
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<BYTE>(c - 'a' + 0xA);
    }
    assert(c == '?');
    return 0x0;
  }

  static constexpr BYTE mask_cast(CHAR c) noexcept {
    return c == '?' ? 0x0 : 0xF;
  }
};

inline std::vector<UINT_PTR> scan(const memory& memory, const signature& sig, SIZE_T count = 0) {
  std::vector<UINT_PTR> result;
  const auto scan = sig.scan.data();
  const auto mask = sig.apply ? sig.mask.data() : nullptr;
  const deus_scan_callback callback = [](PVOID context, UINT_PTR address) {
    static_cast<std::vector<UINT_PTR>*>(context)->push_back(address);
  };
  deus_scan_memory(memory.header(), scan, mask, sig.size, count, callback, &result);
  return result;
}

inline std::vector<UINT_PTR> scan(const memory& memory, const std::string_view sig, SIZE_T count = 0) {
  return scan(memory, deus::signature(sig), count);
}

class device {
public:
  constexpr device() noexcept = default;

  device(device&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }

  device(const device& other) = delete;

  device& operator=(device&& other) noexcept {
    destroy();
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
    return *this;
  }

  device& operator=(const device& other) = delete;

  ~device() {
    destroy();
  }

  std::error_code create() noexcept {
    return make_error_code(deus_device_create(&handle_));
  }

  std::error_code destroy() noexcept {
    const auto ec = deus_device_destroy(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return make_error_code(ec);
  }

  std::error_code control(io::code code) const noexcept {
    return make_ntstatus_error_code(deus::control(handle_, code));
  }

  template <typename T>
  std::error_code control(io::code code, T& message) const noexcept {
    return make_ntstatus_error_code(deus::control(handle_, code, message));
  }

  template <typename Address, typename... Offsets>
  std::error_code read(HANDLE pid, PVOID data, SIZE_T size, Address address, Offsets... offsets) const noexcept {
    io::copy copy;
    copy.pid = pid;
    copy.src = pointer_cast<UINT_PTR>(address);
    for (const auto offset : std::initializer_list<UINT_PTR>{ pointer_cast<UINT_PTR>(offsets)... }) {
      UINT_PTR src = 0;
      copy.dst = reinterpret_cast<UINT_PTR>(&src);
      copy.size = sizeof(UINT_PTR);
      if (const auto ec = control(io::code::read, copy)) {
        return ec;
      }
      copy.src = src + offset;
    }
    copy.dst = reinterpret_cast<UINT_PTR>(data);
    copy.size = size;
    return control(io::code::read, copy);
  }

  template <typename Address, typename... Offsets>
  std::error_code write(HANDLE pid, PVOID data, SIZE_T size, Address address, Offsets... offsets) const noexcept {
    io::copy copy;
    copy.pid = pid;
    copy.dst = pointer_cast<UINT_PTR>(address);
    for (const auto offset : std::initializer_list<UINT_PTR>{ pointer_cast<UINT_PTR>(offsets)... }) {
      UINT_PTR src = 0;
      copy.src = copy.dst;
      copy.dst = reinterpret_cast<UINT_PTR>(&src);
      copy.size = sizeof(UINT_PTR);
      if (const auto ec = control(io::code::read, copy)) {
        return ec;
      }
      copy.dst = src + offset;
    }
    copy.src = reinterpret_cast<UINT_PTR>(data);
    copy.size = size;
    return control(io::code::write, copy);
  }

  std::error_code info(HANDLE pid, memory& memory, UINT_PTR min = io::min, UINT_PTR max = io::max) const noexcept {
    io::query query;
    query.pid = pid;
    query.min = min;
    query.max = max;
    query.memory = memory.header();
    return control(io::code::query, query);
  }

  std::error_code copy(HANDLE pid, memory& memory, UINT_PTR min = io::min, UINT_PTR max = io::max) const noexcept {
    deus::memory result;
    for (auto& src : memory) {
      const auto size = sizeof(io::region) + src.region_size;
      const auto data = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (!data) {
        return make_dword_error_code(GetLastError());
      }
#  pragma warning(disable : 6386)
      const auto dst = new (data) io::region(src);
#  pragma warning(default : 6386)
      if (const auto ec = read(pid, dst->data(), dst->region_size, dst->base_address); !ec) {
        dst->copy = dst->region_size;
        InterlockedPushEntrySList(result.header(), dst);
      } else {
        VirtualFree(data, 0, MEM_RELEASE);
      }
    }
    memory = std::move(result);
    return {};
  }

  std::vector<UINT_PTR> scan(HANDLE pid, const signature& sig, SIZE_T count = 0, UINT_PTR min = io::min, UINT_PTR max = io::max) const {
    std::vector<UINT_PTR> result;
    const auto scan = sig.scan.data();
    const auto mask = sig.apply ? sig.mask.data() : nullptr;
    const deus_scan_callback callback = [](PVOID context, UINT_PTR address) {
      static_cast<std::vector<UINT_PTR>*>(context)->push_back(address);
    };
    deus_scan_process(handle_, pid, scan, mask, sig.size, count, min, max, callback, &result);
    return result;
  }

  std::vector<UINT_PTR> scan(HANDLE pid, const std::string_view sig, SIZE_T count = 0, UINT_PTR min = io::min, UINT_PTR max = io::max) const {
    return scan(pid, signature(sig), count, min, max);
  }

  std::error_code simulate(KEYBOARD_INPUT_DATA& data) const noexcept {
    return control(io::code::keyboard, data);
  }

  std::error_code simulate(MOUSE_INPUT_DATA& data) const noexcept {
    return control(io::code::mouse, data);
  }

  std::error_code delay(LONG offset) const noexcept {
    return control(io::code::delay, offset);
  }

  constexpr HANDLE handle() const noexcept {
    return handle_;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class events : public list<io::event> {
public:
  events() noexcept = default;

  events(events&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }

  events(const events& other) = delete;

  events& operator=(events&& other) noexcept {
    close();
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
    return *this;
  }

  events& operator=(const events& other) = delete;

  ~events() {
    close();
  }

  std::error_code open(device& device) noexcept {
    if (!header()) {
      return make_dword_error_code(ERROR_NOT_ENOUGH_MEMORY);
    }
    if (handle_ != INVALID_HANDLE_VALUE) {
      return make_dword_error_code(ERROR_INVALID_HANDLE);
    }
    const auto handle = device.handle();
    if (handle == INVALID_HANDLE_VALUE) {
      return make_dword_error_code(ERROR_INVALID_HANDLE);
    }
    if (const auto ec = make_ntstatus_error_code(control(handle, io::code::open))) {
      return ec;
    }
    handle_ = handle;
    return {};
  }

  std::error_code close() noexcept {
    if (handle_ != INVALID_HANDLE_VALUE) {
      const auto ec = make_ntstatus_error_code(control(handle_, io::code::close));
      for (const auto& e : *this) {
      }
      handle_ = INVALID_HANDLE_VALUE;
      return ec;
    }
    return {};
  }

  std::error_code poll() noexcept {
    return make_ntstatus_error_code(control(handle_, io::code::poll, *header()));
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

namespace detail {

class error_category_dword : public std::error_category {
public:
  const char* name() const noexcept override {
    return "dword";
  }

  std::string message(int value) const override {
    LPTSTR buffer = nullptr;
    // clang-format off
    const auto length = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      static_cast<DWORD>(value), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPTSTR>(&buffer), 0, nullptr);
    // clang-format on
    if (buffer && length) {
      std::string message;
      auto size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
      message.resize(static_cast<std::size_t>(size));
      WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), message.data(), size, nullptr, nullptr);
      LocalFree(buffer);
      if (const auto pos = message.find_first_of(".\r\n"); pos != std::string::npos) {
        message.erase(pos);
      }
      if (!message.empty()) {
        message.push_back('.');
      }
      return message;
    }
    return "Unknown DWORD value.";
  }
};

class error_category_hresult : public std::error_category {
public:
  const char* name() const noexcept override {
    return "hresult";
  }

  std::string message(int value) const override {
    LPTSTR buffer = nullptr;
    // clang-format off
    const auto length = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      static_cast<DWORD>(value), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPTSTR>(&buffer), 0, nullptr);
    // clang-format on
    if (buffer && length) {
      std::string message;
      auto size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
      message.resize(static_cast<std::size_t>(size));
      WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), message.data(), size, nullptr, nullptr);
      LocalFree(buffer);
      if (const auto pos = message.find_first_of(".\r\n"); pos != std::string::npos) {
        message.erase(pos);
      }
      if (!message.empty()) {
        message.push_back('.');
      }
      return message;
    }
    return "Unknown HRESULT value.";
  }
};

class error_category_ntstatus : public std::error_category {
public:
  const char* name() const noexcept override {
    return "ntstatus";
  }

  std::string message(int value) const override {
    LPTSTR buffer = nullptr;
    static const auto ntdll = LoadLibrary(L"ntdll.dll");
    // clang-format off
    const auto length = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
      ntdll, static_cast<DWORD>(value), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPTSTR>(&buffer), 0, nullptr);
    // clang-format on
    if (buffer && length) {
      std::string message;
      auto size = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
      message.resize(static_cast<std::size_t>(size));
      WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), message.data(), size, nullptr, nullptr);
      LocalFree(buffer);
      if (!message.empty() && message[0] == '{') {
        message.erase(0, 1);
      }
      if (const auto pos = message.find_first_of("}.\r\n"); pos != std::string::npos) {
        message.erase(pos);
      }
      if (!message.empty()) {
        message.push_back('.');
      }
      return message;
    }
    return "Unknown NTSTATUS value.";
  }
};

}  // namespace detail

inline const std::error_category& error_category_dword() noexcept {
  static const detail::error_category_dword category;
  return category;
}

inline const std::error_category& error_category_hresult() noexcept {
  static const detail::error_category_hresult category;
  return category;
}

inline const std::error_category& error_category_ntstatus() noexcept {
  static const detail::error_category_ntstatus category;
  return category;
}

#endif  // DEUS_DRIVER

}  // namespace deus
