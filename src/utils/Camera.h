#ifndef __CAMERA_H_INCLUDED__
#define __CAMERA_H_INCLUDED__

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

inline glm::mat4 perspectiveReversedZ(float fovyRadians, float aspect, float zNear, float zFar) {
  glm::mat4 proj = glm::perspective(fovyRadians, aspect, zFar, zNear);
  proj[1][1] *= -1.0f;
  return proj;
}

struct OrbitCamera {
  glm::vec3 target{0.0f};
  float     yaw         = 0.0f;
  float     pitch       = 0.0f;
  float     distance    = 3.0f;
  float     minDistance = 0.2f;
  float     maxDistance = 500.0f;

  glm::vec3 eye() const {
    return target + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                          std::cos(pitch) * std::cos(yaw));
  }

  glm::mat4 view() const { return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }

  static OrbitCamera looking(glm::vec3 eye, glm::vec3 target) {
    OrbitCamera cam{};
    glm::vec3   d = eye - target;
    cam.target    = target;
    cam.distance  = glm::length(d);
    cam.pitch     = std::asin(std::clamp(d.y / cam.distance, -1.0f, 1.0f));
    cam.yaw       = std::atan2(d.x, d.z);
    cam.minDistance = std::min(cam.minDistance, cam.distance);
    cam.maxDistance = std::max(cam.maxDistance, cam.distance);
    return cam;
  }

  void orbit(float dx, float dy) {
    yaw  -= dx * 0.005f;
    pitch = std::clamp(pitch + dy * 0.005f, -1.55f, 1.55f);
  }

  void zoom(float notches) {
    distance = std::clamp(distance * std::pow(0.9f, notches), minDistance, maxDistance);
  }
};

#endif
