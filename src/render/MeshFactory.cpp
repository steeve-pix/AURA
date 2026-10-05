#include "aura/render/MeshFactory.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aura::render {
    std::vector<float> MeshFactory::createCube() {
        return {
            // Front face (Z = +0.5)
            -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,

            // Back face (Z = -0.5)
            0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
            -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
            -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
            0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
            -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
            0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,

            // Left face (X = -0.5)
            -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
            -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
            -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
            -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f,

            // Right face (X = +0.5)
            0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f,

            // Top face (Y = +0.5)
            -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
            -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
            -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,

            // Bottom face (Y = -0.5)
            -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
            0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
            0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
            -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
            0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
            -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f
        };
    }

    std::vector<float> MeshFactory::createSphere(int slices, int stacks) {
        if (slices < 3 || stacks < 2) {
            throw std::invalid_argument("Sphere requires at least 3 slices and 2 stacks");
        }
        std::vector<float> vertices;
        vertices.reserve(static_cast<std::size_t>(slices) * (stacks - 1) * 6 * 6);
        const auto point = [=](int stack, int slice) -> std::array<float, 3> {
            // Exact poles and a shared seam avoid tiny cracks between triangles.
            if (stack == 0) return {0.0f, 1.0f, 0.0f};
            if (stack == stacks) return {0.0f, -1.0f, 0.0f};
            const float latitude = std::numbers::pi_v<float> * stack / stacks;
            const float longitude = 2.0f * std::numbers::pi_v<float> * (slice % slices) / slices;
            return {std::sin(latitude) * std::cos(longitude), std::cos(latitude),
                    std::sin(latitude) * std::sin(longitude)};
        };
        const auto appendVertex = [&](const std::array<float, 3> &normal) {
            // On a sphere the unit radial vector is also the surface normal.
            vertices.insert(vertices.end(), {normal[0] * 0.5f, normal[1] * 0.5f, normal[2] * 0.5f,
                                             normal[0], normal[1], normal[2]});
        };
        const auto triangle = [&](const auto &a, const auto &b, const auto &c) {
            appendVertex(a);
            appendVertex(b);
            appendVertex(c);
        };
        for (int stack = 0; stack < stacks; ++stack) {
            for (int slice = 0; slice < slices; ++slice) {
                const auto a = point(stack, slice);
                const auto b = point(stack + 1, slice);
                const auto c = point(stack + 1, slice + 1);
                const auto d = point(stack, slice + 1);
                // Outward winding; only one triangle per slice at each pole.
                if (stack > 0) triangle(a, d, b);
                if (stack < stacks - 1) triangle(d, c, b);
            }
        }
        return vertices;
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
            // Position         // Normal

            -h, 0.0f, -h, 0.0f, 1.0f, 0.0f,

            h, 0.0f, h, 0.0f, 1.0f, 0.0f,

            -h, 0.0f, h, 0.0f, 1.0f, 0.0f,

            -h, 0.0f, -h, 0.0f, 1.0f, 0.0f,

            h, 0.0f, -h, 0.0f, 1.0f, 0.0f,

            h, 0.0f, h, 0.0f, 1.0f, 0.0f
        };
    }
}
