#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include "aura/physics/SelfCollision.hpp"

int main(int argc,char **argv) {
    using namespace aura;
    bool passed=true;
    const auto check=[&](bool ok,const char *message) {
        if (!ok) { std::cerr << "FAIL " << message << '\n'; passed=false; }
    };
    const auto near=[](const math::Vec3 &a,const math::Vec3 &b) { return (a-b).length()<2e-6f; };
    const auto closest=[&](const math::Vec3 &a0,const math::Vec3 &a1,
                           const math::Vec3 &b0,const math::Vec3 &b1,float distance) {
        const auto result=physics::closestPointsBetweenSegments(a0,a1,b0,b1);
        check(std::abs((result.pointB-result.pointA).length()-distance)<2e-6f,"segment closest distance matches geometry");
        check(result.fractionA>=0 && result.fractionA<=1 && result.fractionB>=0 && result.fractionB<=1,
              "closest fractions remain on finite segments");
        check(near(result.pointA,a0+(a1-a0)*result.fractionA) &&
              near(result.pointB,b0+(b1-b0)*result.fractionB),"closest points belong to segments");
        const auto swapped=physics::closestPointsBetweenSegments(b0,b1,a0,a1);
        check(std::abs((swapped.pointB-swapped.pointA).length()-distance)<2e-6f,"distance symmetric under swapping");
        const auto reversed=physics::closestPointsBetweenSegments(a1,a0,b1,b0);
        check(std::abs((reversed.pointB-reversed.pointA).length()-distance)<2e-6f,"distance unchanged by endpoint reversal");
    };
    closest({-1,0,0},{1,0,0},{0,-1,0},{0,1,0},0); // Crossing interiors.
    closest({-1,0,0},{1,0,0},{0,-1,2},{0,1,2},2); // Skew lines.
    closest({0,0,0},{0,2,0},{1,0,0},{1,2,0},1); // Parallel.
    closest({0,0,0},{0,2,0},{1,2,0},{1,0,0},1); // Opposite parallel directions.
    closest({0,0,0},{2,0,0},{1,0,0},{3,0,0},0); // Collinear overlap.
    closest({0,0,0},{1,0,0},{2,0,0},{3,0,0},1); // Endpoint pair.
    closest({0,0,0},{1,0,0},{2,-1,0},{2,1,0},1); // Endpoint/interior.
    closest({0,0,0},{0,0,0},{-1,2,0},{1,2,0},2); // Point/segment.
    closest({0,0,0},{0,0,0},{0,3,0},{0,3,0},3); // Point/point.
    closest({0,0,0},{1,0,0},{0,0.00001f,0},{1,-0.00001f,0},0); // Almost parallel crossing.

    body::BodyPart3D limb;
    limb.size={0.5f,1.8f,0.5f}; limb.body.position={1,2,3};
    limb.body.orientation=math::Quaternion::fromAxisAngle({0,0,1},std::numbers::pi_v<float>/2);
    limb.body.orientation={limb.body.orientation.w*2,limb.body.orientation.x*2,
                           limb.body.orientation.y*2,limb.body.orientation.z*2};
    const auto capsule=physics::bodyPartCollisionCapsule(limb);
    check(near(capsule.pointA,{1.65f,2,3}) && near(capsule.pointB,{0.35f,2,3}) && capsule.radius==0.25f,
          "local Y endpoints rotate into world space using normalized orientation");
    check(limb.body.orientation.length()>1.9f,"detection leaves input orientation unchanged");
    limb.size={1,0.2f,1};
    const auto shortPart=physics::bodyPartCollisionCapsule(limb);
    check(near(shortPart.pointA,limb.body.position) && near(shortPart.pointB,limb.body.position),"short capsule collapses to sphere");
    physics::CollisionCapsule a{{0,0,0},{0,2,0},0.5f}, b{{1,0,0},{1,2,0},0.5f};
    check(physics::capsulePenetration(a,b)==0,"touching capsules do not overlap");
    b.pointA.x=b.pointB.x=2;
    check(physics::capsulePenetration(a,b)==0,"separated capsules do not overlap");
    b.pointA.x=b.pointB.x=0.75f;
    check(std::abs(physics::capsulePenetration(a,b)-0.25f)<1e-6f,"parallel capsule penetration correct");
    auto neutral=body::createAuraBody3D();
    const auto nl=physics::bodyPartCollisionCapsule(neutral.leftThigh), nr=physics::bodyPartCollisionCapsule(neutral.rightThigh);
    check(physics::capsulePenetration(nl,nr)==0,"neutral thigh capsules do not overlap");
    const auto np=physics::closestPointsBetweenSegments(nl.pointA,nl.pointB,nr.pointA,nr.pointB);
    check(std::abs((np.pointB-np.pointA).length()-0.68f)<1e-6f,"neutral thigh segment separation matches hip spacing");

    if (argc!=2) { std::cerr << "Missing attached-hip fixture path\n"; return 2; }
    std::ifstream file(argv[1]); std::string line;
    body::BodyPart3D left,right;
    bool haveLeft=false,haveRight=false;
    while (std::getline(file,line)) {
        if (!line.starts_with("hipCapsuleBody,copiedBothHipsAttached,")) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line); std::string tag,stage,side; row>>tag>>stage>>side;
        auto &part=side=="left"?left:right;
        row>>part.size.x>>part.size.y>>part.size.z>>part.body.position.x>>part.body.position.y>>part.body.position.z
            >>part.body.orientation.w>>part.body.orientation.x>>part.body.orientation.y>>part.body.orientation.z;
        check(bool(row),"attached pose fixture parses");
        if (side=="left") haveLeft=true; else if (side=="right") haveRight=true;
    }
    check(haveLeft&&haveRight,"fixture contains both attached thighs");
    if (haveLeft&&haveRight) {
        const auto l=physics::bodyPartCollisionCapsule(left), r=physics::bodyPartCollisionCapsule(right);
        const float overlap=physics::capsulePenetration(l,r);
        const auto ls=physics::collisionSphere(left),rs=physics::collisionSphere(right);
        const float sphereOverlap=std::max(0.0f,ls.radius+rs.radius-(rs.center-ls.center).length());
        const auto points=physics::closestPointsBetweenSegments(l.pointA,l.pointB,r.pointA,r.pointB);
        const auto separation=points.pointA-points.pointB;
        check(std::abs(separation.length()-0.05027332902f)<2e-6f &&
              std::abs(overlap-0.5297266245f)<2e-6f,"recorded attached-thigh closest distance and penetration reproduced");
        check(points.fractionA>0 && points.fractionA<1 && points.fractionB>0 && points.fractionB<1 &&
              std::abs(separation.dot(l.pointB-l.pointA))<2e-6f &&
              std::abs(separation.dot(r.pointB-r.pointA))<2e-6f,
              "recorded interior closest points are perpendicular to both axes");
        check(overlap>0.014f,"copied attached-hip capsules genuinely overlap");
        check(overlap+2e-6f>=sphereOverlap,"limb capsules contain their center sphere proxies");
        std::cout << "Attached thigh capsule penetration=" << overlap << '\n';
    }
    return passed?0:1;
}
