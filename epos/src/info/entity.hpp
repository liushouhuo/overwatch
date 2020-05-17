#pragma once
#include "info.hpp"

void info(deus::device& device) {
  const auto tp0 = std::chrono::high_resolution_clock::now();

  deus::memory memory;
  if (const auto ec = device.info(g_pid, memory)) {
    fmt::print("info error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
    return;
  }

  const auto tp1 = std::chrono::high_resolution_clock::now();

  if (const auto ec = device.copy(g_pid, memory)) {
    fmt::print("copy error: 0x{:08X} {}\n", static_cast<unsigned>(ec.value()), ec.message());
    return;
  }

  const auto tp2 = std::chrono::high_resolution_clock::now();

  const auto addresses = deus::scan(memory, "00 00 00 00 ?? ?? 00 00 ?? 00 00 00 ?? 00 00 00 00 00 FF FF 00 01 00 00 02");

  const auto tp3 = std::chrono::high_resolution_clock::now();

  for (const auto address : addresses) {
    for (const auto& e : memory) {
      if (e.base_address <= address && address < e.base_address + e.region_size) {
        fmt::print("0x{:016X}: 0x{:016X} {} 0x{:X} 0x{:X} 0x{:X}\n",
          address, e.base_address, e.region_size, e.state, e.protect, e.type);
      }
    }
  }
  fmt::print("{} addresses in {} ms ({} ms info, {} ms copy, {} ms scan)\n", addresses.size(),
    std::chrono::duration_cast<std::chrono::milliseconds>(tp3 - tp0).count(),
    std::chrono::duration_cast<std::chrono::milliseconds>(tp1 - tp0).count(),
    std::chrono::duration_cast<std::chrono::milliseconds>(tp2 - tp1).count(),
    std::chrono::duration_cast<std::chrono::milliseconds>(tp3 - tp2).count());

  // 0x000002619835B19C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835B31C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835B49C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835B61C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835B79C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835B91C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835BA9C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835BC1C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835BD9C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835BF1C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835C09C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835C45C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835C5DC: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835C75C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835C99C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 0x000002619835CF9C: 0x0000026198320000 1572864 0x1000 0x4 0x20000
  // 16 addresses in 15118 ms (107 ms info, 6991 ms copy, 8019 ms scan)
  // 16 addresses in    69 ms ( 28 ms info,    0 ms copy,   40 ms scan) (optimized)
}
