#include "aura/render/Camera.hpp"

#include <numbers>

namespace aura::render {
    Camera::Camera(const math::Vec3 &position, const math::Vec3 &target, float aspectRatio)
        : position_(position), target_(target), aspectRatio_(aspectRatio),
          fovRadians_(std::numbers::pi_v<float> / 3.0f) {
    }

    math::Mat4 Camera::viewMatrix() const {
        return math::Mat4::lookAt(position_, target_, up_);
    }

    math::Mat4 Camera::projectionMatrix() const {
        return math::Mat4::perspective(fovRadians_, aspectRatio_, nearPlane_, farPlane_);
    }
}
