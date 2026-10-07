#include "aura/physics/SelfCollision.hpp"
#include <algorithm>
#include <array>

namespace aura::physics {
    namespace {
        template<class Body> auto partsOf(Body &body) {
            return std::array{&body.head, &body.neck, &body.torso, &body.pelvis,
                &body.leftUpperArm, &body.leftForearm, &body.leftHand,
                &body.rightUpperArm, &body.rightForearm, &body.rightHand,
                &body.leftThigh, &body.leftShin, &body.leftFoot,
                &body.rightThigh, &body.rightShin, &body.rightFoot};
        }
    }

    CollisionCapsule bodyPartCollisionCapsule(const body::BodyPart3D &part) {
        const float radius = 0.5f * std::min(part.size.x, part.size.z);
        const float halfSegment = std::max(0.0f, 0.5f * part.size.y - radius);
        const auto orientation = part.body.orientation.normalized();
        return {part.body.position + orientation.rotate({0,-halfSegment,0}),
                part.body.position + orientation.rotate({0,halfSegment,0}), radius};
    }

    SegmentClosestPoints closestPointsBetweenSegments(
        const math::Vec3 &a0, const math::Vec3 &a1,
        const math::Vec3 &b0, const math::Vec3 &b1) {
        const auto u = a1-a0, v = b1-b0, r = a0-b0;
        // Solve the squared-distance minimum with fractions clamped to [0,1].
        // Double coefficients avoid loss of accuracy for almost parallel lines.
        const auto dot = [](const auto &a, const auto &b) {
            return double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z;
        };
        const double uu = dot(u,u), vv = dot(v,v), uv = dot(u,v);
        const double ur = dot(u,r), vr = dot(v,r);
        const auto unit = [](double value) { return std::clamp(value,0.0,1.0); };
        double s=0,t=0;
        if (uu==0 && vv==0) return {a0,b0,0,0};
        if (uu==0) t=unit(vr/vv);
        else if (vv==0) s=unit(-ur/uu);
        else {
            // |u x v|^2 equals uu*vv-uv*uv, without subtracting nearly
            // identical squared lengths. Exactly parallel segments use s=0.
            const double x=double(u.y)*v.z-double(u.z)*v.y;
            const double y=double(u.z)*v.x-double(u.x)*v.z;
            const double z=double(u.x)*v.y-double(u.y)*v.x;
            const double denominator=x*x+y*y+z*z;
            if (denominator>0) s=unit((uv*vr-ur*vv)/denominator);
            t=(uv*s+vr)/vv;
            if (t<0) { t=0; s=unit(-ur/uu); }
            else if (t>1) { t=1; s=unit((uv-ur)/uu); }
        }
        return {a0+u*static_cast<float>(s),b0+v*static_cast<float>(t),
                static_cast<float>(s),static_cast<float>(t)};
    }

    float capsulePenetration(const CollisionCapsule &a, const CollisionCapsule &b) {
        const auto points=closestPointsBetweenSegments(a.pointA,a.pointB,b.pointA,b.pointB);
        return std::max(0.0f,a.radius+b.radius-(points.pointB-points.pointA).length());
    }

    CollisionSphere collisionSphere(const body::BodyPart3D &part) {
        return {part.body.position, 0.5f * std::min({part.size.x, part.size.y, part.size.z})};
    }

    bool shouldSelfCollide(const body::BodyPart3D &a, const body::BodyPart3D &b,
                           const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton) {
        return &a != &b && !skeleton.areDirectlyConnected(owner, a, b);
    }

    void resolveSelfCollision(body::BodyPart3D &a, body::BodyPart3D &b) {
        if (&a == &b) return;
        const auto sphereA = collisionSphere(a), sphereB = collisionSphere(b);
        const auto delta = sphereB.center - sphereA.center;
        const float distance = delta.length();
        const float penetration = sphereA.radius + sphereB.radius - distance;
        if (penetration <= 0.0f) return;
        const auto normal = distance > 0.0f ? delta * (1.0f / distance) : math::Vec3{1, 0, 0};
        const float inverseA = 1.0f / a.body.mass, inverseB = 1.0f / b.body.mass;
        const float total = inverseA + inverseB;
        a.body.position -= normal * (penetration * inverseA / total);
        b.body.position += normal * (penetration * inverseB / total);
    }

    void resolveBodySelfCollisions(body::AuraBody3D &body, const body::AuraSkeleton3D &skeleton) {
        const auto parts = partsOf(body);
        for (std::size_t i = 0; i < parts.size(); ++i)
            for (std::size_t j = i + 1; j < parts.size(); ++j)
                if (shouldSelfCollide(*parts[i], *parts[j], body, skeleton))
                    resolveSelfCollision(*parts[i], *parts[j]);
    }

    bool componentTranslationIsSelfCollisionSafe(
        const body::AuraBody3D &owner, const body::AuraSkeleton3D &skeleton,
        const std::vector<body::BodyPart3D *> &component,
        const math::Vec3 &delta, float tolerance) {
        const auto parts = partsOf(owner);
        for (const auto *a : component) for (const auto *b : parts) {
            if (std::find(component.begin(), component.end(), b) != component.end()) continue;
            if (!shouldSelfCollide(*a, *b, owner, skeleton)) continue;
            const auto sphereA = collisionSphere(*a), sphereB = collisionSphere(*b);
            const float before = sphereA.radius + sphereB.radius - (sphereB.center-sphereA.center).length();
            const float after = sphereA.radius + sphereB.radius - (sphereB.center-sphereA.center-delta).length();
            if (after > std::max(0.0f, before) + tolerance) return false;
        }
        return true;
    }

    float maximumSelfCollisionPenetration(const body::AuraBody3D &body,
                                          const body::AuraSkeleton3D &skeleton) {
        const auto parts = partsOf(body);
        float maximum = 0.0f;
        for (std::size_t i = 0; i < parts.size(); ++i)
            for (std::size_t j = i + 1; j < parts.size(); ++j) {
                if (!shouldSelfCollide(*parts[i], *parts[j], body, skeleton)) continue;
                const auto a = collisionSphere(*parts[i]), b = collisionSphere(*parts[j]);
                maximum = std::max(maximum, a.radius + b.radius - (b.center - a.center).length());
            }
        return maximum;
    }
}
