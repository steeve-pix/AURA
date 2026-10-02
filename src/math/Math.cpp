#include <cmath>
#include <numbers>
#include <stdexcept>
#include "aura/math/Mat4.hpp"

namespace aura::math {
    Mat4::Mat4() = default;

    Mat4 Mat4::identity() {
        Mat4 result;

        result.values_ = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };

        return result;
    }

    const float *Mat4::data() const {
        return values_.data();
    }

    Mat4 Mat4::perspective(float fovRadians, float aspectRatio, float nearPlane, float farPlane) {
        if (
            fovRadians <= 0.0f ||
            fovRadians >= std::numbers::pi_v<float> ||
            aspectRatio <= 0.0f || nearPlane <= 0.0f ||
            farPlane <= nearPlane
        ) {
            throw std::invalid_argument("Invalid perspective parameters");
        }

        Mat4 result;

        const float f =
                1.0f / std::tan(fovRadians * 0.5f);

        result.values_ = {
            f / aspectRatio, 0.0f, 0.0f, 0.0f,
            0.0f, f, 0.0f, 0.0f,
            0.0f, 0.0f, (farPlane + nearPlane) / (nearPlane - farPlane), -1.0f, 0.0f, 0.0f,
            (2.0f * farPlane * nearPlane) / (nearPlane - farPlane), 0.0f
        };

        return result;
    }

    Mat4 Mat4::lookAt(const Vec3 &position, const Vec3 &target, const Vec3 &up) {
        const Vec3 forward =
                (target - position).normalized();

        const Vec3 right =
                forward.cross(up).normalized();

        const Vec3 cameraUp =
                right.cross(forward);

        Mat4 result;

        result.values_ = {
            right.x, cameraUp.x, -forward.x, 0.0f,
            right.y, cameraUp.y, -forward.y, 0.0f,
            right.z, cameraUp.z, -forward.z, 0.0f,

            -right.dot(position),
            -cameraUp.dot(position),
            forward.dot(position),
            1.0f
        };

        return result;
    }

    Mat4 Mat4::translation(const Vec3 &positon) {
        Mat4 result = Mat4::identity();

        result.values_[12] = positon.x;
        result.values_[13] = positon.y;
        result.values_[14] = positon.z;

        return result;
    }

    Mat4 Mat4::rotation(const Quaternion &orientation) {
        const Quaternion q = orientation.normalized();

        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;

        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;

        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;

        Mat4 result = identity();

        result.values_ = {
            1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f,

            2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f,

            2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f,

            0.0f, 0.0f, 0.0f, 1.0f
        };

        return result;
    }

    Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
        Mat4 result = identity();

        result.values_[0] =
                2.0f / (right - left);

        result.values_[5] =
                2.0f / (top - bottom);

        result.values_[10] =
                -2.0f / (farPlane - nearPlane);

        result.values_[12] =
                -(right + left) / (right - left);

        result.values_[13] =
                -(top + bottom) / (top - bottom);

        result.values_[14] =
                -(farPlane + nearPlane) / (farPlane - nearPlane);

        return result;
    }

    Mat4 Mat4::operator*(const Mat4 &other) const {
        Mat4 result;
        for (int column{}; column < 4; ++column) {
            for (int row{}; row < 4; ++row) {
                float value = 0.0f;
                for (int k{}; k < 4; ++k) {
                    value += values_[k * 4 + row] * other.values_[column * 4 + k];
                }
                result.values_[column * 4 + row] = value;
            }
        }

        return result;
    }
}
