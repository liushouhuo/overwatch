#include "view.hpp"
#include <fstream>

#define EPOS_DEBUG_ENTITIES 0

constexpr auto g_game_cx = 1920.0f;
constexpr auto g_game_cy = 1080.0f;
constexpr auto g_view_cx = 2560.0f;
constexpr auto g_view_cy = 1080.0f;

void View::Create(ComPtr<ID2D1DeviceContext> dc) {
  const auto size = dc->GetSize();
  cx_ = size.width;
  cy_ = size.height;
  dc_ = dc;

  HR(dc->CreateSolidColorBrush(D2D1::ColorF(0xE53935), &color_.red));
  HR(dc->CreateSolidColorBrush(D2D1::ColorF(0x8BC34A), &color_.green));
  HR(dc->CreateSolidColorBrush(D2D1::ColorF(0x2196F3), &color_.blue));
  HR(dc->CreateSolidColorBrush(D2D1::ColorF(0x000000), &color_.black));
  HR(dc->CreateSolidColorBrush(D2D1::ColorF(0xF0F0F0), &color_.white));

  HR(CreateTextFormat(L"Roboto", 14.0f, FALSE, &format_.regular));
  HR(CreateTextFormat(L"Roboto", 24.0f, FALSE, &format_.numeric));
  HR(CreateTextFormat(L"Roboto Mono", 10.0f, FALSE, &format_.monospace));
  format_.regular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

  HR(CreateTextFormat(L"Roboto", 14.0f, FALSE, &status_.format));
  HR(CreateTextFormat(L"Roboto Mono", 10.0f, FALSE, &report_.format));
  status_.color = color_.red;
  report_.color = color_.white;

  if (auto is = std::ifstream("epos.txt", std::ios::binary)) {
    std::string line;
    if (std::getline(is, line) && std::sscanf(line.data(), "%tu", reinterpret_cast<UINT_PTR*>(&pid_)) == 1) {
      if (!std::getline(is, line) || std::sscanf(line.data(), "0x%tX", &exe_) != 1) {
        pid_ = nullptr;
        exe_ = 0;
      }
    }
  }

  if (pid_) {
    deus::memory memory;
    if (const auto ec = deus_.info(pid_, memory, deus::io::min, deus::io::min + 1024 * 1024); ec) {
      const auto message = ec.message();
      pid_ = nullptr;
      exe_ = 0;
      if (auto is = std::ofstream("epos.txt", std::ios::binary)) {
        is << fmt::format("{}\n", reinterpret_cast<UINT_PTR>(pid_));
        is << fmt::format("0x{:016X}\n", exe_);
      }
    }
  }

  Destroy();
  stop_ = false;
  thread_ = std::thread([this]() {
    while (!stop_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (!pid_) {
        std::lock_guard lock(mutex_);
        entities_.clear();
        continue;
      }
      auto addresses = deus_.scan(pid_, "00 00 00 00 ?? ?? 00 00 ?? 00 00 00 ?? 00 00 00 00 00 FF FF 00 01 00 00 02");
      if (addresses.empty()) {
        std::lock_guard lock(mutex_);
        entities_.clear();
        continue;
      }
      std::sort(addresses.begin(), addresses.end());
      std::lock_guard lock(mutex_);
      entities_ = std::move(addresses);
    }
  });
}

void View::Destroy() noexcept {
  stop_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
}

__forceinline std::optional<XMFLOAT2> WorldToScreen(const XMFLOAT4X4& vm, float x, float y, float z) noexcept {
  const auto sw = (vm._14 * x) + (vm._24 * y) + (vm._34 * z + vm._44);
  if (sw < 0.0001f)
    return std::nullopt;

  const auto sx = (vm._11 * x) + (vm._21 * y) + (vm._31 * z + vm._41);
  const auto sy = (vm._12 * x) + (vm._22 * y) + (vm._32 * z + vm._42);

  return XMFLOAT2{
    (g_game_cx / 2) + (g_game_cx / 2) * sx / sw + (g_view_cx - g_game_cx) / 2.0f,
    (g_game_cy / 2) - (g_game_cy / 2) * sy / sw + (g_view_cy - g_game_cy) / 2.0f,
  };
}

