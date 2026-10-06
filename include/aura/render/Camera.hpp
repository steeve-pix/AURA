#pragma once
#include "aura/math/Mat4.hpp"
#include "aura/math/Vec3.hpp"

namespace aura::render {
    class Camera {
    public:
        Camera(const math::Vec3 &position, const math::Vec3 &target, float aspectRatio);

        [[nodiscard]] math::Mat4 viewMatrix() const;

        [[nodiscard]] math::Mat4 projectionMatrix() const;

        void setAspectRatio(float aspectRatio);

        void orbit(float deltaYaw, float deltaPitch);

        // Positive scroll steps zoom in by a constant ratio, independent of distance.
        void zoom(float delta);

        void setOrbit(float yaw, float pitch);
        void setTarget(const math::Vec3 &worldTarget);
        [[nodiscard]] math::Vec3 position() const;
        [[nodiscard]] float distance() const;

        const math::Vec3 &target() const;

        // Screen-plane movement in fractions of viewport height (right/up positive).
        void pan(float deltaX, float deltaY);

    private:
        math::Vec3 target_;

        float aspectRatio_;
        float fovRadians_;
        float nearPlane_ = 0.1f;
        float farPlane_ = 100.0f;

        float distance_ = 6.0f;
        float yaw_ = 0.0f;
        float pitch_ = 0.4f;
    };
}
