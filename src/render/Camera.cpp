#include "aura/render/Camera.hpp"

#include <numbers>
#include <algorithm>
#include <cmath>

namespace aura::render {
    Camera::Camera(const math::Vec3 &position, const math::Vec3 &target, float aspectRatio)
        : position_(position), target_(target), aspectRatio_(aspectRatio),
          fovRadians_(std::numbers::pi_v<float> / 3.0f) {
    }

    math::Mat4 Camera::viewMatrix() const {
        const float horizontalDistance =
                distance_ * std::cos(pitch_);

        math::Vec3 position{
            target_.x + horizontalDistance * std::sin(yaw_),
            target_.y + distance_ * std::sin(pitch_),
            target_.z + horizontalDistance * std::cos(yaw_)
        };
        return math::Mat4::lookAt(position, target_, up_);
    }

    math::Mat4 Camera::projectionMatrix() const {
        return math::Mat4::perspective(fovRadians_, aspectRatio_, nearPlane_, farPlane_);
    }

    void Camera::setAspectRatio(float aspectRatio) {
        if (aspectRatio > 0.0f) {
            aspectRatio_ = aspectRatio;
        }
    }

    void Camera::orbit(float deltaYaw, float deltaPitch) {
        yaw_ += deltaYaw;
        pitch_ += deltaPitch;
        pitch_ = std::clamp(pitch_, -1.5f, 1.5f);
    }

    void Camera::zoom(float delta) {
        distance_ -= delta;
        distance_ = std::clamp(distance_, 1.0f, 30.0f);
    }
}