void View::Update() noexcept {
  // Wait for target.
  if (!pid_ || !exe_) {
    Status(color_.red);
    Status(L"Waiting for target...");
    std::this_thread::sleep_for(std::chrono::milliseconds(32));
    return;
  }

  // Wait for entities.
  std::lock_guard lock(mutex_);
  if (entities_.empty()) {
    Status(color_.blue);
    Status(L"Waiting for entities...");
    std::this_thread::sleep_for(std::chrono::milliseconds(32));
    return;
  }

  // Reduce CPU load.
  std::this_thread::sleep_for(std::chrono::microseconds(10));

  // Get time point.
  const auto now = clock::now();

  // Read view matrix.
  XMFLOAT4X4 vm;
  if (const auto ec = deus_.read(pid_, &vm, sizeof(vm), exe_ + 0x02DBC830, 0x8, 0x80); ec) {
    Report(ec);
    return;
  }
  const auto vmx = XMLoadFloat4x4(&vm);

  // Calculate camera matrix.
  const auto cmx = XMMatrixInverse(nullptr, vmx);
  XMFLOAT4X4 cm;
  XMStoreFloat4x4(&cm, cmx);

  // Calculate camera point.
  const auto pc = XMVectorSet(cm._41 / 10.0f, cm._42 / 10.0f, cm._43 / 10.0f, 0.0f);

  // Process entity list.
  auto targeting_ally = false;
  auto targeting_enemy = false;
  auto player_distance = 10.0f;
  auto player_team = player_team_;
  for (std::size_t i = 0, max = entities_.size(); i < max; i++) {
    // Read entity.
    Entity entity;
    if (const auto ec = deus_.read(pid_, &entity, sizeof(entity), entities_[i] - sizeof(Entity)); ec) {
#if EPOS_DEBUG_ENTITIES
      Report(L"{:02} not readable ({})\n", i, S2W(ec.message()));
#endif
      continue;
    }

    // Skip dead entities.
    if (entity.state != Entity::State::Alive) {
#if EPOS_DEBUG_ENTITIES
      Report(L"{:02} not alive ({:02X})\n", i, static_cast<std::uint8_t>(entity.state));
#endif
      continue;
    }

    // Hitbox [0,0].
    const auto p1 = XMLoadFloat4(&entity.p[0]);

    // Hitbox [3,3].
    const auto p2 = XMLoadFloat4(&entity.p[1]);

    // Hitbox center.
    const auto p0 = (p1 + p2) / 2;

    // Distance from hitbox center to the camera.
    const auto distance = XMVectorGetX(XMVector3Length(pc - p0));

    // Detect player entity based on distance to camera.
    if (distance < player_distance) {
      player_distance = distance;
      player_team = entity.team;
    }

    // Skip entities which are too close.
    if (distance < 1.0f) {
#if EPOS_DEBUG_ENTITIES
      Report(L"{:02} too close ({:.3f})\n", i, distance);
#endif
      continue;
    }

    // Skip entities which are too far away.
    if (distance > 200.0f) {
#if EPOS_DEBUG_ENTITIES
      Report(L"{:02} too far away ({:.3f})\n", i, distance);
#endif
      continue;
    }

    // Draw circle around the hitbox center point.
    if (DrawEntity(vm, pc, p0, p1, p2, 1.5f, (entity.team == player_team_ ? color_.green : color_.red).Get())) {
      if (entity.team == player_team_) {
        targeting_ally = true;
      } else {
        targeting_enemy = true;
      }
    }

#if EPOS_DEBUG_ENTITIES
    const auto name = fmt::format(L"{:02}", i);
    const auto rc = D2D1::RectF(sp->x - 20.0f, sp->y - 9.5f, sp->x + 20.0f, sp->y + 10.0f);
    dc_->DrawText(name.data(), static_cast<UINT32>(name.size()), format_.regular.Get(), rc, color_.white.Get());
#endif
  }
  player_team_ = player_team;

  // Simulate mouse events.
  if ((trigger_) && (targeting_ally || targeting_enemy)) {
    auto expected = false;
    if (attack_.compare_exchange_strong(expected, true)) {
      if (!delay_) {
        deus_.delay(1);
        delay_ = now;
      }
      Simulate(MOUSE_BUTTON_5_DOWN);
    }
  } else {
    auto expected = true;
    if (attack_.compare_exchange_strong(expected, false)) {
      Simulate(MOUSE_BUTTON_5_UP);
    }
  }

  // Simulate keyboard events.
  if (special_ && targeting_enemy) {
    auto expected = false;
    if (sleep_.compare_exchange_strong(expected, true)) {
      Simulate(VK_LSHIFT, KEY_MAKE);
    }
  } else {
    auto expected = true;
    if (sleep_.compare_exchange_strong(expected, false)) {
      Simulate(VK_LSHIFT, KEY_BREAK);
    }
  }

  // Reset delay.
  if (delay_) {
    if (*delay_ + std::chrono::milliseconds(796) < now) {
      delay_ = std::nullopt;
      deus_.delay(0);
    } else if (*delay_ + std::chrono::milliseconds(100) < now) {
      deus_.delay(0);
    }
  }

  // Perform revenge action.
  switch (execute_.load(std::memory_order_acquire)) {
  case 1: {
    Simulate(MOUSE_BUTTON_5_DOWN);
    execute_start_ = now;
    execute_.store(2, std::memory_order_release);
  } break;
  case 2:
    if (execute_start_ + std::chrono::milliseconds(10) < now) {
      Simulate('V', KEY_MAKE);
      execute_.store(3, std::memory_order_release);
    }
    break;
  case 3:
    if (execute_start_ + std::chrono::milliseconds(20) < now) {
      Simulate(MOUSE_BUTTON_5_UP);
      execute_.store(4, std::memory_order_release);
    }
    break;
  case 4:
    if (execute_start_ + std::chrono::milliseconds(30) < now) {
      Simulate('V', KEY_BREAK);
      execute_.store(5, std::memory_order_release);
    }
    break;
  case 5:
    if (execute_start_ + std::chrono::milliseconds(40) < now) {
      Simulate('E', KEY_MAKE);
      execute_.store(6, std::memory_order_release);
    }
    break;
  case 6:
    if (execute_start_ + std::chrono::milliseconds(700) < now) {
      Simulate('E', KEY_BREAK);
      execute_.store(0, std::memory_order_release);
    }
    break;
  }
}

