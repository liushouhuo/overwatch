#include <com.hpp>
#include <deus.hpp>
#include <view.hpp>
#include <shellscalingapi.h>
#include <version.h>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <cstdlib>

class Window : public View {
public:
  static constexpr auto class_name = TEXT(PROJECT_VENDOR PROJECT_DESCRIPTION PROJECT_VERSION);
  static constexpr auto title_text = TEXT(PROJECT_DESCRIPTION);

  Window(HINSTANCE hinstance) : hinstance_(hinstance) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
      auto window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
      if (msg == WM_CREATE) {
        window = reinterpret_cast<Window*>(reinterpret_cast<LPCREATESTRUCT>(lparam)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
      } else if (msg == WM_DESTROY) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
      }
      return window ? window->handle(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
    };
    wc.hInstance = hinstance_;
    wc.hIcon = wc.hIconSm = LoadIcon(hinstance_, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOWFRAME);
    wc.lpszClassName = class_name;
    RegisterClassEx(&wc);
  }

  Window(Window&& other) = delete;
  Window(const Window& other) = delete;
  Window& operator=(Window&& other) = delete;
  Window& operator=(const Window& other) = delete;

  ~Window() {
    UnregisterClass(class_name, hinstance_);
  }

  HWND Create(int x, int y, int cx, int cy) {
    auto ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    ex |= WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_LAYERED;
    return CreateWindowEx(ex, class_name, title_text, WS_POPUP, x, y, cx, cy, nullptr, nullptr, hinstance_, this);
  }

  HRESULT CreateTextFormat(LPCWSTR name, FLOAT size, BOOL bold, IDWriteTextFormat** format) override {
    const auto weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    constexpr auto style = DWRITE_FONT_STYLE_NORMAL;
    constexpr auto stretch = DWRITE_FONT_STRETCH_NORMAL;
    return factory_->CreateTextFormat(name, fonts_.Get(), weight, style, stretch, size, L"en-US", format);
  }

  constexpr HWND hwnd() const noexcept {
    return hwnd_;
  }

