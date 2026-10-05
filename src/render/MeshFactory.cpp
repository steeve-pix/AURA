#include "aura/render/MeshFactory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "aura/math/Vec3.hpp"

namespace aura::render {
    namespace {
        constexpr int profileRings = 32;
        constexpr int profileSegments = 48;
        float torsoRadius(float y) {
            if (y < -0.1f) {
                const float t = (y + 0.5f) / 0.4f;
                return 0.3f + 0.18f * std::sin(t * std::numbers::pi_v<float> * 0.5f);
            }
            if (y < 0.3f) return 0.48f + 0.02f * (y + 0.1f) / 0.4f;
            const float t = (y - 0.3f) / 0.2f;
            return 0.5f - 0.09f * t * t;
        }
        float waistRadius(float y) {
            // A gentle waist, with broad ends that blend into chest and pelvis.
            const float t = 2.0f * y;
            return 0.42f + 0.045f * t * t - 0.005f * t;
        }
        float pelvisRadius(float y) {
            if (y < 0.0f) return 0.5f * std::sqrt(std::max(0.0f, 1.0f - 4.0f * y * y));
            const float t = 2.0f * y;
            return 0.5f - 0.1f * t * t * (3.0f - 2.0f * t);
        }
        // Revolve a radius-versus-height curve around Y, then close both ends.
        // The curve's slope gives the vertical component of each surface normal.
        template<class Profile>
        std::vector<float> profileMesh(Profile radiusAt) {
            std::vector<float> result;
            const auto vertex = [&](int ring, int segment) {
                const float y = -0.5f + static_cast<float>(ring) / profileRings;
                const float angle = 2.0f * std::numbers::pi_v<float> * (segment % profileSegments) / profileSegments;
                const float low = std::max(-0.5f, y - 0.001f), high = std::min(0.5f, y + 0.001f);
                const auto normal = math::Vec3{std::cos(angle) * (high - low),
                    radiusAt(low) - radiusAt(high), std::sin(angle) * (high - low)}.normalized();
                result.insert(result.end(), {radiusAt(y) * std::cos(angle), y, radiusAt(y) * std::sin(angle),
                                             normal.x, normal.y, normal.z});
            };
            for (int ring = 0; ring < profileRings; ++ring) for (int segment = 0; segment < profileSegments; ++segment) {
                if (radiusAt(-0.5f + static_cast<float>(ring) / profileRings) > 0.0f) {
                    vertex(ring, segment); vertex(ring + 1, segment); vertex(ring, segment + 1);
                }
                if (radiusAt(-0.5f + static_cast<float>(ring + 1) / profileRings) > 0.0f) {
                    vertex(ring + 1, segment); vertex(ring + 1, segment + 1); vertex(ring, segment + 1);
                }
            }
            for (float y : {-0.5f, 0.5f}) for (int segment = 0; segment < profileSegments; ++segment) {
                if (radiusAt(y) == 0.0f) continue;
                result.insert(result.end(), {0.0f, y, 0.0f, 0.0f, y * 2.0f, 0.0f});
                const std::array corners = y > 0.0f ? std::array{segment + 1, segment} : std::array{segment, segment + 1};
                for (int corner : corners) {
                    const float angle = 2.0f * std::numbers::pi_v<float> * corner / profileSegments;
                    result.insert(result.end(), {radiusAt(y) * std::cos(angle), y, radiusAt(y) * std::sin(angle),
                                                 0.0f, y * 2.0f, 0.0f});
                }
            }
            return result;
        }
        template<class Profile>
        std::vector<float> profileLines(Profile radiusAt, bool equator) {
            std::vector<float> result;
            if (equator) for (int segment = 0; segment < profileSegments; ++segment) {
                for (int corner : {segment, segment + 1}) {
                    const float angle = 2.0f * std::numbers::pi_v<float> * corner / profileSegments;
                    result.insert(result.end(), {radiusAt(0.0f) * std::cos(angle), 0.0f, radiusAt(0.0f) * std::sin(angle)});
                }
            }
            for (int meridian = 0; meridian < 4; ++meridian) for (int ring = 0; ring < profileRings; ++ring) {
                const float angle = meridian * std::numbers::pi_v<float> * 0.5f;
                for (int end : {ring, ring + 1}) {
                    const float y = -0.5f + static_cast<float>(end) / profileRings;
                    result.insert(result.end(), {radiusAt(y) * std::cos(angle), y, radiusAt(y) * std::sin(angle)});
                }
            }
            return result;
        }
    }

