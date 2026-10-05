#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

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

    for (const auto resolution : {std::array{3, 2}, std::array{24, 16}}) {
        const auto sphere = aura::render::MeshFactory::createSphere(resolution[0], resolution[1]);
        if (sphere.size() != static_cast<std::size_t>(resolution[0] * (resolution[1] - 1) * 36)) {
            std::cerr << "FAIL sphere triangle count\n";
            return 1;
        }
        for (std::size_t i = 0; i < sphere.size(); i += 6) {
            float radiusSquared = 0.0f;
            float normalSquared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float position = sphere[i + axis];
                const float normal = sphere[i + axis + 3];
                if (!std::isfinite(position) || !std::isfinite(normal) ||
                    std::abs(normal - 2.0f * position) > 1e-5f) {
                    std::cerr << "FAIL sphere radial normals\n";
                    return 1;
                }
                radiusSquared += position * position;
                normalSquared += normal * normal;
            }
            if (std::abs(radiusSquared - 0.25f) > 1e-5f || std::abs(normalSquared - 1.0f) > 1e-5f) {
                std::cerr << "FAIL sphere radius or unit normals\n";
                return 1;
            }
        }
        for (std::size_t i = 0; i < sphere.size(); i += 18) {
            std::array<float, 3> ab{}, ac{}, centre{};
            for (int axis = 0; axis < 3; ++axis) {
                ab[axis] = sphere[i + 6 + axis] - sphere[i + axis];
                ac[axis] = sphere[i + 12 + axis] - sphere[i + axis];
                centre[axis] = sphere[i + axis] + sphere[i + 6 + axis] + sphere[i + 12 + axis];
            }
            const std::array cross{ab[1] * ac[2] - ab[2] * ac[1],
                                   ab[2] * ac[0] - ab[0] * ac[2],
                                   ab[0] * ac[1] - ab[1] * ac[0]};
            const float outward = cross[0] * centre[0] + cross[1] * centre[1] + cross[2] * centre[2];
            if (!(outward > 1e-8f)) {
                std::cerr << "FAIL sphere: degenerate triangle or inward winding\n";
                return 1;
            }
        }
    }
    for (const auto invalid : {std::array{2, 16}, std::array{24, 1}}) {
        try {
            aura::render::MeshFactory::createSphere(invalid[0], invalid[1]);
            std::cerr << "FAIL sphere accepted invalid resolution\n";
            return 1;
        } catch (const std::invalid_argument &) {
        }
    }
    // Hemisphere lines must stay on the visible cap, never cross through the cylinder.
    for (int hemisphere : {-1, 0, 1}) {
        const auto lines = aura::render::MeshFactory::createSphereLines(48, hemisphere);
        if (lines.empty() || lines.size() % 6 != 0) return 1;
        for (std::size_t i = 0; i < lines.size(); i += 3) {
            const float x = lines[i], y = lines[i + 1], z = lines[i + 2];
            if (!std::isfinite(x + y + z) || std::abs(x*x + y*y + z*z - 0.25f) > 1e-5f ||
                hemisphere * y < -1e-5f) {
                std::cerr << "FAIL sphere lines: wrong radius or cap\n";
                return 1;
            }
        }
    }
    const auto edges = aura::render::MeshFactory::createCubeLines();
    if (edges.size() != 12 * 6) return 1;
    for (std::size_t i = 0; i < edges.size(); i += 6) {
        int differingAxes = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(edges[i + axis]) != 0.5f || std::abs(edges[i + 3 + axis]) != 0.5f) return 1;
            differingAxes += edges[i + axis] != edges[i + 3 + axis];
        }
        if (differingAxes != 1) return 1;
    }
    for (const auto &torso : {aura::render::MeshFactory::createTorso(),
                             aura::render::MeshFactory::createWaist(),
                             aura::render::MeshFactory::createPelvis(),
                             aura::render::MeshFactory::createRoundedBox()}) {
        if (torso.empty() || torso.size() % 18 != 0) return 1;
        for (std::size_t i = 0; i < torso.size(); i += 6) {
            float normalLengthSquared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(torso[i + axis]) || std::abs(torso[i + axis]) > 0.50001f ||
                    !std::isfinite(torso[i + axis + 3])) return 1;
                normalLengthSquared += torso[i + axis + 3] * torso[i + axis + 3];
            }
            if (std::abs(normalLengthSquared - 1.0f) > 1e-5f) return 1;
        }
        // Outward faces are necessary for the outline's front-face culling.
        for (std::size_t i = 0; i < torso.size(); i += 18) {
            std::array<float, 3> ab{}, ac{}, normal{};
            for (int axis = 0; axis < 3; ++axis) {
                ab[axis] = torso[i + 6 + axis] - torso[i + axis];
                ac[axis] = torso[i + 12 + axis] - torso[i + axis];
                normal[axis] = torso[i + 3 + axis] + torso[i + 9 + axis] + torso[i + 15 + axis];
            }
            const float outward = (ab[1]*ac[2] - ab[2]*ac[1])*normal[0] +
                (ab[2]*ac[0] - ab[0]*ac[2])*normal[1] + (ab[0]*ac[1] - ab[1]*ac[0])*normal[2];
            if (!(outward > 1e-8f)) {
                std::cerr << "FAIL body mesh: inward or degenerate triangle\n";
                return 1;
            }
        }
    }
    std::cout << "Mesh geometry, normals, winding, and surface line checks passed.\n";
    return 0;
}
