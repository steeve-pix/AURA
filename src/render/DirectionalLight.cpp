#include "aura/render/DirectionalLight.hpp"

namespace aura::render {
    DirectionalLight::DirectionalLight(const math::Vec3 &position, const math::Vec3 &target)
        : position_(position), target_(target) {
    }

    math::Mat4 DirectionalLight::viewMatrix() const {
        return math::Mat4::lookAt(position_, target_, up_);
    }

    math::Mat4 DirectionalLight::projectionMatrix() {
        return math::Mat4::orthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 30.0f);
    }

    math::Mat4 DirectionalLight::lightSpaceMatrix() const {
        return projectionMatrix() * viewMatrix();
    }
}
