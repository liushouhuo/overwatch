#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <string_view>
#include <system_error>
#include <cassert>

#define HR(status) ShowError(status, TEXT(__FILE__), __LINE__, ShowErrorAction::Check)
#define HRE(status) ShowError(status, TEXT(__FILE__), __LINE__, ShowErrorAction::Exit)

#ifdef _DEBUG
#  define HRD(status) HR(status)
#else
#  define HRD(status) status
#endif

extern HMODULE ntdll;

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct __declspec(uuid("9d7d3183-7cf2-47fb-82c9-98799214c673")) __declspec(novtable) IMemoryFontResource : IUnknown {
  virtual LPVOID __stdcall Data() noexcept = 0;
  virtual UINT32 __stdcall Size() noexcept = 0;
};

HRESULT CreateMemoryFontResource(HMODULE module, LPCWSTR name, LPCWSTR type, IMemoryFontResource** res);

enum class ShowErrorAction {
  None,   // show message and return
  Check,  // show message and exit on error
  Exit,   // show message and exit
};

inline std::wstring S2W(std::string_view str) {
  std::wstring wcs;
  if (const auto ssize = static_cast<int>(str.size())) {
    if (const auto wsize = MultiByteToWideChar(CP_UTF8, 0, str.data(), ssize, nullptr, 0); wsize > 0) {
      wcs.resize(static_cast<std::size_t>(wsize));
      if (!MultiByteToWideChar(CP_UTF8, 0, str.data(), ssize, wcs.data(), wsize)) {
        return {};
      }
    }
  }
  return wcs;
}

inline std::wstring S2W(const char* str, std::size_t size) {
  return S2W({ str, strnlen(str, size) });
}

inline std::string W2S(std::wstring_view wcs) {
  std::string str;
  if (const auto wsize = static_cast<int>(wcs.size())) {
    if (const auto ssize = WideCharToMultiByte(CP_UTF8, 0, wcs.data(), wsize, nullptr, 0, nullptr, nullptr); ssize > 0) {
      str.resize(static_cast<std::size_t>(ssize));
      if (!WideCharToMultiByte(CP_UTF8, 0, wcs.data(), wsize, str.data(), ssize, nullptr, nullptr)) {
        return {};
      }
    }
  }
  return str;
}

inline std::string W2S(const wchar_t* wcs, std::size_t size) {
  return W2S({ wcs, wcsnlen(wcs, size) });
}

void ShowError(LPCWSTR string);
void ShowError(HRESULT result, LPCWSTR file, UINT line, ShowErrorAction action = ShowErrorAction::None);
void ShowError(std::error_code ec, LPCWSTR file, UINT line, ShowErrorAction action = ShowErrorAction::None);