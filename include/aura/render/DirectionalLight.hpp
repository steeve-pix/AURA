#pragma once
#include "aura/math/Mat4.hpp"
#include "aura/math/Vec3.hpp"

namespace aura::render {
    class DirectionalLight {
    public:
        DirectionalLight(const math::Vec3 &position, const math::Vec3 &target);

        [[nodiscard]] math::Mat4 viewMatrix() const;

        static math::Mat4 projectionMatrix();

        [[nodiscard]] math::Mat4 lightSpaceMatrix() const;

    private:
        math::Vec3 position_;
        math::Vec3 target_;
        math::Vec3 up_{0.0f, 1.0f, 0.0f};
    };
}