bool View::DrawEntity(const XMFLOAT4X4& vm, XMVECTOR pc, XMVECTOR p0, XMVECTOR p1, XMVECTOR p2, float line, ID2D1SolidColorBrush* color) {
  // Calculate position for the hitbox center point.
  const auto s0 = WorldToScreen(vm, XMVectorGetX(p0), XMVectorGetY(p0), XMVectorGetZ(p0));
  if (!s0 || s0->x < 0.0f || s0->x > cx_ || s0->y < 0.0f || s0->y > cy_) {
    return false;
  }
#if EPOS_DEBUG_ENTITIES
  dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(s0->x, s0->y), 4.0f, 4.0f), color);
#endif

  // Get point values.
  const auto p1x = XMVectorGetX(p1);
  const auto p1y = XMVectorGetY(p1) - 0.4f;  // lower bottom point
  const auto p1z = XMVectorGetZ(p1);
  const auto p2x = XMVectorGetX(p2);
  const auto p2y = XMVectorGetY(p2) - 0.2f;  // lower top point
  const auto p2z = XMVectorGetZ(p2);

  // Create cube points.
  XMVECTOR cube[8];
  constexpr auto factor = 0.4f;
  cube[0] = XMVectorSet(p1x, p1y, p1z, 0.0f);
  cube[1] = XMVectorSet(p1x, p1y, p2z, 0.0f);
  cube[2] = XMVectorSet(p1x, p2y, p1z, 0.0f);
  cube[3] = XMVectorSet(p1x, p2y, p2z, 0.0f);
  cube[4] = XMVectorSet(p2x, p1y, p1z, 0.0f);
  cube[5] = XMVectorSet(p2x, p1y, p2z, 0.0f);
  cube[6] = XMVectorSet(p2x, p2y, p1z, 0.0f);
  cube[7] = XMVectorSet(p2x, p2y, p2z, 0.0f);

  // Calculate top point.
  const auto pt = (cube[2] + cube[7]) / 2;
#if EPOS_DEBUG_ENTITIES
  if (const auto sp = WorldToScreen(vm, XMVectorGetX(pt), XMVectorGetY(pt), XMVectorGetZ(pt))) {
    dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sp->x, sp->y), 3.0f, 3.0f), color_.red.Get());
  }
#endif

  // Calculate bottom point.
  const auto pb = (cube[0] + cube[5]) / 2;
#if EPOS_DEBUG_ENTITIES
  if (const auto sp = WorldToScreen(vm, XMVectorGetX(pb), XMVectorGetY(pb), XMVectorGetZ(pb))) {
    dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sp->x, sp->y), 3.0f, 3.0f), color_.green.Get());
  }
#endif

  // Reduce rectangle size.
  cube[0] = pb + (pb - cube[0]) * factor;
  cube[1] = pb + (pb - cube[1]) * factor;
  cube[2] = pt + (pt - cube[2]) * factor;
  cube[3] = pt + (pt - cube[3]) * factor;
  cube[4] = pb + (pb - cube[4]) * factor;
  cube[5] = pb + (pb - cube[5]) * factor;
  cube[6] = pt + (pt - cube[6]) * factor;
  cube[7] = pt + (pt - cube[7]) * factor;

  // Calculate screen positions.
  auto xmin = cx_;
  auto xmax = 0.0f;
  auto ymin = cy_;
  auto ymax = 0.0f;
  for (auto& e : cube) {
    if (const auto sp = WorldToScreen(vm, XMVectorGetX(e), XMVectorGetY(e), XMVectorGetZ(e))) {
#if EPOS_DEBUG_ENTITIES
      dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sp->x, sp->y), 3.0f, 3.0f), color_.white.Get());
