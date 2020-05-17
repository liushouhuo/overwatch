#pragma once
#include <com.hpp>
#include <deus.hpp>
#include <ring.hpp>
#include <d2d1.h>
#include <d2d1_2.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <fmt/format.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <cstddef>
#include <cstdint>

#include <DirectXMath.h>
using namespace DirectX;

struct alignas(16) Entity {
  std::array<std::byte, 0x24> block0;
  XMFLOAT4 p[2];
  std::array<std::byte, 0x78> block1;
  enum class Team : std::uint8_t {
    One = 0x08,
    Two = 0x10,
  } team;
  std::array<std::byte, 0x02> block2;
  enum class State : std::uint8_t {
    Alive = 0x14,
  } state;
};

class View {
public:
  using clock = std::chrono::high_resolution_clock;

  View() = default;

  View(View&& other) = delete;
  View(const View& other) = delete;
  View& operator=(View&& other) = delete;
  View& operator=(const View& other) = delete;

  virtual ~View() = default;

  void Create(ComPtr<ID2D1DeviceContext> dc);
  void Destroy() noexcept;
  void Update() noexcept;

  bool DrawEntity(const XMFLOAT4X4& vm, XMVECTOR pc, XMVECTOR p0, XMVECTOR p1, XMVECTOR p2, float line, ID2D1SolidColorBrush* color);

  void DrawText(std::wstring_view text, float x, float y, float cx, float cy, IDWriteTextFormat* format, ID2D1SolidColorBrush* color);
  void DrawText(const fmt::wmemory_buffer& text, float x, float y, float cx, float cy, IDWriteTextFormat* format, ID2D1SolidColorBrush* color);

  template <typename... Args>
  void Report(PCWSTR format, Args&&... args) {
    fmt::format_to(report_.text, format, std::forward<Args>(args)...);
  }

  void Report(ComPtr<ID2D1SolidColorBrush> color) {
    report_.color = color;
  }

  void Report(std::error_code ec) {
    const auto value = static_cast<unsigned>(ec.value());
    fmt::format_to(report_.text, L"Error: 0x{:08X}\n{}\n", value, S2W(ec.message()));
  }

  template <typename... Args>
  void Status(PCWSTR format, Args&&... args) {
    status_.text.clear();
    fmt::format_to(status_.text, format, std::forward<Args>(args)...);
  }

  void Status(ComPtr<ID2D1SolidColorBrush> color) {
    status_.color = color;
  }

  void ReportBinary(SIZE_T index, PVOID data, SIZE_T size);

  virtual HRESULT CreateTextFormat(LPCWSTR name, FLOAT size, BOOL bold, IDWriteTextFormat** format) = 0;

protected:
  void OnProcessCreate(HANDLE pid, std::wstring_view name) noexcept;
  void OnProcessDestroy(HANDLE pid, std::wstring_view name) noexcept;
  void OnImageLoad(HANDLE pid, std::wstring_view name, UINT_PTR base, UINT_PTR size) noexcept;
  void OnKeyboard(UINT message, KBDLLHOOKSTRUCT* event) noexcept;
  void OnMouse(UINT message, MSLLHOOKSTRUCT* event) noexcept;

  std::error_code Simulate(USHORT button) noexcept {
    MOUSE_INPUT_DATA mid = {};
    mid.Flags |= MOUSE_MOVE_RELATIVE;
    mid.ButtonFlags |= button;
    return deus_.simulate(mid);
  }

  std::error_code Simulate(UINT code, USHORT flags) noexcept {
    KEYBOARD_INPUT_DATA kid = {};
    kid.MakeCode = MapVirtualKey(code, MAPVK_VK_TO_VSC);
    kid.Flags = flags;
    return deus_.simulate(kid);
  }

  deus::device deus_;

  FLOAT cx_ = 1.0f;
  FLOAT cy_ = 1.0f;

  struct color {
    ComPtr<ID2D1SolidColorBrush> red;
    ComPtr<ID2D1SolidColorBrush> green;
    ComPtr<ID2D1SolidColorBrush> blue;
    ComPtr<ID2D1SolidColorBrush> black;
    ComPtr<ID2D1SolidColorBrush> white;
  } color_;

  struct format {
    ComPtr<IDWriteTextFormat> regular;
    ComPtr<IDWriteTextFormat> numeric;
    ComPtr<IDWriteTextFormat> monospace;
  } format_;

  struct status {
    fmt::wmemory_buffer text;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<ID2D1SolidColorBrush> color;
  } status_;

  struct report {
    fmt::wmemory_buffer text;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<ID2D1SolidColorBrush> color;
  } report_;

private:
  ComPtr<ID2D1DeviceContext> dc_;
  HANDLE device_ = nullptr;

  HANDLE pid_ = nullptr;
  UINT_PTR exe_ = 0;

  std::mutex mutex_;
  std::vector<std::uintptr_t> entities_;

  Entity::Team player_team_ = Entity::Team::One;

  std::atomic_bool stop_ = false;
  std::thread thread_;

  std::atomic_bool trigger_ = false;
  std::atomic_bool special_ = false;
  std::atomic_bool scope_ = false;

  std::atomic_bool attack_ = false;
  std::atomic_bool sleep_ = false;

  std::atomic_int execute_ = 0;
  clock::time_point execute_start_;

  std::optional<clock::time_point> delay_;
};