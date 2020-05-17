#pragma once
#include <deus.hpp>
#include <fmt/format.h>
#include <array>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

inline auto g_pid = reinterpret_cast<HANDLE>(UINT_PTR(0));
inline auto g_exe = UINT_PTR(0);
inline auto g_max = UINT_PTR(0);