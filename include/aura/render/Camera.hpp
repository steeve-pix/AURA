#pragma once
#include "aura/math/Mat4.hpp"
#include "aura/math/Vec3.hpp"

namespace aura::render {
    class Camera {
    public:
        Camera(const math::Vec3 &position, const math::Vec3 &target, float aspectRatio);

        [[nodiscard]] math::Mat4 viewMatrix() const;

        [[nodiscard]] math::Mat4 projectionMatrix() const;

    private:
        math::Vec3 position_;
        math::Vec3 target_;
        math::Vec3 up_{0.0f, 1.0f, 0.0f};

        float aspectRatio_;
        float fovRadians_;
        float nearPlane_ = 0.1f;
        float farPlane_ = 100.0f;
    };
}