private:
  void OnCreate() {
    // Resize window.
    RECT rc = { 0, 0, 100, 100 };
    if (const auto monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONULL)) {
      MONITORINFO mi = {};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfo(monitor, &mi)) {
        rc = mi.rcMonitor;
      }
    }
    const auto cx = rc.right - rc.left;
    const auto cy = rc.bottom - rc.top;
    SetWindowPos(hwnd_, nullptr, rc.left, rc.top, cx, cy, SWP_NOACTIVATE);

    // Create swap chain.
    ComPtr<ID3D11Device> d3d_device;
    HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
      D3D11_SDK_VERSION, &d3d_device, nullptr, nullptr));

    ComPtr<IDXGIDevice> dxgi_device;
    HR(d3d_device.As(&dxgi_device));

    ComPtr<IDXGIFactory2> dxgi_factory;
    HR(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(dxgi_factory), &dxgi_factory));

    DXGI_SWAP_CHAIN_DESC1 description = {};
    description.Width = static_cast<decltype(description.Width)>(cx);
    description.Height = static_cast<decltype(description.Height)>(cy);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.BufferCount = 2;
    description.SampleDesc.Count = 1;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    HR(dxgi_factory->CreateSwapChainForComposition(dxgi_device.Get(), &description, nullptr, &sc_));

    // Create device context.
    ComPtr<ID2D1Factory2> d2d_factory;
    HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, { D2D1_DEBUG_LEVEL_INFORMATION }, d2d_factory.GetAddressOf()));

    ComPtr<ID2D1Device1> d2d_device;
    HR(d2d_factory->CreateDevice(dxgi_device.Get(), d2d_device.GetAddressOf()));
    HR(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_));

    // Create bitmap render target for swap chain.
    ComPtr<IDXGISurface2> surface;
    HR(sc_->GetBuffer(0, __uuidof(surface), &surface));

    D2D1_BITMAP_PROPERTIES1 buffer_properties = {};
    buffer_properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    buffer_properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    buffer_properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    HR(dc_->CreateBitmapFromDxgiSurface(surface.Get(), buffer_properties, &buffer_));
    dc_->SetTarget(buffer_.Get());

    // Enable composition.
    HR(DCompositionCreateDevice(dxgi_device.Get(), __uuidof(composition_device_), &composition_device_));
    HR(composition_device_->CreateTargetForHwnd(hwnd_, true, &composition_target_));
    HR(composition_device_->CreateVisual(&composition_visual_));
    HR(composition_visual_->SetContent(sc_.Get()));
    HR(composition_target_->SetRoot(composition_visual_.Get()));
    HR(composition_device_->Commit());

    // Create DirectWrite objects.
    HR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(factory_), &factory_));

    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    HR(factory_->CreateInMemoryFontFileLoader(&loader));
    HR(factory_->RegisterFontFileLoader(loader.Get()));

    ComPtr<IDWriteFontSetBuilder2> builder;
    HR(factory_->CreateFontSetBuilder(&builder));

    for (const DWORD id : { IDR_FONT_ROBOTO, IDR_FONT_ROBOTO_BLACK, IDR_FONT_ROBOTO_MONO }) {
      ComPtr<IDWriteFontFile> file;
      ComPtr<IMemoryFontResource> font;
      HR(CreateMemoryFontResource(hinstance_, MAKEINTRESOURCE(id), RT_RCDATA, &font));
      HR(loader->CreateInMemoryFontFileReference(factory_.Get(), font->Data(), font->Size(), font.Get(), &file));
      HR(builder->AddFontFile(file.Get()));
    }

    ComPtr<IDWriteFontSet> set;
    HR(builder->CreateFontSet(&set));
    HR(factory_->CreateFontCollectionFromFontSet(set.Get(), DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &fonts_));

    // Create device.
    HR(deus_.create());

    // Create view.
    View::Create(dc_);

    // Start listening for events.
    HR(events_.open(deus_));

    // Show window.
    ShowWindow(hwnd_, SW_SHOW);
    SetLayeredWindowAttributes(hwnd_, 0, 0xFF, LWA_ALPHA);

    // Hook keyboard and mouse events.
    if (thread_.joinable()) {
      stop_ = true;
      thread_.join();
    }
    stop_ = false;
    thread_ = std::thread([this]() {
      static thread_local Window* window = this;
      const auto keyboard = SetWindowsHookEx(WH_KEYBOARD_LL, [](int code, WPARAM wparam, LPARAM lparam) -> LRESULT {
        if (code >= 0) {
          window->OnKeyboard(static_cast<UINT>(wparam), reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam));
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
      }, nullptr, 0);
      const auto mouse = SetWindowsHookEx(WH_MOUSE_LL, [](int code, WPARAM wparam, LPARAM lparam) -> LRESULT {
        if (code >= 0) {
          window->OnMouse(static_cast<UINT>(wparam), reinterpret_cast<MSLLHOOKSTRUCT*>(lparam));
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
      }, nullptr, 0);
      MSG msg = {};
      while (GetMessage(&msg, nullptr, 0, 0) && !stop_.load(std::memory_order_relaxed)) {
        DispatchMessage(&msg);
      }
      UnhookWindowsHookEx(mouse);
      UnhookWindowsHookEx(keyboard);
    });
  }

  void OnDestroy() {
    if (thread_.joinable()) {
      stop_ = true;
      thread_.join();
    }
    View::Destroy();
    events_.close();
    deus_.destroy();
    ShowWindow(hwnd_, SW_HIDE);
    PostQuitMessage(EXIT_SUCCESS);
  }

  void OnPaint() noexcept {
    dc_->BeginDraw();
    dc_->Clear();

    View::Update();

    if (status_.text.size()) {
      DrawText(status_.text, 8.0f, 8.0f, 500.0f, 29.0f, status_.format.Get(), status_.color.Get());
      status_.text.clear();
    }
    if (report_.text.size()) {
      DrawText(report_.text, 8.0f, 29.0f, 500.0f, cy_ - 38.0f, report_.format.Get(), report_.color.Get());
      report_.text.clear();
    }

    HRD(dc_->EndDraw());
    sc_->Present(0, 0);

    HRD(events_.poll());
    auto clear = false;
    for (const auto& event : events_) {
      std::wstring_view path(event.data(), event.size());
      if (const auto pos = path.find_last_of(L'\\'); pos != std::wstring_view::npos) {
        path = path.substr(pos + 1);
      }
      switch (event.type) {
      case deus::io::event::type::process_created:
        View::OnProcessCreate(event.pid, path);
        break;
      case deus::io::event::type::process_terminated:
        View::OnProcessDestroy(event.pid, path);
        break;
      case deus::io::event::type::image_loaded:
        View::OnImageLoad(event.pid, path, event.image_base, event.image_size);
        break;
      }
      clear = true;
    }
    if (clear) {
      events_.clear();
    }
  }

  LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    switch (msg) {
    case WM_CREATE:
      hwnd_ = hwnd;
      OnCreate();
      return 0;
    case WM_DESTROY:
      OnDestroy();
      hwnd_ = nullptr;
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      OnPaint();
      return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }

  HINSTANCE hinstance_ = GetModuleHandle(nullptr);
  HWND hwnd_ = nullptr;

  ComPtr<IDXGISwapChain1> sc_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<ID2D1Bitmap1> buffer_;

  ComPtr<IDCompositionDevice> composition_device_;
  ComPtr<IDCompositionTarget> composition_target_;
  ComPtr<IDCompositionVisual> composition_visual_;

  ComPtr<IDWriteFactory6> factory_;
  ComPtr<IDWriteFontCollection2> fonts_;

  deus::events events_;

  std::thread thread_;
  std::atomic_bool stop_ = false;
};

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, LPWSTR cmd, int show) {
  HR(deus::load(cmd));
  Window window(hinstance);
  if (!window.Create(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT)) {
    ShowError(L"Could not create overlay.");
    return EXIT_FAILURE;
  }

  MSG msg = {};
  while (GetMessage(&msg, nullptr, 0, 0)) {
    DispatchMessage(&msg);
  }
  return static_cast<int>(msg.wParam);
}
