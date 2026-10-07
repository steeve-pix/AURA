#pragma once

#include <vector>
#include "aura/math/Vec3.hpp"

namespace aura::render {
    class MeshFactory {
    public:
        static std::vector<float> createCube();

        // Unit diameter, centred at the origin; interleaved positions and normals.
        static std::vector<float> createSphere(int slices = 24, int stacks = 16);

        static std::vector<float> createGrid(int halfSize, float spacing);

        static std::vector<float> createFloor(float size);

        static std::vector<float> createCylinder(int segments);

        // Sparse surface lines, rather than every triangle in the mesh.
        // hemisphere: 0 = whole sphere, +1 = upper cap, -1 = lower cap.
        static std::vector<float> createSphereLines(int segments = 48, int hemisphere = 0);
        static std::vector<float> createCylinderLines(int segments = 48);
        static std::vector<float> createCubeLines();
        // Wire grids on four walls and the ceiling, centred horizontally at Y=0.
        static std::vector<float> createRoomLines(const math::Vec3 &size, float spacing = 2.0f);

        // Broad, flat shoulder line and a rounded, tapered lower chest.
        static std::vector<float> createTorso();
        static std::vector<float> createTorsoLines();
        static std::vector<float> createWaist();
        static std::vector<float> createWaistLines();
        static std::vector<float> createPelvis();
        static std::vector<float> createPelvisLines();
        static std::vector<float> createRoundedBox();
        static std::vector<float> createSoleLines();
    };
}
