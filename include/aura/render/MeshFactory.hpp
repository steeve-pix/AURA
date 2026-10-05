#pragma once

#include <vector>

namespace aura::render {
    class MeshFactory {
    public:
        static std::vector<float> createCube();

        // Unit diameter, centred at the origin; interleaved positions and normals.
        static std::vector<float> createSphere(int slices = 24, int stacks = 16);

        static std::vector<float> createGrid(int halfSize, float spacing);

        static std::vector<float> createFloor(float size);

        static std::vector<float> createCylinder(int segments);
    };
}
