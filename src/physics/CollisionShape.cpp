#include "aura/physics/CollisionShape.hpp"
#include "aura/physics/SelfCollision.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/Impulse.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/MechanicalState.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aura::physics {
namespace {
    float component(const math::Vec3& v,int i) {return i==0?v.x:i==1?v.y:v.z;}
    math::Vec3 unit(int i) {return i==0?math::Vec3{1,0,0}:i==1?math::Vec3{0,1,0}:math::Vec3{0,0,1};}
    math::Vec3 clampBox(const math::Vec3& p,const math::Vec3& h) {
        return {std::clamp(p.x,-h.x,h.x),std::clamp(p.y,-h.y,h.y),std::clamp(p.z,-h.z,h.z)};
    }
    CollisionCapsule capsule(const RigidBody3D& b,const math::Vec3& size,CollisionShape s) {
        const float r=0.5f*std::min({size.x,size.y,size.z});
        const float h=s.type==CollisionShapeType::Sphere?0:std::max(0.0f,0.5f*size.y-r);
        const auto offset=b.orientation.normalized().rotate({0,h,0});
        return {b.position-offset,b.position+offset,r};
    }
    float bound(const math::Vec3& size,CollisionShape s) {
        if(s.type==CollisionShapeType::Box)return size.length()*0.5f;
        if(s.type==CollisionShapeType::Sphere)return 0.5f*std::min({size.x,size.y,size.z});
        const float r=0.5f*std::min({size.x,size.y,size.z});return std::max(r,size.y*0.5f);
    }
    BodyContact capsules(const CollisionCapsule& a,const CollisionCapsule& b) {
        const auto closest=closestPointsBetweenSegments(a.pointA,a.pointB,b.pointA,b.pointB);
        auto connector=closest.pointB-closest.pointA;
        const float distance=connector.length(),depth=a.radius+b.radius-distance;
        if(depth<=0)return {};
        if(distance<1e-7f) {
            const auto u=a.pointB-a.pointA,v=b.pointB-b.pointA;
            connector=u.cross(v);
            if(connector.lengthSquared()<1e-12f)connector=u.cross({1,0,0});
            if(connector.lengthSquared()<1e-12f)connector=u.cross({0,1,0});
            if(connector.lengthSquared()<1e-12f)connector={1,0,0};
            if(connector.dot((b.pointA+b.pointB)-(a.pointA+a.pointB))<0)connector=-connector;
        }
        const auto n=connector.normalized();
        return {closest.pointA+n*a.radius,closest.pointB-n*b.radius,n,depth,true};
    }
    // Exact closest segment/AABB distance: squared distance is a quadratic on
    // each interval between segment crossings of the six box planes.
    BodyContact capsuleBox(const CollisionCapsule& cap,const RigidBody3D& box,const math::Vec3& size) {
        const auto q=box.orientation.normalized(),inverse=q.conjugate();
        const auto a=inverse.rotate(cap.pointA-box.position),b=inverse.rotate(cap.pointB-box.position);
        const auto d=b-a,h=size*0.5f;
        std::array<double,8> breaks{};size_t count=2;breaks[0]=0;breaks[1]=1;
        for(int k=0;k<3;++k)if(std::abs(component(d,k))>1e-12f)
            for(float sign:{-1.0f,1.0f}) {
                const double t=(sign*component(h,k)-component(a,k))/component(d,k);
                if(t>0 && t<1)breaks[count++]=t;
            }
        std::sort(breaks.begin(),breaks.begin()+count);
        float best=std::numeric_limits<float>::infinity();math::Vec3 p{},c{};
        const auto test=[&](double t) {
            const auto x=a+d*float(t),y=clampBox(x,h);const float squared=(x-y).lengthSquared();
            if(squared<best) {best=squared;p=x;c=y;}
        };
        for(size_t i=0;i<count;++i)test(breaks[i]);
        for(size_t i=1;i<count;++i) {
            const double lo=breaks[i-1],hi=breaks[i],mid=(lo+hi)*0.5;
            double quadratic=0,linear=0;
            for(int k=0;k<3;++k) {
                const double value=component(a,k)+mid*component(d,k),half=component(h,k);
                if(value>=-half && value<=half)continue;
                const double offset=component(a,k)-(value<0?-half:half),slope=component(d,k);
                quadratic+=slope*slope;linear+=slope*offset;
            }
            if(quadratic>0)test(std::clamp(-linear/quadratic,lo,hi));
        }
        const float distance=std::sqrt(best);
        math::Vec3 normal{};float depth=cap.radius-distance;
        if(distance>1e-7f)normal=(c-p)*(1.0f/distance);
        else {
            // Segment intersects box: choose the shortest whole-capsule escape
            // through a box face, including both segment endpoints.
            depth=std::numeric_limits<float>::infinity();
            for(int k=0;k<3;++k)for(float sign:{-1.0f,1.0f}) {
                const float low=std::min(sign*component(a,k),sign*component(b,k));
                const float move=component(h,k)-low+cap.radius;
                if(move<depth) {
                    depth=move;const auto out=unit(k)*sign;normal=-out;
                    p=sign*component(a,k)<sign*component(b,k)?a:b;
                    c=clampBox(p,h);c+=unit(k)*(sign*component(h,k)-component(c,k));
                }
            }
        }
        if(depth<=0)return {};
        const auto n=q.rotate(normal);
        return {box.position+q.rotate(p)+n*cap.radius,box.position+q.rotate(c),n,depth,true};
    }
    BodyContact boxes(const RigidBody3D& a,const math::Vec3& sa,const RigidBody3D& b,const math::Vec3& sb) {
        std::array<math::Vec3,3> axesA,axesB;
        for(int k=0;k<3;++k) {axesA[k]=a.orientation.normalized().rotate(unit(k));axesB[k]=b.orientation.normalized().rotate(unit(k));}
        const auto delta=b.position-a.position;float depth=std::numeric_limits<float>::infinity();math::Vec3 normal{};
        const auto test=[&](math::Vec3 axis) {
            if(axis.lengthSquared()<1e-12f)return true;
            axis=axis.normalized();float radiusA=0,radiusB=0;
            for(int k=0;k<3;++k) {radiusA+=0.5f*component(sa,k)*std::abs(axis.dot(axesA[k]));radiusB+=0.5f*component(sb,k)*std::abs(axis.dot(axesB[k]));}
            const float overlap=radiusA+radiusB-std::abs(delta.dot(axis));
            if(overlap<=0)return false;
            if(overlap<depth) {depth=overlap;normal=delta.dot(axis)>=0?axis:-axis;}
            return true;
        };
        for(const auto& x:axesA)if(!test(x))return {};
        for(const auto& x:axesB)if(!test(x))return {};
        for(const auto& x:axesA)for(const auto& y:axesB)if(!test(x.cross(y)))return {};
        // Single contact representative, not a clipped multipoint manifold.
        const auto closest=[](const RigidBody3D& body,const math::Vec3& size,const math::Vec3& point) {
            const auto q=body.orientation.normalized();
            return body.position+q.rotate(clampBox(q.conjugate().rotate(point-body.position),size*0.5f));
        };
        return {closest(a,sa,b.position),closest(b,sb,a.position),normal,depth,true};
    }
}
BodyContact detectBodyContact(const RigidBody3D& a,const math::Vec3& sa,CollisionShape shapeA,
                              const RigidBody3D& b,const math::Vec3& sb,CollisionShape shapeB) {
    const float radius=bound(sa,shapeA)+bound(sb,shapeB);
    if((b.position-a.position).lengthSquared()>=radius*radius)return {};
    if(shapeA.type==CollisionShapeType::Box && shapeB.type==CollisionShapeType::Box)return boxes(a,sa,b,sb);
    if(shapeB.type==CollisionShapeType::Box)return capsuleBox(capsule(a,sa,shapeA),b,sb);
    if(shapeA.type==CollisionShapeType::Box) {
        const auto c=capsuleBox(capsule(b,sb,shapeB),a,sa);
        return {c.pointB,c.pointA,-c.normal,c.penetration,c.overlapping};
    }
    return capsules(capsule(a,sa,shapeA),capsule(b,sb,shapeB));
}
ContactImpulse resolveBodyContactVelocity(RigidBody3D& a,RigidBody3D& b,const BodyContact& contact) {
    ContactImpulse result;
    if(!contact.overlapping)return result;
    const auto point=(contact.pointA+contact.pointB)*0.5f,normal=contact.normal.normalized();
    const auto speed=[&] {return (velocityAtWorldPoint(b,point)-velocityAtWorldPoint(a,point)).dot(normal);};
    result.normalSpeedBefore=result.normalSpeedAfter=speed();
    if(result.normalSpeedBefore>=0)return result;
    const auto ra=point-a.position,rb=point-b.position;
    const auto ca=ra.cross(normal),cb=rb.cross(normal);
    const float inverseMass=1/a.mass+1/b.mass+ca.dot(applyInverseInertiaWorld(a,ca))+cb.dot(applyInverseInertiaWorld(b,cb));
    if(!(inverseMass>0) || !std::isfinite(inverseMass))return result;
    const auto energy=[&] {const auto x=mechanicalState(a),y=mechanicalState(b);
        return double(x.linearKinetic)+x.rotationalKinetic+double(y.linearKinetic)+y.rotationalKinetic;};
    const auto oldA=a.velocity,oldB=b.velocity;const double before=energy();
    result.magnitude=-result.normalSpeedBefore/inverseMass;const auto impulse=normal*result.magnitude;
    applyImpulseAtPoint(a,-impulse,point);applyImpulseAtPoint(b,impulse,point);
    result.normalSpeedAfter=speed();result.energyChange=energy()-before;
    result.momentumChange=(a.velocity-oldA)*a.mass+(b.velocity-oldB)*b.mass;result.applied=true;
    return result;
}
}
