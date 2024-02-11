#pragma once

#include <chrono>
#include <deque>
#include <numeric>

struct Timer {
  size_t bufferSize;
  std::chrono::time_point<std::chrono::steady_clock> start;
  std::deque<std::chrono::nanoseconds> durations;

  Timer(size_t bufferSize = 20) : bufferSize(bufferSize) {
  }

  void Start() {
    start = std::chrono::steady_clock::now();
  }

  void End() {
    auto end = std::chrono::steady_clock::now();
    durations.push_back(end - start);
    if (durations.size() > bufferSize) durations.pop_front();
  }

  auto GetAverageDuration() {
    return std::accumulate(durations.begin(), durations.end(), std::chrono::nanoseconds(0)) / durations.size();
  }
};
