#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "aura/render/Camera.hpp"

int main() {
    aura::render::Camera camera{
        {0.0f, 3.0f, 6.0f},
        {0.0f, 0.0f, 0.0f},
        1280.0f / 720.0f
    };

    const auto view = camera.viewMatrix();
    const auto projection = camera.projectionMatrix();
    camera.setAspectRatio(1.0f);
    const auto squareProjection = camera.projectionMatrix();

    constexpr float tolerance = 1e-4f;
    bool passed =
        std::abs(view.data()[14] + std::sqrt(45.0f)) <= tolerance &&
        std::abs(view.data()[15] - 1.0f) <= tolerance &&
        std::abs(projection.data()[0] - 0.974279f) <= tolerance &&
        std::abs(projection.data()[5] - 1.73205f) <= tolerance &&
        std::abs(squareProjection.data()[0] - squareProjection.data()[5]) <= tolerance;

    const auto check = [&](bool condition, const char *message) {
        if (!condition) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    check((camera.position() - aura::math::Vec3{0, 3, 6}).length() < tolerance,
          "constructor honors supplied world position");
    const auto startPosition = camera.position();
    camera.orbit(0, 0);
    check((camera.position() - startPosition).length() < tolerance, "first orbit does not jump");
    camera.zoom(1);
    const float nearRatio = camera.distance() / std::sqrt(45.0f);
    camera.zoom(-1);
    check(std::abs(camera.distance() - std::sqrt(45.0f)) < tolerance, "opposite scroll restores distance");
    aura::render::Camera farCamera{{0, 0, 20}, {}, 1};
    farCamera.zoom(1);
    check(std::abs(farCamera.distance() / 20 - nearRatio) < tolerance, "zoom is proportional to distance");
    camera.zoom(10000);
    check(camera.distance() == 1, "near zoom bound");
    camera.zoom(-10000);
    check(camera.distance() == 30, "far zoom bound");
    camera.setOrbit(0, 0);
    camera.pan(0.1f, 0.2f);
    const float viewHeight = 60 * std::tan(3.14159265359f / 6);
    check((camera.target() - aura::math::Vec3{0.1f * viewHeight, 0.2f * viewHeight, 0}).length() < tolerance,
          "pan moves target in camera screen plane at visible scale");
    const auto offset = camera.position() - camera.target();
    camera.setTarget({2, 5, -3});
    check((camera.position() - camera.target() - offset).length() < tolerance, "focus preserves viewing offset");
    camera.setOrbit(1.57079632679f, 0);
    check((camera.position() - camera.target() - aura::math::Vec3{30, 0, 0}).length() < tolerance,
          "side preset has absolute yaw");
    for (float pitch : {-100.0f, 100.0f}) {
        camera.setOrbit(0, pitch);
        const auto poleView = camera.viewMatrix();
        for (int i = 0; i < 16; ++i) check(std::isfinite(poleView.data()[i]), "pole-clamped view stays finite");
    }
    aura::render::Camera vertical{{0, 10, 0}, {}, 1};
    check((vertical.position() - aura::math::Vec3{0, 10, 0}).length() < tolerance, "vertical constructor position");
    const auto verticalView = vertical.viewMatrix();
    for (int i = 0; i < 16; ++i) check(std::isfinite(verticalView.data()[i]), "vertical view stays finite");
    bool rejected = false;
    try { aura::render::Camera invalid{{}, {}, 1}; }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "coincident eye and target rejected");
    const auto targetBeforeInvalid = camera.target();
    camera.setTarget({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    check((camera.target() - targetBeforeInvalid).length() == 0, "invalid focus ignored");
    if (!passed) {
        std::cerr << "FAIL camera matrices: view Z=" << view.data()[14]
                  << ", projection scales=" << projection.data()[0]
                  << ", " << projection.data()[5]
                  << ", square projection scales=" << squareProjection.data()[0]
                  << ", " << squareProjection.data()[5] << '\n';
    }
    return passed ? 0 : 1;
}
