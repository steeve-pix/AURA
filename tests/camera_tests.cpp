#include <cmath>
#include <iostream>

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
    const bool passed =
        std::abs(view.data()[14] + std::sqrt(45.0f)) <= tolerance &&
        std::abs(view.data()[15] - 1.0f) <= tolerance &&
        std::abs(projection.data()[0] - 0.974279f) <= tolerance &&
        std::abs(projection.data()[5] - 1.73205f) <= tolerance &&
        std::abs(squareProjection.data()[0] - squareProjection.data()[5]) <= tolerance;

    if (!passed) {
        std::cerr << "FAIL camera matrices: view Z=" << view.data()[14]
                  << ", projection scales=" << projection.data()[0]
                  << ", " << projection.data()[5]
                  << ", square projection scales=" << squareProjection.data()[0]
                  << ", " << squareProjection.data()[5] << '\n';
    }
    return passed ? 0 : 1;
}
