#include "aura/render/Camera.hpp"

#include <numbers>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aura::render {
    Camera::Camera(const math::Vec3 &position, const math::Vec3 &target, float aspectRatio)
        : target_(target), aspectRatio_(aspectRatio),
          fovRadians_(std::numbers::pi_v<float> / 3.0f) {
        const auto offset = position - target;
        distance_ = offset.length();
        if (!std::isfinite(distance_) || distance_ <= 0.0f || !std::isfinite(aspectRatio) || aspectRatio <= 0)
            throw std::invalid_argument("Camera needs distinct finite position/target and positive aspect ratio");
        yaw_ = std::atan2(offset.x, offset.z);
        pitch_ = std::asin(std::clamp(offset.y / distance_, -1.0f, 1.0f));
    }

    math::Vec3 Camera::position() const {
        const float horizontalDistance =
                distance_ * std::cos(pitch_);

        return {
            target_.x + horizontalDistance * std::sin(yaw_),
            target_.y + distance_ * std::sin(pitch_),
            target_.z + horizontalDistance * std::cos(yaw_)
        };
    }

    math::Mat4 Camera::viewMatrix() const {
        const auto eye = position();
        const auto forward = (target_ - eye).normalized();
        const math::Vec3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};
        // Yaw-defined right keeps an exact vertical constructor view nonsingular.
        return math::Mat4::lookAt(eye, target_, right.cross(forward).normalized());
    }

    math::Mat4 Camera::projectionMatrix() const {
        return math::Mat4::perspective(fovRadians_, aspectRatio_, nearPlane_, farPlane_);
    }

    void Camera::setAspectRatio(float aspectRatio) {
        if (std::isfinite(aspectRatio) && aspectRatio > 0.0f) {
            aspectRatio_ = aspectRatio;
        }
    }

    void Camera::setOrbit(float yaw, float pitch) {
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
        yaw_ = std::remainder(yaw, 2.0f * std::numbers::pi_v<float>);
        pitch_ = std::clamp(pitch, -1.5f, 1.5f);
    }

    void Camera::orbit(float deltaYaw, float deltaPitch) {
        setOrbit(yaw_ + deltaYaw, pitch_ + deltaPitch);
    }

    void Camera::zoom(float delta) {
        if (!std::isfinite(delta)) return;
        distance_ = std::clamp(distance_ * std::exp(-0.12f * delta), 1.0f, 30.0f);
    }

    const math::Vec3 &Camera::target() const { return target_; }
    float Camera::distance() const { return distance_; }

    void Camera::setTarget(const math::Vec3 &worldTarget) {
        if (std::isfinite(worldTarget.x) && std::isfinite(worldTarget.y) && std::isfinite(worldTarget.z))
            target_ = worldTarget;
    }

    void Camera::pan(float deltaX, float deltaY) {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
        const auto forward = (target_ - position()).normalized();
        const math::Vec3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};
        const auto screenUp = right.cross(forward).normalized();
        const float viewHeight = 2.0f * distance_ * std::tan(fovRadians_ / 2.0f);
        setTarget(target_ + (right * deltaX + screenUp * deltaY) * viewHeight);
    }
}
