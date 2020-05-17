#include "input.hpp"
#include <fstream>

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

int main(int argc, char* argv[]) {
#if 1
  if (auto is = std::ifstream("epos.txt", std::ios::binary)) {
    std::string line;
    if (std::getline(is, line) && std::sscanf(line.data(), "%tu", reinterpret_cast<UINT_PTR*>(&g_pid)) == 1) {
      if (!std::getline(is, line) || std::sscanf(line.data(), "0x%tX", &g_exe) != 1) {
        g_pid = nullptr;
        g_exe = 0;
        g_max = 0;
      } else if (!std::getline(is, line) || std::sscanf(line.data(), "0x%tX", &g_max) != 1) {
        g_pid = nullptr;
        g_exe = 0;
        g_max = 0;
      }
    }
  }
#endif

  if (const auto ec = deus::load(argc, argv)) {
    fmt::print("load error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
    return ec.value();
  }

  deus::device device;
  if (const auto ec = device.create()) {
    fmt::print("create device error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
    return ec.value();
  }

#if 0
  if (g_pid) {
    deus::memory memory;
    if (device.info(g_pid, memory, deus::io::min, deus::io::min + 1024 * 1024)) {
      g_pid = nullptr;
      g_exe = 0;
      g_max = 0;
      if (auto is = std::ofstream("epos.txt", std::ios::binary)) {
        is << fmt::format("{}\n", reinterpret_cast<UINT_PTR>(g_pid));
        is << fmt::format("0x{:016X}\n", g_exe);
        is << fmt::format("0x{:016X}\n", g_max);
      }
    }
  }

  if (!g_pid) {
    deus::events events;
    if (const auto ec = events.open(device)) {
      fmt::print("create events error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
      return ec.value();
    }
    while (true/*!g_pid*/) {
      Sleep(10);
      events.poll();
      for (const auto& event : events) {
        std::wstring_view path(event.data(), event.size());
        if (const auto pos = path.find_last_of(L'\\'); pos != std::wstring_view::npos) {
          path = path.substr(pos + 1);
        }
        if (g_pid && event.pid == g_pid) {
          if (auto is = std::ofstream("images.txt", std::ios::binary | std::ios::app)) {
            is << fmt::format("0x{:016X} {} {}\n", event.image_base, event.image_size, W2S(path));
          }
        }
        if (path == L"Overwatch.exe" && event.type == deus::io::event::type::image_loaded) {
          g_pid = event.pid;
          g_exe = event.image_base;
          g_max = event.image_base + event.image_size;
          if (auto is = std::ofstream("epos.txt", std::ios::binary)) {
            is << fmt::format("{}\n", reinterpret_cast<UINT_PTR>(g_pid));
            is << fmt::format("0x{:016X}\n", g_exe);
            is << fmt::format("0x{:016X}\n", g_max);
          }
          if (auto is = std::ofstream("images.txt", std::ios::binary | std::ios::app)) {
            is << fmt::format("0x{:016X} {} {}\n", event.image_base, event.image_size, W2S(path));
          }
          break;
        }
      }
      events.clear();
    }
    events.close();
  }

  fmt::print("{}\n", reinterpret_cast<UINT_PTR>(g_pid));
  fmt::print("0x{:016X}\n", g_exe);
  fmt::print("0x{:016X}\n", g_max);
#endif

  info(device);
}
