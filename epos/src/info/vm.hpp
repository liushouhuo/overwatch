#pragma once
#include "info.hpp"
#include <fstream>
#include <map>

void info(deus::device& device) {
  const auto addresses = device.scan(g_pid, "48 8B ?? ?? ?? ?? ?? 0F 28 D0 F3 0F 53 CA 0F 28 C3 F3 0F 11 A5 ?? 00 00");
  for (const auto address : addresses) {
    fmt::print("0x{:016X}\n", address);
    UINT_PTR value = 0;
  }
  fmt::print("{} found\n", addresses.size());

#if 0
  constexpr std::size_t size = 64;

  std::array<BYTE, size> mask;
  for (auto& e : mask) {
    e = 0xFF;
  }

  std::array<BYTE, size> scan;
  if (const auto ec = device.read(g_pid, scan.data(), size, g_exe + 0x2DBBBF8, 0x8, 0x80); ec) {
    fmt::print("scan error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
    return;
  }

  bool changed = true;
  std::array<BYTE, size> data;
  while (true) {
    if (const auto ec = device.read(g_pid, data.data(), size, g_exe + 0x2DBBBF8, 0x8, 0x80); ec) {
      fmt::print("data error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
      return;
    }
    UINT_PTR ptr = 0;
    device.read(g_pid, &ptr, sizeof(ptr), g_exe + 0x2DBBBF8, 0x8);
    fmt::memory_buffer buffer;
    fmt::format_to(buffer, "== {:016X} ===========================\n", ptr);  // 0x00000283D1F05800
    for (std::size_t i = 0; i < size; i++) {
      if (i > 0 && i % 16 == 0) {
        fmt::format_to(buffer, "\n");
      }
      const auto mask_one = (mask[i] >> 4) & 0xF;
      const auto mask_two = (mask[i] & 0xF);
      const auto scan_one = (scan[i] >> 4) & 0xF;
      const auto scan_two = (scan[i] & 0xF);
      const auto data_one = (data[i] >> 4) & 0xF;
      const auto data_two = (data[i] & 0xF);
      if (mask_one) {
        if (data_one == scan_one) {
          fmt::format_to(buffer, "{:X}", data_one);
        } else {
          changed = true;
          mask[i] &= 0x0F;
          fmt::format_to(buffer, "?");
        }
      } else {
        fmt::format_to(buffer, "?");
      }
      if (mask_two) {
        if (data_two == scan_two) {
          fmt::format_to(buffer, "{:X}", data_two);
        } else {
          changed = true;
          mask[i] &= 0xF0;
          fmt::format_to(buffer, "?");
        }
      } else {
        fmt::format_to(buffer, "?");
      }
      fmt::format_to(buffer, " ");
    }
    if (changed) {
      fmt::print("{}\n", std::string_view(buffer.data(), buffer.size()));
      changed = false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Training
  // 49 60 62 40 00 00 00 00 5C FC B7 BD 02 FA B7 BD
  // 00 00 00 00 43 0A CA 40 00 00 00 00 00 00 00 00
  // F9 58 A3 BE 00 00 00 00 4E FA 7E BF 0B F7 7E BF
  // 49 E3 38 42 A2 2E E1 C0 A8 00 3B C2 DD 97 3A C2
#endif
}
