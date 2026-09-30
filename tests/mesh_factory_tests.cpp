#include <array>
#include <iostream>

#include "aura/render/MeshFactory.hpp"

int main() {
    const auto cube = aura::render::MeshFactory::createCube();
    if (cube.size() != 36 * 6) {
        std::cerr << "FAIL cube: expected 36 position-normal vertices, got "
                  << cube.size() << " floats\n";
        return 1;
    }

    const std::array<std::array<float, 3>, 6> faceNormals{{
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}
    }};

    for (std::size_t face = 0; face < faceNormals.size(); ++face) {
        for (std::size_t vertex = 0; vertex < 6; ++vertex) {
            const std::size_t normal = (face * 6 + vertex) * 6 + 3;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (cube[normal + axis] != faceNormals[face][axis]) {
                    std::cerr << "FAIL cube normal at face " << face
                              << ", vertex " << vertex << '\n';
                    return 1;
                }
            }
        }
    }

    std::cout << "All 36 cube vertices have the expected face normals.\n";
    return 0;
}
