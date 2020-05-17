#include "com.hpp"
#include <deus.hpp>
#include <fmt/format.h>
#include <version.h>
#include <string>
#include <string_view>
#include <cstdlib>

HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

class MemoryFontResource : public IMemoryFontResource {
public:
  HRESULT __stdcall Load(HMODULE module, LPCWSTR name, LPCWSTR type) noexcept {
    if (const auto resource = FindResource(module, name, type)) {
      if (const auto handle = LoadResource(module, resource)) {
        data_ = LockResource(handle);
        size_ = static_cast<UINT32>(SizeofResource(module, resource));
        return S_OK;
      }
    }
    return E_FAIL;
  }

  ULONG __stdcall AddRef() noexcept override {
    return InterlockedIncrement(&references_);
  }

  ULONG __stdcall Release() noexcept override {
    const auto count = InterlockedDecrement(&references_);
    if (count == 0) {
      delete this;
    }
    return count;
  }

  HRESULT __stdcall QueryInterface(const IID& id, void** object) noexcept override {
    if (id == __uuidof(IMemoryFontResource) || id == __uuidof(IUnknown)) {
      *object = static_cast<IMemoryFontResource*>(this);
    } else {
      *object = nullptr;
      return E_NOINTERFACE;
    }
    static_cast<IUnknown*>(*object)->AddRef();
    return S_OK;
  }

  virtual LPVOID __stdcall Data() noexcept override {
    return data_;
  }

  virtual UINT32 __stdcall Size() noexcept override {
    return size_;
  }

private:
  LONG references_ = 1;
  LPVOID data_ = nullptr;
  UINT32 size_ = 0;
};

HRESULT CreateMemoryFontResource(HMODULE module, LPCWSTR name, LPCWSTR type, IMemoryFontResource** res) {
  if (!res) {
    return E_INVALIDARG;
  }
  const auto resource = new MemoryFontResource();
  const auto hr = resource->Load(module, name, type);
  if (SUCCEEDED(hr)) {
    *res = resource;
  } else {
    delete resource;
  }
  return hr;
}

void ShowError(LPCWSTR string) {
  if (string) {
    MessageBox(nullptr, string, TEXT(PROJECT_DESCRIPTION " Error"), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  }
}

void ShowError(HRESULT status, LPCWSTR file, UINT line, ShowErrorAction action) {
  ShowError(deus::make_ntstatus_error_code(status), file, line, action);
}

void ShowError(std::error_code ec, LPCWSTR file, UINT line, ShowErrorAction action) {
  if (ec || action == ShowErrorAction::Exit) {
    std::wstring_view src(file);
    if (const auto pos = src.rfind(L"\\src\\"); pos != src.npos) {
      src = src.substr(pos + 5);
    }
    const auto value = static_cast<unsigned>(ec.value());
    const auto str = fmt::format(L"Error 0x{:08X} ({}:{})\r\n\r\n{}", value, src, line, S2W(ec.message()));
#ifdef _DEBUG
    if (IsDebuggerPresent()) {
      OutputDebugString(str.data());
      assert(false);
    }
#endif
    ShowError(str.data());
    if (action != ShowErrorAction::None) {
      ExitProcess(ec.value());
    }
  }
}