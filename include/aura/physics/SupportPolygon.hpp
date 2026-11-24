#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <vector>

#include "aura/math/Vec3.hpp"

namespace aura::physics {
    struct GroundPoint {
        float x{};
        float z{};
    };

    struct SupportPolygon {
        std::vector<GroundPoint> vertices;
    };

    inline GroundPoint projectToGround(const math::Vec3 &point) noexcept {
        return {
            .x = point.x,
            .z = point.z
        };
    }

    inline float cross(const GroundPoint &a, const GroundPoint &b, const GroundPoint &c) noexcept {
        return (b.x - a.x) * (c.z - a.z)
               - (b.z - a.z) * (c.x - a.x);
    }

    inline float signedDistanceToEdgeLine(const GroundPoint &a, const GroundPoint &b,
                                          const GroundPoint &point) noexcept {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float edgeLength = std::sqrt(dx * dx + dz * dz);
        if (edgeLength == 0.0f) {
            return 0.0f;
        }

        return cross(a, b, point) / edgeLength;
    }

    inline float supportMargin(const SupportPolygon &polygon, const GroundPoint &point) noexcept {
        if (polygon.vertices.size() < 3) {
            return 0.0f;
        }

        float minimumDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
            const auto &a = polygon.vertices[i];
            const auto &b = polygon.vertices[(i + 1) % polygon.vertices.size()];
            minimumDistance = std::min(minimumDistance, signedDistanceToEdgeLine(a, b, point));
        }

        return minimumDistance;
    }

    inline bool containsPoint(const SupportPolygon &polygon, const GroundPoint &point) noexcept {
        if (polygon.vertices.size() < 3) {
            return false;
        }

        for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
            const auto &a = polygon.vertices[i];
            const auto &b = polygon.vertices[(i + 1) % polygon.vertices.size()];
            if (cross(a, b, point) < 0.0f) {
                return false;
            }
        }

        return true;
    }

    inline SupportPolygon convexHull(std::vector<GroundPoint> points) {
        // Step 1: Sort the points.
        std::sort(points.begin(), points.end(), [](const GroundPoint &a, const GroundPoint &b) {
            if (a.x != b.x) {
                return a.x < b.x;
            }
            return a.z < b.z;
        });
        points.erase(
            std::unique(points.begin(), points.end(), [](const GroundPoint &a, const GroundPoint &b) {
                return a.x == b.x && a.z == b.z;
            }),
            points.end()
        );
        if (points.size() <= 2) {
            return {.vertices = points};
        }

        // Step 2: Construct the lower hull.
        std::vector<GroundPoint> lower;
        for (const auto &point: points) {
            while (lower.size() >= 2 &&
                   cross(lower[lower.size() - 2], lower.back(), point) <= 0.0f) {
                lower.pop_back();
            }
            lower.push_back(point);
        }

        // Step 3: Construct the upper hull.
        std::vector<GroundPoint> upper;
        for (const auto &point: std::views::reverse(points)) {
            while (upper.size() >= 2 &&
                   cross(upper[upper.size() - 2], upper.back(), point) <= 0.0f) {
                upper.pop_back();
            }
            upper.push_back(point);
        }

        // Step 4: Combine both hulls without repeating their endpoints.
        lower.pop_back();
        upper.pop_back();
        lower.insert(lower.end(), upper.begin(), upper.end());

        return {.vertices = lower};
    }
}
