#pragma once

#include <vector>

namespace aura::render {
    class MeshFactory {
    public:
        static std::vector<float> createCube();

        static std::vector<float> createGrid(int halfSize, float spacing);

        static std::vector<float> createFloor(float size);
    };
}
