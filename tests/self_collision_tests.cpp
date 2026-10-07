#include <cmath>
#include <iostream>
#include "aura/physics/SelfCollision.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::cerr << "FAIL " << message << '\n'; passed = false; }
    };
    auto body = body::createAuraBody3D();
    auto skeleton = body::createAuraSkeleton3D(body);
    check(!physics::shouldSelfCollide(body.head, body.head, body, skeleton), "same part excluded");
    const std::pair<body::BodyPart3D*, body::BodyPart3D*> neighbors[]{
        {&body.torso,&body.pelvis},{&body.torso,&body.neck},{&body.neck,&body.head},
        {&body.torso,&body.leftUpperArm},{&body.leftUpperArm,&body.leftForearm},{&body.leftForearm,&body.leftHand},
        {&body.torso,&body.rightUpperArm},{&body.rightUpperArm,&body.rightForearm},{&body.rightForearm,&body.rightHand},
        {&body.pelvis,&body.leftThigh},{&body.leftThigh,&body.leftShin},{&body.leftShin,&body.leftFoot},
        {&body.pelvis,&body.rightThigh},{&body.rightThigh,&body.rightShin},{&body.rightShin,&body.rightFoot}};
    for (const auto &[a,b] : neighbors) {
        check(!physics::shouldSelfCollide(*a,*b,body,skeleton) &&
              !physics::shouldSelfCollide(*b,*a,body,skeleton), "all fifteen neighbors excluded symmetrically");
    }
    // The intended assembled pose must satisfy anchors and the experimental
    // eligible sphere pairs before any dynamics or geometric projection runs.
    const body::Joint3D *neutralJoints[]{&skeleton.waist,&skeleton.neck,&skeleton.head,
        &skeleton.leftShoulder,&skeleton.leftElbow,&skeleton.leftWrist,
        &skeleton.rightShoulder,&skeleton.rightElbow,&skeleton.rightWrist,
        &skeleton.leftHip,&skeleton.leftKnee,&skeleton.leftAnkle,
        &skeleton.rightHip,&skeleton.rightKnee,&skeleton.rightAnkle};
    for (std::size_t i=0;i<15;++i) {
        const auto &[a,b]=neighbors[i]; const auto &joint=*neutralJoints[i];
        const auto worldA=a->body.position+a->body.orientation.rotate(joint.localAnchorA);
        const auto worldB=b->body.position+b->body.orientation.rotate(joint.localAnchorB);
        check((worldB-worldA).length()<1e-6f, "neutral joint anchors aligned");
    }
    check(physics::maximumSelfCollisionPenetration(body,skeleton)<1e-6f,
          "all eligible neutral sphere pairs are compatible");
    const float thighDistance=(body.rightThigh.body.position-body.leftThigh.body.position).length();
    const float thighRadii=physics::collisionSphere(body.leftThigh).radius+physics::collisionSphere(body.rightThigh).radius;
    check(thighDistance>thighRadii && std::abs(thighDistance-thighRadii-0.1f)<1e-6f,
          "neutral thighs have positive sphere clearance");
    body.leftHand.name = body.torso.name;
    check(physics::shouldSelfCollide(body.leftHand,body.torso,body,skeleton), "filter independent of names");
    auto copy = body;
    check(!physics::shouldSelfCollide(copy.leftShin,copy.leftFoot,copy,skeleton), "topology works with copied body");
    check(physics::collisionSphere(body.torso).radius == 0.4f, "sphere uses smallest physical dimension");

    for (auto [a,b] : {std::pair{&body.leftHand,&body.torso},
                       std::pair{&body.leftShin,&body.rightShin},
                       std::pair{&body.leftForearm,&body.rightThigh}}) {
        a->body.position = {0,2,0}; b->body.position = {0.1f,2,0};
        a->body.mass = 1; b->body.mass = 3;
        a->body.velocity = {1,2,3}; b->body.angularVelocity = {4,5,6};
        a->body.orientation = math::Quaternion::fromAxisAngle({0,1,0},0.7f);
        const auto oldA = a->body, oldB = b->body;
        const auto momentumPosition = oldA.position*oldA.mass + oldB.position*oldB.mass;
        const float radiusSum = physics::collisionSphere(*a).radius + physics::collisionSphere(*b).radius;
        physics::resolveSelfCollision(*a,*b);
        check(std::abs((b->body.position-a->body.position).length()-radiusSum)<1e-6f, "overlapping allowed pair separates");
        check((a->body.position*a->body.mass+b->body.position*b->body.mass-momentumPosition).length()<1e-6f,
              "inverse-mass correction preserves pair COM");
        check((a->body.velocity-oldA.velocity).lengthSquared()==0 &&
              (b->body.velocity-oldB.velocity).lengthSquared()==0 &&
              (a->body.angularVelocity-oldA.angularVelocity).lengthSquared()==0 &&
              (b->body.angularVelocity-oldB.angularVelocity).lengthSquared()==0 &&
              a->body.orientation.w==oldA.orientation.w && a->body.orientation.y==oldA.orientation.y,
              "projection changes neither velocity nor orientation");
    }
    body::BodyPart3D a,b;
    a.body.position = b.body.position = {0,2,0};
    physics::resolveSelfCollision(a,b);
    check(std::abs((b.body.position-a.body.position).length()-1)<1e-6f, "coincident centers separate without NaN");
    const auto separatedA = a.body.position, separatedB = b.body.position;
    physics::resolveSelfCollision(a,b);
    check((a.body.position-separatedA).lengthSquared()==0 && (b.body.position-separatedB).lengthSquared()==0,
          "touching pair unchanged");
    b.body.position.x += 10;
    const auto distant = b.body.position;
    physics::resolveSelfCollision(a,b);
    check((b.body.position-distant).lengthSquared()==0, "distant pair unchanged");

    // Isolate one overlapping allowed pair; all other bodies are far apart.
    auto isolated = body::createAuraBody3D();
    auto graph = body::createAuraSkeleton3D(isolated);
    auto all = graph.collectComponent(isolated,isolated.torso,graph.head);
    all.push_back(&isolated.head);
    for (std::size_t i=0;i<all.size();++i) all[i]->body.position = {float(i)*10,20,0};
    isolated.leftHand.body.position = isolated.torso.body.position;
    physics::resolveBodySelfCollisions(isolated,graph);
    check(physics::maximumSelfCollisionPenetration(isolated,graph)<1e-6f, "body pair sweep resolves isolated hand/torso overlap");
    isolated = body::createAuraBody3D();
    all = graph.collectComponent(isolated,isolated.torso,graph.head); all.push_back(&isolated.head);
    for (std::size_t i=0;i<all.size();++i) all[i]->body.position = {float(i)*10,20,0};
    isolated.leftShin.body.position = isolated.leftThigh.body.position;
    const auto before = isolated.leftThigh.body.position;
    physics::resolveBodySelfCollisions(isolated,graph);
    check((isolated.leftThigh.body.position-before).lengthSquared()==0 &&
          (isolated.leftShin.body.position-before).lengthSquared()==0, "neighbor overlap untouched by body sweep");
    // Read-only component feasibility; existing overlap elsewhere must not block
    // a candidate unless that candidate worsens a crossing pair.
    for (std::size_t i=0;i<all.size();++i) all[i]->body.position = {float(i)*10,20,0};
    isolated.leftHand.size = isolated.torso.size = {1,1,1};
    isolated.leftHand.body.position = {0,20,0};
    isolated.torso.body.position = {1,20,0};
    const std::vector<body::BodyPart3D*> hand{&isolated.leftHand};
    const auto safe = [&](const auto &component, const math::Vec3 &delta) {
        return physics::componentTranslationIsSelfCollisionSafe(isolated,graph,component,delta,0.001f);
    };
    check(!safe(hand,{0.01f,0,0}), "new crossing overlap rejected");
    check(safe(hand,{-0.01f,0,0}), "moving away is allowed");
    check(safe(hand,{0.0005f,0,0}), "sub-tolerance crossing overlap allowed");
    isolated.torso.body.position.x = 0.8f;
    check(safe(hand,{0,0,0}) && safe(hand,{-0.01f,0,0}), "unchanged or reduced existing overlap allowed");
    check(!safe(hand,{0.01f,0,0}), "worsening existing overlap rejected");
    check(safe(std::vector<body::BodyPart3D*>{&isolated.leftHand,&isolated.torso},{0.1f,0,0}),
          "internal overlapping pair does not block rigid component translation");
    isolated.torso.body.position = {1000,20,0};
    isolated.leftForearm.size = {1,1,1};
    isolated.leftForearm.body.position = {1,20,0};
    check(safe(hand,{0.01f,0,0}), "directly connected crossing pair excluded");
    check((isolated.leftHand.body.position-math::Vec3{0,20,0}).lengthSquared()==0,
          "candidate checks leave physical pose unchanged");
    return passed ? 0 : 1;
}
