#pragma once

#include "glm/exponential.hpp"
#include "glm/ext/scalar_constants.hpp"
#include "glm/trigonometric.hpp"

inline float EaseOutSine(float x) {
  return glm::sin((x * glm::pi<float>()) / 2);
};

inline float EaseOutElastic(float x) {
  float c4 = (2 * glm::pi<float>()) / 3;
  return x == 0   ? 0
         : x == 1 ? 1
                  : glm::pow(2, -10 * x) * glm::sin((x * 10 - 0.75) * c4) + 1;
};

inline float EaseOutQuad(float x) {
  return 1 - (1 - x) * (1 - x);
};

inline float EaseOutCubic(float x) {
  return 1 - glm::pow(1 - x, 3);
};
