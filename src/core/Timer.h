#ifndef __TIMER_H_INCLUDED__
#define __TIMER_H_INCLUDED__

#include "core/Device.h"

#include <chrono>
#include <functional>

struct Timing {
  double medianMs = 0.0;
  double p95Ms    = 0.0;
  double cpuMs    = 0.0;
};

Timing timePass(const Device& dev,
                std::uint32_t warmup,
                std::uint32_t frames,
                const std::function<void(VkCommandBuffer)>& record,
                std::uint32_t repeats = FRAME_REPEATS);

Timing timeCpu(std::uint32_t warmup, std::uint32_t runs, const std::function<void()>& work);

struct Stopwatch {
  std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

  void   restart() { started = std::chrono::steady_clock::now(); }
  double ms() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
      .count();
  }
};

#endif
