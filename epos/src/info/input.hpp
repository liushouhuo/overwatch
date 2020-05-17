#pragma once
#include "info.hpp"

deus::device* g_device = nullptr;

class KeyboardHook {
public:
  constexpr KeyboardHook() noexcept = default;

  KeyboardHook(KeyboardHook&& other) = delete;
  KeyboardHook(const KeyboardHook& other) = delete;
  KeyboardHook& operator=(KeyboardHook&& other) = delete;
  KeyboardHook& operator=(const KeyboardHook& other) = delete;

  ~KeyboardHook() {
    destroy();
  }

  std::error_code create() noexcept {
    if (hook_) {
      return deus::make_dword_error_code(ERROR_INVALID_HANDLE);
    }
    hook_ = SetWindowsHookEx(WH_KEYBOARD_LL, handle, nullptr, 0);
    if (!hook_) {
      return deus::make_dword_error_code(GetLastError());
    }
    return {};
  }

  std::error_code destroy() noexcept {
    if (const auto hook = hook_) {
      hook_ = nullptr;
      if (!UnhookWindowsHookEx(hook)) {
        return deus::make_dword_error_code(GetLastError());
      }
    }
    return {};
  }

  static LRESULT CALLBACK handle(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0) {
      fmt::print("k: ");
      const auto ks = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
      if (ks) {
        fmt::print("0x{:X} 0x{:X} ", ks->vkCode, ks->scanCode);
      }

      switch (wparam) {
      case WM_KEYDOWN:
        fmt::print("WM_KEYDOWN");
        break;
      case WM_KEYUP:
        fmt::print("WM_KEYUP");
        break;
      case WM_SYSKEYDOWN:
        fmt::print("WM_SYSKEYDOWN");
        break;
      case WM_SYSKEYUP:
        fmt::print("WM_SYSKEYUP");
        break;
      default:
        fmt::print("0x{:X} ({})", wparam, wparam);
        break;
      }

      if (ks) {
        if (ks->flags & LLKHF_EXTENDED) {
          fmt::print(" (LLKHF_EXTENDED)");
        }
        if (ks->flags & LLKHF_LOWER_IL_INJECTED) {
          fmt::print(" (LLKHF_LOWER_IL_INJECTED)");
        }
        if (ks->flags & LLKHF_INJECTED) {
          fmt::print(" (LLKHF_INJECTED)");
        }
        if (ks->flags & LLKHF_ALTDOWN) {
          fmt::print(" (LLKHF_ALTDOWN)");
        }
        if (ks->flags & LLKHF_UP) {
          fmt::print(" (LLKHF_UP)");
        }
      }

      fmt::print("\n");
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

private:
  HHOOK hook_ = nullptr;
};


class MouseHook {
public:
  constexpr MouseHook() noexcept = default;

  MouseHook(MouseHook&& other) = delete;
  MouseHook(const MouseHook& other) = delete;
  MouseHook& operator=(MouseHook&& other) = delete;
  MouseHook& operator=(const MouseHook& other) = delete;

  ~MouseHook() {
    destroy();
  }

  std::error_code create() noexcept {
    if (hook_) {
      return deus::make_dword_error_code(ERROR_INVALID_HANDLE);
    }
    hook_ = SetWindowsHookEx(WH_MOUSE_LL, handle, nullptr, 0);
    if (!hook_) {
      return deus::make_dword_error_code(GetLastError());
    }
    return {};
  }

  std::error_code destroy() noexcept {
    if (const auto hook = hook_) {
      hook_ = nullptr;
      if (!UnhookWindowsHookEx(hook)) {
        return deus::make_dword_error_code(GetLastError());
      }
    }
    return {};
  }

  static LRESULT CALLBACK handle(int code, WPARAM wparam, LPARAM lparam) {
    if (wparam == WM_MOUSEWHEEL) {
      //MOUSE_INPUT_DATA mid = {};
      //mid.Flags |= MOUSE_MOVE_RELATIVE;
      //mid.ButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;
      //if (const auto ec = g_device->simulate(mid)) {
      //  fmt::print("Could not simulate mouse event: {}\n", ec.message());
      //}
      //Sleep(52);
      //mid.ButtonFlags &= ~MOUSE_LEFT_BUTTON_DOWN;
      //mid.ButtonFlags |= MOUSE_LEFT_BUTTON_UP;
      //if (const auto ec = g_device->simulate(mid)) {
      //  fmt::print("Could not simulate mouse event: {}\n", ec.message());
      //}

      //KEYBOARD_INPUT_DATA kid = {};
      //kid.MakeCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
      //kid.Flags = KEY_MAKE;
      //if (const auto ec = g_device->simulate(kid)) {
      //  fmt::print("Could not simulate keyboard event: {}\n", ec.message());
      //}
      //Sleep(52);
      //kid.Flags = KEY_BREAK;
      //if (const auto ec = g_device->simulate(kid)) {
      //  fmt::print("Could not simulate keyboard event: {}\n", ec.message());
      //}
    }
    if (code >= 0 && wparam != WM_MOUSEMOVE && wparam != WM_MOUSEWHEEL) {
      fmt::print("m: ");
      const auto ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
      if (ms) {
        fmt::print("{} ", HIWORD(ms->mouseData));
      }

      switch (wparam) {
      case WM_LBUTTONDOWN:
        fmt::print("WM_LBUTTONDOWN");
        break;
      case WM_LBUTTONUP:
        fmt::print("WM_LBUTTONUP");
        break;
      case WM_RBUTTONDOWN:
        fmt::print("WM_RBUTTONDOWN");
        break;
      case WM_RBUTTONUP:
        fmt::print("WM_RBUTTONUP");
        break;
      case WM_MBUTTONDOWN:
        fmt::print("WM_MBUTTONDOWN");
        break;
      case WM_MBUTTONUP:
        fmt::print("WM_MBUTTONUP");
        break;
      case WM_XBUTTONDOWN:
        fmt::print("WM_XBUTTONDOWN");
        break;
      case WM_XBUTTONUP:
        fmt::print("WM_XBUTTONUP");
        break;
      case WM_NCXBUTTONDOWN:
        fmt::print("WM_NCXBUTTONDOWN");
        break;
      case WM_NCXBUTTONUP:
        fmt::print("WM_NCXBUTTONUP");
        break;
      default:
        fmt::print("0x{:X} ({})", wparam, wparam);
        break;
      }

      if (ms) {
        if (ms->flags & LLMHF_INJECTED) {
          fmt::print(" (LLMHF_INJECTED)");
        }
        if (ms->flags & LLMHF_LOWER_IL_INJECTED) {
          fmt::print(" (LLMHF_LOWER_IL_INJECTED)");
        }
      }

      fmt::print("\n");
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

private:
  HHOOK hook_ = nullptr;
};

void info(deus::device& device) {
  // Pressing RETURN yields:
  // k: 0xD 0x1C WM_KEYDOWN
  // k: 0xD 0x1C WM_KEYUP (LLKHF_UP)

  g_device = &device;
  KeyboardHook keyboard;
  if (const auto ec = keyboard.create()) {
    fmt::print("Could not create keyboard hook: 0x{0:X} ({0})\n", ec.value(), ec.message());
  }

  MouseHook mouse;
  if (const auto ec = mouse.create()) {
    fmt::print("Could not create mouse hook: 0x{0:X} ({0})\n", ec.value(), ec.message());
  }

  MSG msg = {};
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
