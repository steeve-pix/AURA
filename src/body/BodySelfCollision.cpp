#include "aura/body/BodySelfCollision.hpp"
#include "aura/body/ThighSelfCollision.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aura::body {
namespace {
    template<class Body> auto partsOf(Body& b) {
        return std::array{&b.head,&b.neck,&b.torso,&b.pelvis,&b.leftUpperArm,&b.leftForearm,&b.leftHand,
            &b.rightUpperArm,&b.rightForearm,&b.rightHand,&b.leftThigh,&b.leftShin,&b.leftFoot,
            &b.rightThigh,&b.rightShin,&b.rightFoot};
    }
    physics::BodyContact contact(const BodyPart3D& a,const BodyPart3D& b) {
        return physics::detectBodyContact(a.body,a.size,a.collisionShape,b.body,b.size,b.collisionShape);
    }
    float limitError(const JointConnection3D& edge) {
        const auto& j=*edge.joint;const float angle=relativeJointAngle(*edge.a,*edge.b,j);
        float error=std::max({0.0f,j.minAngle-angle,angle-j.maxAngle});
        if(j.type==JointType::SwingTwist) {
            const auto sw=relativeJointSwingAngles(*edge.a,*edge.b,j);
            error=std::max({error,j.minSwingY-sw.y,sw.y-j.maxSwingY,j.minSwingZ-sw.z,sw.z-j.maxSwingZ});
        }
        else if(j.enforceHingeAxis)
            error=std::max(error,jointSwingRotationVector(relativeJointSwing(*edge.a,*edge.b,j)).length());
        return error;
    }
    float gap(const JointConnection3D& edge) {
        return (localToWorldPoint(*edge.a,edge.joint->localAnchorA)-localToWorldPoint(*edge.b,edge.joint->localAnchorB)).length();
    }
}
std::array<BodyPart3D*,16> collisionParts(AuraBody3D& b) {return partsOf(b);}
std::array<const BodyPart3D*,16> collisionParts(const AuraBody3D& b) {return partsOf(b);}
void configureBodyCollisionJoints(AuraSkeleton3D& s) {
    for(auto* j:{&s.waist,&s.neck,&s.head,&s.leftShoulder,&s.leftElbow,&s.leftWrist,
        &s.rightShoulder,&s.rightElbow,&s.rightWrist,&s.leftHip,&s.leftKnee,&s.leftAnkle,
        &s.rightHip,&s.rightKnee,&s.rightAnkle})
        j->enforceHingeAxis=j->type==JointType::Hinge;
}
bool projectBodyHingeAxis(AuraBody3D& b,const AuraSkeleton3D& s,
    BodyPart3D& parent,BodyPart3D& child,const Joint3D& joint) {
    if(!joint.enforceHingeAxis || joint.type!=JointType::Hinge)return false;
    const auto members=s.collectComponent(b,child,joint);
    if(!correctHingeAxisWithComponent(parent,child,joint,members))return false;
    const auto parts=collisionParts(b);float minimum=0;
    for(const auto* p:parts)minimum=std::min(minimum,physics::lowestPoint(p->body,p->size).y);
    if(minimum<0)translateSubtree(parts,{0,-minimum,0});
    return true;
}
std::vector<BodyCollisionPair> bodyCollisionPairs(AuraBody3D& b,const AuraSkeleton3D& s,int batch) {
    if(batch<1 || batch>3)throw std::invalid_argument("Body collision batch must be 1, 2 or 3");
    std::vector<BodyCollisionPair> result;const auto parts=collisionParts(b);
    const auto arm=[](unsigned i){return i>=4 && i<=9;};
    const auto leg=[](unsigned i){return i>=10;};
    const auto core=[](unsigned i){return i==2 || i==3;};
    for(unsigned a=0;a<16;++a)for(unsigned c=a+1;c<16;++c) {
        if(s.areDirectlyConnected(b,*parts[a],*parts[c]))continue;
        const unsigned group=(arm(a)&&core(c)) || (core(a)&&arm(c))?1:
            ((leg(a)&&leg(c)) || (arm(a)&&leg(c)) || (leg(a)&&arm(c)))?2:3;
        if(group<=unsigned(batch))result.push_back({a,c,group});
    }
    return result;
}
BodyCollisionMetrics bodyCollisionMetrics(const AuraBody3D& b,const std::vector<BodyCollisionPair>& pairs,float tolerance) {
    BodyCollisionMetrics result;const auto parts=collisionParts(b);
    for(const auto& pair:pairs) {
        const auto c=contact(*parts[pair.a],*parts[pair.b]);
        if(c.penetration>result.maximumPenetration) {
            result.maximumPenetration=c.penetration;result.pairA=pair.a;result.pairB=pair.b;
        }
        result.overlappingPairs+=c.penetration>tolerance;
        const double excess=std::max(0.0f,c.penetration-tolerance);
        result.squaredError+=excess*excess;
    }
    return result;
}
BodyCollisionStep solveBodySelfCollisions(AuraBody3D& b,const AuraSkeleton3D& s,
    const std::vector<BodyCollisionPair>& pairs,const physics::RoomBounds& room,
    int budget,float step,float tolerance) {
    BodyCollisionStep stats;
    for(const auto& edge:s.connections(b))projectBodyHingeAxis(b,s,*edge.a,*edge.b,*edge.joint);
    // Aim inside the unchanged acceptance tolerance so float roundoff does not
    // leave a contact just outside it. This strengthens, not relaxes, separation.
    const float projectionTarget=tolerance*.5f;
    constexpr double improvementEpsilon=1e-14; // squared metres
    for(int iteration=0;iteration<budget;++iteration) {
        const auto baseline=bodyCollisionMetrics(b,pairs,projectionTarget);
        if(!baseline.overlappingPairs)break;
        const auto input=b;auto best=input;double bestScore=baseline.squaredError;
        float bestEnvironmentTranslation=0;
        const auto inputParts=collisionParts(input);
        const auto edges=s.connections(b);
        std::array<float,15> oldGap{},oldLimit{};
        for(size_t i=0;i<edges.size();++i) {oldGap[i]=gap(edges[i]);oldLimit[i]=limitError(edges[i]);}
        const auto feasible=[&](AuraBody3D& candidate) {
            const auto parts=collisionParts(candidate);
            for(size_t i=0;i<parts.size();++i) {
                if(!floorMoveIsNoWorse(physics::lowestPoint(inputParts[i]->body,inputParts[i]->size).y,
                    physics::lowestPoint(parts[i]->body,parts[i]->size).y))return false;
                if(physics::roomPenetration(parts[i]->body,parts[i]->size,room)>
                    physics::roomPenetration(inputParts[i]->body,inputParts[i]->size,room)+1e-6f)return false;
            }
            const auto connections=s.connections(candidate);
            for(size_t i=0;i<connections.size();++i) {
                if(std::abs(gap(connections[i])-oldGap[i])>3e-6f)return false;
                if(limitError(connections[i])>std::max(1e-6f,oldLimit[i])+1e-6f)return false;
            }
            return true;
        };
        const auto recoverFloor=[&](AuraBody3D& candidate) {
            // Generalize the proven hip+knee floor recovery: a distal hinge
            // may restore clearance after a parent rotation. No velocity edits.
            for(unsigned bad=0;bad<16;++bad) {
                auto cp=collisionParts(candidate);
                const auto allowed=[&](unsigned i) {
                    const float y=physics::lowestPoint(inputParts[i]->body,inputParts[i]->size).y;
                    return y>=-1e-6f?-1e-6f:y-1e-6f;
                };
                if(physics::lowestPoint(cp[bad]->body,cp[bad]->size).y>=allowed(bad))continue;
                bool fixed=false;
                const auto ce=s.connections(candidate);
                // The table is root-outward. Try distal joints first: otherwise
                // a parent can "recover" the floor simply by undoing the very
                // collision correction we are trying to keep (neck before head).
                for(size_t reverse=ce.size();reverse>0 && !fixed;--reverse) {
                    const size_t e=reverse-1;
                    const auto& edge=ce[e];
                    // Non-root hinges can recover a distal contact. This includes
                    // neck/head as well as elbow/wrist and knee/ankle chains.
                    // Primary shoulder/hip coordinates remain in the outer search.
                    if(edge.joint->type!=JointType::Hinge || edge.joint==&s.waist ||
                       edge.joint==&s.leftShoulder || edge.joint==&s.rightShoulder)continue;
                    const auto component=s.collectComponent(candidate,*edge.b,*edge.joint);
                    if(std::find(component.begin(),component.end(),cp[bad])==component.end())continue;
                    const auto source=candidate;
                    std::vector<unsigned> indices;
                    for(const auto* p:component)indices.push_back(unsigned(std::find(cp.begin(),cp.end(),p)-cp.begin()));
                    const auto attempt=[&](float angle,AuraBody3D& result) {
                        result=source;const auto rp=collisionParts(result);const auto re=s.connections(result);
                        std::vector<BodyPart3D*> members;for(unsigned i:indices)members.push_back(rp[i]);
                        rotateSubtreeAroundWorldPoint(members,localToWorldPoint(*re[e].b,edge.joint->localAnchorB),
                            math::Quaternion::fromAxisAngle(re[e].a->body.orientation.rotate(edge.joint->hingeAxis).normalized(),angle));
                        if(limitError(re[e])>std::max(1e-6f,oldLimit[e])+1e-6f)return false;
                        for(unsigned i:indices)
                            if(physics::lowestPoint(rp[i]->body,rp[i]->size).y<allowed(i))return false;
                        return true;
                    };
                    float smallest=10;AuraBody3D recovered;
                    for(float sign:{-1.0f,1.0f}) {
                        float low=0;
                        for(float high:{.001f,.002f,.004f,.008f,.016f,.032f,.064f,.128f,.256f}) {
                            AuraBody3D trial;
                            if(attempt(sign*high,trial)) {
                                float upper=high;
                                for(int n=0;n<16;++n) {
                                    const float mid=(low+upper)*.5f;
                                    if(attempt(sign*mid,trial))upper=mid;else low=mid;
                                }
                                attempt(sign*upper,trial);
                                if(upper<smallest) {smallest=upper;recovered=trial;}
                                break;
                            }
                            low=high;
                        }
                    }
                    if(smallest<10) {candidate=recovered;fixed=true;}
                }
                if(!fixed)return false;
            }
            return true;
        };
        const auto consider=[&](AuraBody3D& candidate) {
            ++stats.candidateEvaluations;
            double score=bodyCollisionMetrics(candidate,pairs,projectionTarget).squaredError;
            if(score>=bestScore-improvementEpsilon)return;
            float environmentTranslation=0;
            if(!feasible(candidate)) {
                const auto unmodified=candidate;
                bool recovered=recoverFloor(candidate) && feasible(candidate);
                if(recovered) {
                    score=bodyCollisionMetrics(candidate,pairs,projectionTarget).squaredError;
                    recovered=score<bestScore-improvementEpsilon;
                }
                if(!recovered) {
                    // Evaluate collision and environment projection together.
                    // A common external containment translation preserves every
                    // internal joint/collision relationship; no velocity edits.
                    candidate=unmodified;const auto parts=collisionParts(candidate);
                    std::array<physics::RoomBody,16> roomBodies;
                    for(size_t i=0;i<parts.size();++i)roomBodies[i]={&parts[i]->body,parts[i]->size};
                    const auto before=parts.front()->body.position;
                    if(!physics::projectRoomGeometry(roomBodies,room) || !feasible(candidate))return;
                    environmentTranslation=(parts.front()->body.position-before).length();
                    score=bodyCollisionMetrics(candidate,pairs,projectionTarget).squaredError;
                    if(score>=bestScore-improvementEpsilon)return;
                }
            }
            best=candidate;bestScore=score;bestEnvironmentTranslation=environmentTranslation;
        };
        // Retain the validated coupled two-hip search as one candidate provider.
        if(baseline.pairA==10 && baseline.pairB==13) {
            auto candidate=input;correctThighOverlapCoupled(candidate,s,step,projectionTarget);consider(candidate);
        }
        // A cut on the path between contacts has one endpoint on each side.
        // Search both graph sides so environmental support does not pin a child
        // permanently. Each accepted candidate rotates at most one small coordinate.
        for(size_t e=0;e<edges.size();++e) {
            const auto& edge=edges[e];
            const auto members=s.collectComponent(b,*edge.b,*edge.joint);
            const auto liveParts=collisionParts(b);
            std::vector<unsigned> childIndices;
            for(const auto* p:members)childIndices.push_back(unsigned(std::find(liveParts.begin(),liveParts.end(),p)-liveParts.begin()));
            const auto contains=[&](unsigned i){return std::find(childIndices.begin(),childIndices.end(),i)!=childIndices.end();};
            if(contains(baseline.pairA)==contains(baseline.pairB))continue;
            const bool multi=edge.joint->type==JointType::SwingTwist ||
                (!edge.joint->enforceHingeAxis && (edge.joint==&s.leftShoulder || edge.joint==&s.rightShoulder));
            if(multi) {
                const auto active=contact(*liveParts[baseline.pairA],*liveParts[baseline.pairB]);
                const bool childIsA=contains(baseline.pairA);
                const auto surface=childIsA?active.pointA:active.pointB;
                const auto pivot=localToWorldPoint(*edge.b,edge.joint->localAnchorB);
                const auto force=childIsA?-active.normal:active.normal;
                const auto torque=(surface-pivot).cross(force);
                if(torque.lengthSquared()>1e-12f)for(float magnitude:{step,3*step,6*step}) {
                    // Torque-directed request, then retain the old twist explicitly.
                    auto targetPart=*edge.b;
                    targetPart.body.orientation=(math::Quaternion::fromAxisAngle(torque.normalized(),magnitude)*
                        targetPart.body.orientation).normalized();
                    const auto decomposition=relativeJointSwingTwist(*edge.a,targetPart,*edge.joint);
                    const auto original=relativeJointSwingTwist(*edge.a,*edge.b,*edge.joint);
                    const auto target=(edge.a->body.orientation.normalized()*decomposition.swing*original.twist).normalized();
                    const auto rotation=(target*edge.b->body.orientation.normalized().conjugate()).normalized();
                    for(bool moveChild:{true,false}) {
                        auto candidate=input;const auto candidateParts=collisionParts(candidate);
                        const auto ce=s.connections(candidate);std::vector<BodyPart3D*> moved;
                        for(unsigned i=0;i<16;++i)if(contains(i)==moveChild)moved.push_back(candidateParts[i]);
                        const auto p=moveChild?localToWorldPoint(*ce[e].b,edge.joint->localAnchorB):
                            localToWorldPoint(*ce[e].a,edge.joint->localAnchorA);
                        rotateSubtreeAroundWorldPoint(moved,p,moveChild?rotation:rotation.conjugate());
                        consider(candidate);
                    }
                }
            }
            for(int coordinate=0;coordinate<(multi?3:1);++coordinate)for(float sign:{-1.0f,1.0f})
            for(float magnitude:{step,step*.5f,step*.25f,step*.125f,step*.0625f}) {
                const auto& j=*edge.joint;
                math::Quaternion rotation;
                if(coordinate==0) {
                    rotation=math::Quaternion::fromAxisAngle(edge.a->body.orientation.rotate(j.hingeAxis).normalized(),sign*magnitude);
                } else {
                    auto sw=relativeJointSwingAngles(*edge.a,*edge.b,j);
                    (coordinate==1?sw.y:sw.z)+=sign*magnitude;
                    if(j.type==JointType::SwingTwist && (sw.y<j.minSwingY || sw.y>j.maxSwingY || sw.z<j.minSwingZ || sw.z>j.maxSwingZ))continue;
                    const math::Vec3 v{0,sw.y,sw.z};const float length=v.length();
                    const auto swing=length>0?math::Quaternion::fromAxisAngle(v,length):math::Quaternion{};
                    const auto target=edge.a->body.orientation.normalized()*swing*
                        math::Quaternion::fromAxisAngle(j.hingeAxis,relativeJointAngle(*edge.a,*edge.b,j));
                    rotation=(target*edge.b->body.orientation.normalized().conjugate()).normalized();
                }
                for(bool moveChild:{true,false}) {
                    auto candidate=input;const auto candidateParts=collisionParts(candidate);
                    const auto candidateEdges=s.connections(candidate);
                    const auto& c=candidateEdges[e];
                    std::vector<BodyPart3D*> moved;
                    for(unsigned i=0;i<16;++i)if(contains(i)==moveChild)moved.push_back(candidateParts[i]);
                    const auto pivot=moveChild?localToWorldPoint(*c.b,j.localAnchorB):localToWorldPoint(*c.a,j.localAnchorA);
                    rotateSubtreeAroundWorldPoint(moved,pivot,moveChild?rotation:rotation.conjugate());
                    consider(candidate);
                }
            }
        }
        if(bestScore>=baseline.squaredError-improvementEpsilon) {++stats.blockedProjections;break;}
        b=best;++stats.acceptedProjections;
        stats.environmentProjections+=bestEnvironmentTranslation>0;
        stats.maximumEnvironmentTranslation=std::max(stats.maximumEnvironmentTranslation,bestEnvironmentTranslation);
    }
    const auto parts=collisionParts(b);
    for(const auto& pair:pairs) {
        auto c=contact(*parts[pair.a],*parts[pair.b]);
        // Positional acceptance slop is not a velocity dead zone. Even a
        // shallow, successfully projected contact must stop closing motion.
        if(!c.overlapping)continue;
        const auto result=physics::resolveBodyContactVelocity(parts[pair.a]->body,parts[pair.b]->body,c);
        stats.impulses+=result.applied;
        stats.largestImpulse=std::max(stats.largestImpulse,result.magnitude);
        stats.maximumPositiveEnergy=std::max(stats.maximumPositiveEnergy,result.energyChange);
        stats.maximumMomentumError=std::max(stats.maximumMomentumError,double(result.momentumChange.length()));
    }
    return stats;
}
}
