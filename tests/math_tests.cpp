#include <cmath>
#include <iostream>
#include <numbers>

#include "aura/math/Quaternion.hpp"
#include "aura/math/Rotation.hpp"
#include "aura/math/Vec3.hpp"

namespace {
    constexpr float tolerance = 1e-5f;

    void checkFloat(const char *name, float actual, float expected, int &failures) {
        if (!(std::abs(actual - expected) <= tolerance)) {
            std::cerr << "FAIL " << name << ": expected " << expected << ", got " << actual << '\n';
            ++failures;
        }
    }

    void checkVec3(const char *name, const aura::math::Vec3 &actual,
                   const aura::math::Vec3 &expected, int &failures) {
        if (!(std::abs(actual.x - expected.x) <= tolerance &&
              std::abs(actual.y - expected.y) <= tolerance &&
              std::abs(actual.z - expected.z) <= tolerance)) {
            std::cerr << "FAIL " << name << ": expected (" << expected.x << ", " << expected.y
                    << ", " << expected.z << "), got (" << actual.x << ", " << actual.y
                    << ", " << actual.z << ")\n";
            ++failures;
        }
    }
}

int main() {
    using aura::math::Vec3;
    int failures = 0;

    checkVec3("default construction", Vec3{}, {0.0f, 0.0f, 0.0f}, failures);
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};
    checkVec3("value construction", a, {1.0f, 2.0f, 3.0f}, failures);
    checkVec3("addition", a + b, {5.0f, 7.0f, 9.0f}, failures);
    checkVec3("subtraction", b - a, {3.0f, 3.0f, 3.0f}, failures);
    checkVec3("scalar multiplication", a * 2.0f, {2.0f, 4.0f, 6.0f}, failures);

    Vec3 changed = a;
    checkVec3("addition assignment result", changed += b, {5.0f, 7.0f, 9.0f}, failures);
    checkVec3("addition assignment mutation", changed, {5.0f, 7.0f, 9.0f}, failures);
    checkVec3("subtraction assignment result", changed -= b, a, failures);
    checkVec3("subtraction assignment mutation", changed, a, failures);

    const Vec3 triangle{3.0f, 4.0f, 0.0f};
    checkFloat("length squared", triangle.lengthSquared(), 25.0f, failures);
    checkFloat("length", triangle.length(), 5.0f, failures);
    checkVec3("normalization", triangle.normalized(), {0.6f, 0.8f, 0.0f}, failures);
    checkVec3("zero normalization", Vec3{}.normalized(), {}, failures);
    checkFloat("dot product", a.dot(b), 32.0f, failures);
    checkVec3("cross product", Vec3{1.0f, 0.0f, 0.0f}.cross({0.0f, 1.0f, 0.0f}),
              {0.0f, 0.0f, 1.0f}, failures);

    const float quarterTurn = std::numbers::pi_v<float> / 2.0f;
    checkVec3("rotate X by 90 degrees", aura::math::rotateAroundX(a, quarterTurn),
              {1.0f, -3.0f, 2.0f}, failures);
    checkVec3("rotate Y by 90 degrees", aura::math::rotateAroundY(a, quarterTurn),
              {3.0f, 2.0f, -1.0f}, failures);
    checkVec3("rotate Z by 90 degrees", aura::math::rotateAroundZ(a, quarterTurn),
              {-2.0f, 1.0f, 3.0f}, failures);
    checkVec3("rotate X by zero", aura::math::rotateAroundX(a, 0.0f), a, failures);
    checkVec3("rotate Y by zero", aura::math::rotateAroundY(a, 0.0f), a, failures);
    checkVec3("rotate Z by zero", aura::math::rotateAroundZ(a, 0.0f), a, failures);

    const auto q = aura::math::Quaternion::fromAxisAngle(
        Vec3{0.0f, 1.0f, 0.0f}, quarterTurn);
    const float halfTurnComponent = std::sqrt(0.5f);
    checkFloat("quaternion Y 90 degrees w", q.w, halfTurnComponent, failures);
    checkFloat("quaternion Y 90 degrees x", q.x, 0.0f, failures);
    checkFloat("quaternion Y 90 degrees y", q.y, halfTurnComponent, failures);
    checkFloat("quaternion Y 90 degrees z", q.z, 0.0f, failures);
    std::cout << "Quaternion Y 90 degrees: " << q.w << ", " << q.x
              << ", " << q.y << ", " << q.z << '\n';

    const Vec3 v{1.0f, 0.0f, 0.0f};
    const Vec3 rotated = q.rotate(v);
    checkVec3("quaternion rotates X toward negative Z", rotated,
              {0.0f, 0.0f, -1.0f}, failures);
    std::cout << "Rotated vector: " << rotated.x << ", " << rotated.y
              << ", " << rotated.z << '\n';

    const aura::math::Quaternion twiceIdentity{2.0f, 0.0f, 0.0f, 0.0f};
    const auto normalized = twiceIdentity.normalized();
    checkFloat("normalized quaternion length", normalized.length(), 1.0f, failures);
    checkFloat("normalized quaternion w", normalized.w, 1.0f, failures);
    std::cout << "Normalized quaternion length: " << normalized.length() << '\n';

    if (failures == 0) {
        std::cout << "All math checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
