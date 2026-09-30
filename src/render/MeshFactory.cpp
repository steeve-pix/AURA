#include "aura/render/MeshFactory.hpp"

namespace aura::render {
    std::vector<float> MeshFactory::createCube() {
        return {
            // Front face (Z = +0.5)
            -0.5f, -0.5f, 0.5f,
            0.5f, -0.5f, 0.5f,
            0.5f, 0.5f, 0.5f,
            -0.5f, -0.5f, 0.5f,
            0.5f, 0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f,

            // Back face (Z = -0.5)
            0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,
            -0.5f, 0.5f, -0.5f,
            0.5f, -0.5f, -0.5f,
            -0.5f, 0.5f, -0.5f,
            0.5f, 0.5f, -0.5f,

            // Left face (X = -0.5)
            -0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f,
            -0.5f, -0.5f, -0.5f,
            -0.5f, 0.5f, 0.5f,
            -0.5f, 0.5f, -0.5f,

            // Right face (X = +0.5)
            0.5f, -0.5f, 0.5f,
            0.5f, -0.5f, -0.5f,
            0.5f, 0.5f, -0.5f,
            0.5f, -0.5f, 0.5f,
            0.5f, 0.5f, -0.5f,
            0.5f, 0.5f, 0.5f,

            // Top face (Y = +0.5)
            -0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, -0.5f,
            -0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, -0.5f,
            -0.5f, 0.5f, -0.5f,

            // Bottom face (Y = -0.5)
            -0.5f, -0.5f, -0.5f,
            0.5f, -0.5f, -0.5f,
            0.5f, -0.5f, 0.5f,
            -0.5f, -0.5f, -0.5f,
            0.5f, -0.5f, 0.5f,
            -0.5f, -0.5f, 0.5f
        };
    }

    std::vector<float> MeshFactory::createGrid(int halfSize, float spacing) {
        std::vector<float> vertices;

        const auto extent = static_cast<float>(halfSize) * spacing;

        for (int i = -halfSize; i <= halfSize; ++i) {
            const float coordinate = static_cast<float>(i) * spacing;

            vertices.insert(vertices.end(), {coordinate, 0.0f, -extent, coordinate, 0.0f, extent});
            vertices.insert(vertices.end(), {-extent, 0.0f, coordinate, extent, 0.0f, coordinate});
        }

        return vertices;
    }

    std::vector<float> MeshFactory::createFloor(float size) {
        const float h = size * 0.5f;

        return {
            -h, 0.0f, -h,
            -h, 0.0f, h,
            h, 0.0f, h,

            -h, 0.0f, -h,
            h, 0.0f, h,
            h, 0.0f, -h
        };
    }
}
