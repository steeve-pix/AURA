#pragma once
#include <array>

#include "Vec3.hpp"

namespace aura::math {
    class Mat4 {
    public:
        Mat4();

        static Mat4 identity();

        [[nodiscard]] const float *data() const;

        static Mat4 perspective(float fovRadians, float aspectRatio, float nearPlane, float farPlane);

        static Mat4 lookAt(const Vec3 &position, const Vec3 &target, const Vec3 &up);

        static Mat4 translation(const Vec3& positon);

    private:
        std::array<float, 16> values_{};
    };
}
