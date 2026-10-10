#pragma once
#include <span>
#include "Mesh.hpp"
#include "Shader.hpp"
#include "aura/body/AuraBody3D.hpp"
#include "aura/body/Joint3D.hpp"

namespace aura::render {
    struct JointMarker {
        const body::BodyPart3D* partA;
        const body::BodyPart3D* partB;
        const body::Joint3D& constraint;
        float markerRadius;
    };

    // Owns visual meshes only. Requires an active GL context during its lifetime.
    // Draws read-only body state; visual ornaments never modify collision shapes.
    class BodyRenderer {
    public:
        BodyRenderer();
        void draw(const body::AuraBody3D& auraBody, std::span<const JointMarker> joints,
                  const Shader& shader, bool colored, bool details = false) const;
    private:
        Mesh jointSphere, cylinder, sphereLines, topSphereLines, bottomSphereLines, cylinderLines, roundedBox, soleLines, noLines;
    };
}