    std::vector<float> MeshFactory::createTorso() { return profileMesh(torsoRadius); }
    std::vector<float> MeshFactory::createTorsoLines() { return profileLines(torsoRadius, true); }
    std::vector<float> MeshFactory::createWaist() { return profileMesh(waistRadius); }
    std::vector<float> MeshFactory::createWaistLines() { return profileLines(waistRadius, false); }
    std::vector<float> MeshFactory::createPelvis() { return profileMesh(pelvisRadius); }
    std::vector<float> MeshFactory::createPelvisLines() { return profileLines(pelvisRadius, true); }

    std::vector<float> MeshFactory::createRoundedBox() {
        std::vector<float> result;
        constexpr int subdivisions = 12;
        constexpr float bevel = 0.14f;
        const auto vertex = [&](int axis, float side, int u, int v) {
            std::array<float, 3> point{};
            point[axis] = side * 0.5f;
            point[(axis + 1) % 3] = -0.5f + static_cast<float>(u) / subdivisions;
            point[(axis + 2) % 3] = -0.5f + static_cast<float>(v) / subdivisions;
            const math::Vec3 core{std::clamp(point[0], -0.5f + bevel, 0.5f - bevel),
                std::clamp(point[1], -0.5f + bevel, 0.5f - bevel),
                std::clamp(point[2], -0.5f + bevel, 0.5f - bevel)};
            // Project onto a smaller box, then offset outward to round its edges and corners.
            const auto normal = (math::Vec3{point[0], point[1], point[2]} - core).normalized();
            const auto position = core + normal * bevel;
            result.insert(result.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z});
        };
        for (int axis = 0; axis < 3; ++axis) for (float side : {-1.0f, 1.0f})
            for (int u = 0; u < subdivisions; ++u) for (int v = 0; v < subdivisions; ++v) {
                const int du = side > 0.0f ? 1 : 0, dv = side > 0.0f ? 0 : 1;
                vertex(axis, side, u, v); vertex(axis, side, u + du, v + dv); vertex(axis, side, u + 1, v + 1);
                vertex(axis, side, u, v); vertex(axis, side, u + 1, v + 1); vertex(axis, side, u + dv, v + du);
            }
        return result;
    }

    std::vector<float> MeshFactory::createSoleLines() {
        std::vector<float> result;
        constexpr float bevel = 0.14f;
        for (int corner = 0; corner < 4; ++corner) for (int segment = 0; segment < 12; ++segment) {
            const float quadrant = corner * std::numbers::pi_v<float> * 0.5f;
            const float centerX = std::cos(quadrant + 0.25f * std::numbers::pi_v<float>) > 0.0f ? 0.36f : -0.36f;
            const float centerZ = std::sin(quadrant + 0.25f * std::numbers::pi_v<float>) > 0.0f ? 0.36f : -0.36f;
            for (int end : {segment, segment + 1}) {
                const float angle = quadrant + end * std::numbers::pi_v<float> / 24.0f;
                result.insert(result.end(), {centerX + bevel * std::cos(angle), -0.27f,
                                             centerZ + bevel * std::sin(angle)});
            }
        }
        // Straight stretches between the four rounded corners.
        for (float sign : {-1.0f, 1.0f}) {
            result.insert(result.end(), {-0.36f, -0.27f, sign * 0.5f, 0.36f, -0.27f, sign * 0.5f});
            result.insert(result.end(), {sign * 0.5f, -0.27f, -0.36f, sign * 0.5f, -0.27f, 0.36f});
        }
        return result;
    }

    std::vector<float> MeshFactory::createSphereLines(int segments, int hemisphere) {
        if (segments < 4 || hemisphere < -1 || hemisphere > 1)
            throw std::invalid_argument("Invalid sphere line resolution or hemisphere");
        std::vector<float> result;
        const auto line = [&](const math::Vec3 &a, const math::Vec3 &b) {
            result.insert(result.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
        };
        const float pi = std::numbers::pi_v<float>;
        for (int i = 0; i < segments; ++i) {
            const float a = 2.0f * pi * i / segments;
            const float b = 2.0f * pi * (i + 1) / segments;
            line({0.5f * std::cos(a), 0.0f, 0.5f * std::sin(a)},
                 {0.5f * std::cos(b), 0.0f, 0.5f * std::sin(b)});
            const float start = hemisphere < 0 ? pi : 0.0f;
            const float extent = hemisphere == 0 ? 2.0f * pi : pi;
            const float c = start + extent * i / segments;
            const float d = start + extent * (i + 1) / segments;
            line({0.5f * std::cos(c), 0.5f * std::sin(c), 0.0f},
                 {0.5f * std::cos(d), 0.5f * std::sin(d), 0.0f});
            line({0.0f, 0.5f * std::sin(c), 0.5f * std::cos(c)},
                 {0.0f, 0.5f * std::sin(d), 0.5f * std::cos(d)});
        }
        return result;
    }

    std::vector<float> MeshFactory::createCylinderLines(int segments) {
        if (segments < 4) throw std::invalid_argument("Cylinder lines require at least 4 segments");
        std::vector<float> result;
        const float pi = std::numbers::pi_v<float>;
        for (int i = 0; i < segments; ++i) {
            const float a = 2.0f * pi * i / segments;
            const float b = 2.0f * pi * (i + 1) / segments;
            for (float y : {-0.5f, 0.5f})
                result.insert(result.end(), {0.5f * std::cos(a), y, 0.5f * std::sin(a),
                                             0.5f * std::cos(b), y, 0.5f * std::sin(b)});
        }
        for (int i = 0; i < 4; ++i) {
            const float a = 0.5f * pi * i;
            result.insert(result.end(), {0.5f * std::cos(a), -0.5f, 0.5f * std::sin(a),
                                         0.5f * std::cos(a), 0.5f, 0.5f * std::sin(a)});
        }
        return result;
    }

    std::vector<float> MeshFactory::createCubeLines() {
        std::vector<float> result;
        for (int axis = 0; axis < 3; ++axis) {
            for (float a : {-0.5f, 0.5f}) for (float b : {-0.5f, 0.5f}) {
                std::array<float, 3> start{}, end{};
                start[axis] = -0.5f;
                end[axis] = 0.5f;
                start[(axis + 1) % 3] = end[(axis + 1) % 3] = a;
                start[(axis + 2) % 3] = end[(axis + 2) % 3] = b;
                result.insert(result.end(), {start[0], start[1], start[2], end[0], end[1], end[2]});
            }
        }
        return result;
    }

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
            return {
                std::sin(latitude) * std::cos(longitude), std::cos(latitude),
                std::sin(latitude) * std::sin(longitude)
            };
        };
        const auto appendVertex = [&](const std::array<float, 3> &normal) {
            // On a sphere the unit radial vector is also the surface normal.
            vertices.insert(vertices.end(), {
                                normal[0] * 0.5f, normal[1] * 0.5f, normal[2] * 0.5f,
                                normal[0], normal[1], normal[2]
                            });
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

    std::vector<float> MeshFactory::createCylinder(int segments) {
        std::vector<float> vertices;

        if (segments < 3) {
            throw std::invalid_argument("Cylinder requires at least 3 segments");
        }

        const auto addVertex =
                [&](const math::Vec3 &position, const math::Vec3 &normal) {
            vertices.insert(vertices.end(), {
                                position.x, position.y, position.z,
                                normal.x, normal.y, normal.z
                            });
        };

        for (int i{}; i < segments; ++i) {
            float angle0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
            float angle1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i + 1) / static_cast<float>(segments);

            math::Vec3 bottom0{
                0.5f * std::cos(angle0),
                -0.5f,
                0.5f * std::sin(angle0)
            };

            math::Vec3 top0{
                0.5f * std::cos(angle0),
                0.5f,
                0.5f * std::sin(angle0)
            };

            math::Vec3 bottom1{
                0.5f * std::cos(angle1),
                -0.5f,
                0.5f * std::sin(angle1)
            };

            math::Vec3 top1{
                0.5f * std::cos(angle1),
                0.5f,
                0.5f * std::sin(angle1)
            };

            math::Vec3 normal0{
                std::cos(angle0),
                0.0f,
                std::sin(angle0)
            };

            math::Vec3 normal1{
                std::cos(angle1),
                0.0f,
                std::sin(angle1)
            };

            // First triangle
            addVertex(bottom0, normal0);
            addVertex(top0, normal0);
            addVertex(bottom1, normal1);

            // Second triangle
            addVertex(top0, normal0);
            addVertex(top1, normal1);
            addVertex(bottom1, normal1);

            const math::Vec3 up{0.0f, 1.0f, 0.0f};
            const math::Vec3 down{0.0f, -1.0f, 0.0f};

            // Top cap
            addVertex({0.0f, 0.5f, 0.0f}, up);
            addVertex(top1, up);
            addVertex(top0, up);

            // Bottom cap
            addVertex({0.0f, -0.5f, 0.0f}, down);
            addVertex(bottom0, down);
            addVertex(bottom1, down);
        }

        return vertices;
    }
}
