#include <deus.hpp>
#include <fmt/format.h>
#include <chrono>
#include <iostream>
#include <thread>

inline void print_error(std::error_code ec, const char* message = nullptr) {
  if (message) {
    fmt::print("{}\n", message);
  }
  fmt::print("Error 0x{:X}: {}\n", static_cast<unsigned>(ec.value()), ec.message());
}

int main(int argc, char* argv[]) {
  // Load driver.
  if (const auto ec = deus::load(argc, argv, true)) {
    print_error(ec, "Could not load driver.");
    return ec.value();
  }

  // Create device.
  deus::device device;
  if (const auto ec = device.create()) {
    print_error(ec, "Could not create device.");
    return ec.value();
  }

  // Create events.
  deus::events events;
  if (const auto ec = events.open(device)) {
    print_error(ec, "Could not create events.");
    return ec.value();
  }

  auto thread = std::thread([&]() {
    while (true) {
      events.poll();
      for (const auto& event : events) {
        if (event.type == deus::io::event::type::log) {
          auto message = std::wstring(event.data(), event.size());
          std::wcout << message << std::endl;
          if (const auto pos = message.find_last_of(L'('); pos != message.npos) {
            message.erase(0, pos);
            NTSTATUS status = STATUS_SUCCESS;
            if (std::swscanf(message.data(), L"(%ld).", &status) == 1) {
              std::cout << deus::make_ntstatus_error_code(status).message() << std::endl;
            }
          }
        }
      }
      events.clear();
    }
    });

  while (true) {
    Sleep(1000);

    MOUSE_INPUT_DATA mid = {};
    mid.Flags |= MOUSE_MOVE_RELATIVE;
    mid.ButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;
    if (const auto ec = device.simulate(mid)) {
      print_error(ec, "Could not simulate button down input.");
    }

    Sleep(1000);

    mid.ButtonFlags &= ~MOUSE_LEFT_BUTTON_DOWN;
    mid.ButtonFlags |= MOUSE_LEFT_BUTTON_UP;
    if (const auto ec = device.simulate(mid)) {
      print_error(ec, "Could not simulate button up input.");
    }

    Sleep(1000);

    KEYBOARD_INPUT_DATA kid = {};
    kid.MakeCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
    kid.Flags = KEY_MAKE;
    if (const auto ec = device.simulate(mid)) {
      print_error(ec, "Could not simulate key down input.");
    }

    Sleep(1000);

    kid.Flags = KEY_BREAK;
    if (const auto ec = device.simulate(mid)) {
      print_error(ec, "Could not simulate key up input.");
    }
  }

  if (thread.joinable()) {
    thread.join();
  }
}
