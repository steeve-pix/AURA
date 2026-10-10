#include "aura/render/BodyRenderer.hpp"
#include "aura/render/MeshFactory.hpp"
#include "aura/render/CapsuleTransform.hpp"
#include "aura/body/BodyPartTransform.hpp"
#include "aura/body/JointGeometry.hpp"
#include <algorithm>

namespace aura::render {
    BodyRenderer::BodyRenderer()
        : jointSphere{
            MeshFactory::createSphere(48, 32),
            MeshPrimitive::Triangles,
            VertexLayout::PositionNormal
        },
          cylinder{
            MeshFactory::createCylinder(48),
            MeshPrimitive::Triangles,
            VertexLayout::PositionNormal
        },
          sphereLines{MeshFactory::createSphereLines(), MeshPrimitive::Lines},
          topSphereLines{
            MeshFactory::createSphereLines(48, 1), MeshPrimitive::Lines
        },
          bottomSphereLines{
            MeshFactory::createSphereLines(48, -1), MeshPrimitive::Lines
        },
          cylinderLines{
            MeshFactory::createCylinderLines(), MeshPrimitive::Lines
        },
          roundedBox{
            MeshFactory::createRoundedBox(), MeshPrimitive::Triangles,
            VertexLayout::PositionNormal
        },
          soleLines{MeshFactory::createSoleLines(), MeshPrimitive::Lines},
          noLines{std::vector<float>{}, MeshPrimitive::Lines} {}

    void BodyRenderer::draw(const body::AuraBody3D& auraBody, std::span<const JointMarker> joints,
                            const Shader& shader, bool colored, bool details) const {
        const auto drawPiece = [&](const math::Mat4 &model, const Mesh &solid,
                                   const Mesh &lines) {
            shader.setMat4("uModel", model);
            (details ? lines : solid).draw();
        };
        if (colored) shader.setVec3("uColor", {0.96f, 0.97f, 1.0f});
        drawPiece(body::modelMatrix(auraBody.head), jointSphere, sphereLines);
        const auto pelvisBase = math::Mat4::translation(auraBody.pelvis.body.position) *
                                math::Mat4::rotation(auraBody.pelvis.body.orientation);
        drawPiece(pelvisBase * math::Mat4::scale({0.9f, 0.95f, 0.85f}), jointSphere, sphereLines);

        const auto &chest = auraBody.torso.size;
        const auto torsoBase = math::Mat4::translation(auraBody.torso.body.position) *
                               math::Mat4::rotation(auraBody.torso.body.orientation);
        // Equal dimensions make the chest spherical, so it stays round from every view.
        constexpr float chestDiameter = 1.45f;
        drawPiece(torsoBase * math::Mat4::scale({chestDiameter, chestDiameter, chestDiameter}),
                  jointSphere, sphereLines);
        for (float side: {-1.0f, 1.0f}) {
            drawPiece(torsoBase * math::Mat4::translation({side * 0.7f, 0.35f, 0.0f}) *
                      math::Mat4::scale({0.5f, 0.4f, 0.65f}), jointSphere, noLines);
        }
        // A small capsule connects the chest and pelvis without filling out the abdomen.
        const auto chestBottom = body::localToWorldPoint(auraBody.torso, {0.0f, -chest.y * 0.5f, 0.0f});
        const auto pelvisTop = body::localToWorldPoint(auraBody.pelvis, {0.0f, 0.475f, 0.0f});
        const auto span = chestBottom - pelvisTop;
        const float spanLength = span.length();
        const auto localDirection = spanLength > 1e-5f
                                        ? auraBody.torso.body.orientation.conjugate().rotate(span * (1.0f / spanLength))
                                        : math::Vec3{0.0f, 1.0f, 0.0f};
        // Shortest rotation from local Y to the connection direction; handle the opposite pole.
        const auto alignment = localDirection.y < -0.9999f
                                   ? math::Quaternion{0.0f, 1.0f, 0.0f, 0.0f}
                                   : math::Quaternion{
                                       1.0f + localDirection.y, localDirection.z, 0.0f, -localDirection.x
                                   }.normalized();
        body::BodyPart3D waistVisual;
        waistVisual.body.position = (chestBottom + pelvisTop) * 0.5f;
        waistVisual.body.orientation = auraBody.torso.body.orientation * alignment;
        waistVisual.size = {0.55f, std::max(0.55f, spanLength + 0.18f), 0.55f};
        const auto waistCapsule = capsuleTransforms(waistVisual);
        const auto waistDepth = math::Mat4::scale({1.0f, 1.0f, 0.7f / 0.55f});
        if (waistVisual.size.y > waistVisual.size.x)
            drawPiece(waistCapsule.cylinder * waistDepth, cylinder, cylinderLines);
        drawPiece(waistCapsule.topSphere * waistDepth, jointSphere, topSphereLines);
        drawPiece(waistCapsule.bottomSphere * waistDepth, jointSphere, bottomSphereLines);

        struct CapsulePart {
            const body::BodyPart3D *part;
            float topInset, bottomInset;
        };
        constexpr float inset = 0.08f;
        const CapsulePart limbs[] = {
            {&auraBody.neck, 0.0f, 0.0f},
            {&auraBody.leftUpperArm, inset, inset}, {&auraBody.rightUpperArm, inset, inset},
            {&auraBody.leftForearm, inset, inset}, {&auraBody.rightForearm, inset, inset},
            {&auraBody.leftThigh, inset, inset}, {&auraBody.rightThigh, inset, inset},
            {&auraBody.leftShin, inset, inset}, {&auraBody.rightShin, inset, inset}
        };
        for (const auto &limb: limbs) {
            const auto capsule = capsuleTransforms(*limb.part, limb.topInset, limb.bottomInset);
            // Short parts collapse to a sphere; skip the zero-height cylinder (singular normal matrix).
            if (limb.part->size.y - limb.topInset - limb.bottomInset >
                std::min(limb.part->size.x, limb.part->size.z))
                drawPiece(capsule.cylinder, cylinder, cylinderLines);
            drawPiece(capsule.topSphere, jointSphere, topSphereLines);
            drawPiece(capsule.bottomSphere, jointSphere, bottomSphereLines);
        }
        for (const auto *part: {&auraBody.leftHand, &auraBody.rightHand}) {
            drawPiece(body::modelMatrix(*part), roundedBox, noLines);
            const float thumbSide = part == &auraBody.leftHand ? 1.0f : -1.0f;
            const auto handBase = math::Mat4::translation(part->body.position) *
                                  math::Mat4::rotation(part->body.orientation);
            drawPiece(handBase * math::Mat4::translation({thumbSide * 0.15f, 0.06f, 0.04f}) *
                      math::Mat4::scale({0.14f, 0.24f, 0.16f}), jointSphere, noLines);
        }
        for (const auto *part: {&auraBody.leftFoot, &auraBody.rightFoot})
            drawPiece(body::modelMatrix(*part), roundedBox, soleLines);

        if (colored) shader.setVec3("uColor", {0.15f, 0.17f, 0.24f});
        for (const auto &joint: joints) {
            if (joint.markerRadius == 0.0f) continue;
            const auto a = body::localToWorldPoint(*joint.partA, joint.constraint.localAnchorA);
            const auto b = body::localToWorldPoint(*joint.partB, joint.constraint.localAnchorB);
            const float diameter = joint.markerRadius * 2.0f;
            drawPiece(math::Mat4::translation((a + b) * 0.5f) *
                      math::Mat4::scale({diameter, diameter, diameter}), jointSphere, sphereLines);
        }
    }
}