#endif
      if (sp->x < xmin) {
        xmin = sp->x;
      }
      if (sp->x > xmax) {
        xmax = sp->x;
      }
      if (sp->y < ymin) {
        ymin = sp->y;
      }
      if (sp->y > ymax) {
        ymax = sp->y;
      }
    }
  }

#if EPOS_DEBUG_ENTITIES
  return false;
#endif

  if (xmin < xmax && ymin < ymax) {
    dc_->DrawRectangle(D2D1::RectF(xmin, ymin, xmax, ymax), color, line);
    const auto cx = g_view_cx / 2;
    const auto cy = g_view_cy / 2;
    if (cx > xmin && cx < xmax && cy > ymin && cy < ymax) {
      return true;
    }
  }
  return false;
}

void View::DrawText(std::wstring_view text, float x, float y, float cx, float cy, IDWriteTextFormat* format, ID2D1SolidColorBrush* color) {
  dc_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, D2D1::RectF(x, y, cx, cy), color,
    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void View::DrawText(const fmt::wmemory_buffer& text, float x, float y, float cx, float cy, IDWriteTextFormat* format, ID2D1SolidColorBrush* color) {
  dc_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, D2D1::RectF(x, y, cx, cy), color,
    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void View::ReportBinary(SIZE_T index, PVOID data, SIZE_T size) {
  Report(L"{:02}", index);
  for (unsigned i = 0x0; i <= 0xF; i++) {
    Report(L" X{:X}", i);
  }
  Report(L"\n--------------------------------------------------");
  for (SIZE_T i = 0; i < size; i++) {
    if (i % 0x10 == 0) {
      Report(L"\n{:02X}", i / 0xF);
    }
    if (reinterpret_cast<BYTE*>(data)[i]) {
      Report(L" {:02X}", 0xFF & static_cast<unsigned>(reinterpret_cast<BYTE*>(data)[i]));
    } else {
      Report(L"   ");
    }
  }
  Report(L"\n\n");
}

void View::OnProcessCreate(HANDLE pid, std::wstring_view name) noexcept {
  if (!pid_ && name == L"Overwatch.exe") {
    pid_ = pid;
  }
}

void View::OnProcessDestroy(HANDLE pid, std::wstring_view name) noexcept {
  if (pid == pid_ && name == L"Overwatch.exe") {
    pid_ = nullptr;
    exe_ = 0;
    if (auto is = std::ofstream("epos.txt", std::ios::binary)) {
      is << fmt::format("{}\n", reinterpret_cast<UINT_PTR>(pid_));
      is << fmt::format("0x{:016X}\n", exe_);
    }
  }
}

void View::OnImageLoad(HANDLE pid, std::wstring_view name, UINT_PTR base, UINT_PTR size) noexcept {
  if (pid == pid_ && name == L"Overwatch.exe") {
    pid_ = pid;
    exe_ = base;
    if (auto is = std::ofstream("epos.txt", std::ios::binary)) {
      is << fmt::format("{}\n", reinterpret_cast<UINT_PTR>(pid_));
      is << fmt::format("0x{:016X}\n", exe_);
    }
  }
}

void View::OnKeyboard(UINT message, KBDLLHOOKSTRUCT* event) noexcept {
  if (message == WM_KEYDOWN || message == WM_KEYUP) {
    switch (MapVirtualKey(event->scanCode, MAPVK_VSC_TO_VK)) {
    case '2': {
      if (message == WM_KEYDOWN) {
        execute_.store(1, std::memory_order_release);
      }
    } break;
    case VK_LSHIFT:
      sleep_.store(message == WM_KEYDOWN, std::memory_order_release);
      break;
    }
  }
}

void View::OnMouse(UINT message, MSLLHOOKSTRUCT* event) noexcept {
  switch (message) {
  case WM_LBUTTONDOWN:
    trigger_.store(true, std::memory_order_release);
    break;
  case WM_LBUTTONUP:
    trigger_.store(false, std::memory_order_release);
    break;
  case WM_RBUTTONDOWN:
    scope_.store(true, std::memory_order_release);
    break;
  case WM_RBUTTONUP:
    scope_.store(false, std::memory_order_release);
    break;
  case WM_XBUTTONDOWN:
    if (const auto button = HIWORD(event->mouseData); button == 2) {
      attack_.store(true, std::memory_order_release);
    } else if (button == 1) {
      special_.store(true, std::memory_order_release);
    }
    break;
  case WM_XBUTTONUP:
    if (const auto button = HIWORD(event->mouseData); button == 2) {
      attack_.store(false, std::memory_order_release);
    } else if (button == 1) {
      special_.store(false, std::memory_order_release);
    }
    break;
  }
}
