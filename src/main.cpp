#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>

#include "aura/body/AuraBody.hpp"
#include "aura/body/BalanceController.hpp"
#include "aura/math/Rotation.hpp"
#include "aura/physics/Body2D.hpp"
#include "aura/physics/Joint2D.hpp"
#include "aura/physics/JointConstraint.hpp"
#include "aura/physics/Physics.hpp"
#include "aura/physics/World2D.hpp"

static float g_aspectRatioModifier = 1.0f;

static float worldToScreenY(float worldY) {
    constexpr float scale = 0.1f;
    constexpr float verticalOffset = -0.6f;

    return worldY * scale + verticalOffset;
}

static void drawBody(const aura::physics::Body2D &body) {
    constexpr float scale = 0.1f;

    const float x =
            body.position.x * scale * g_aspectRatioModifier;
    const float y =
            worldToScreenY(body.position.y);

    const float halfWidth =
            body.size.x * scale * 0.5f;

    const float halfHeight =
            body.size.y * scale * 0.5f;

    const aura::math::Vec2 localCorners[] = {
        {-halfWidth, -halfHeight},
        {halfWidth, -halfHeight},
        {halfWidth, halfHeight},
        {-halfWidth, halfHeight}
    };

    glColor3f(0.8f, 0.8f, 0.8f);

    glBegin(GL_QUADS);

    for (const auto &corner: localCorners) {
        const auto rotated = aura::math::rotate(corner, body.angle);
        const float vertexX = x + rotated.x * g_aspectRatioModifier;
        const float vertexY = y + rotated.y;

        glVertex2f(vertexX, vertexY);
    }

    glEnd();
    glColor3f(1.0f, 1.0f, 1.0f);
}

void drawFloor(const aura::physics::World2D &world) {
    const float top = worldToScreenY(world.floorHeight);

    glBegin(GL_QUADS);

    float leftBound = -1.0f / g_aspectRatioModifier;
    float rightBound = 1.0f / g_aspectRatioModifier;

    glVertex2f(leftBound, -1.0f);
    glVertex2f(rightBound, -1.0f);
    glVertex2f(rightBound, top);
    glVertex2f(leftBound, top);

    glEnd();
}

void drawPoint(const aura::math::Vec2 &worldPoint, float size = 0.02f) {
    const float x =
            worldPoint.x * 0.1f * g_aspectRatioModifier;

    const float y =
            worldToScreenY(worldPoint.y);

    glBegin(GL_QUADS);

    glVertex2f(x - size, y - size);
    glVertex2f(x + size, y - size);
    glVertex2f(x + size, y + size);
    glVertex2f(x - size, y + size);

    glEnd();
}

void drawJoint(const aura::physics::Body2D &bodyA, const aura::physics::Body2D &bodyB,
               const aura::physics::Joint2D &joint) {
    const auto anchorA =
            aura::physics::worldAnchorA(bodyA, joint);

    const auto anchorB =
            aura::physics::worldAnchorB(bodyB, joint);

    drawPoint(anchorA);
    drawPoint(anchorB);
}

void drawCircle(const aura::physics::Body2D &body, int segments = 32) {
    const float scale = 0.1f;
    const float x = body.position.x * scale * g_aspectRatioModifier;
    const float y = worldToScreenY(body.position.y);
    const float radius = body.size.x * scale * 0.5f;

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);

    for (int i = 0; i <= segments; ++i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);

        // Local point inside the circle
        aura::math::Vec2 localPoint{std::cos(angle) * radius, std::sin(angle) * radius};

        // Apply body rotation
        localPoint = aura::math::rotate(localPoint, body.angle);

        // Apply aspect ratio correction ONLY to the X dimension in screen space
        glVertex2f(x + localPoint.x * g_aspectRatioModifier, y + localPoint.y);
    }

    glEnd();
}

void drawCapsule(const aura::physics::Body2D &body, int segments = 16) {
    const float scale = 0.1f;
    const float centerX = body.position.x * scale * g_aspectRatioModifier;
    const float centerY = worldToScreenY(body.position.y);

    const float halfWidth = body.size.x * scale * 0.5f;
    const float halfHeight = body.size.y * scale * 0.5f;
    const float radius = halfWidth;
    const float straightHalfHeight = std::max(0.0f, halfHeight - radius);

    const aura::math::Vec2 topCenter{0.0f, straightHalfHeight};
    const aura::math::Vec2 bottomCenter{0.0f, -straightHalfHeight};

    glBegin(GL_POLYGON);

    // Top cap arc (angles from 0 to PI)
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = t * std::numbers::pi_v<float>;

        aura::math::Vec2 localPoint{std::cos(angle) * radius, topCenter.y + std::sin(angle) * radius};
        localPoint = aura::math::rotate(localPoint, body.angle);

        glVertex2f(centerX + localPoint.x * g_aspectRatioModifier, centerY + localPoint.y);
    }

    // Bottom cap arc (angles from PI to 2*PI)
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = std::numbers::pi_v<float> + t * std::numbers::pi_v<float>;

        aura::math::Vec2 localPoint{std::cos(angle) * radius, bottomCenter.y + std::sin(angle) * radius};
        localPoint = aura::math::rotate(localPoint, body.angle);

        glVertex2f(centerX + localPoint.x * g_aspectRatioModifier, centerY + localPoint.y);
    }

    glEnd();
}

int main() {
    if (!glfwInit())
        return 1;

    GLFWwindow *window = glfwCreateWindow(800, 600, "AURA v0.3", nullptr, nullptr);

    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);

    aura::physics::World2D world{};

    aura::body::AuraBody auraBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "torso",
                .body = {
                    .position = {-5.0f, 5.0f},
                    .size = {1.4f, 2.4f},
                    .mass = 4.0f
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .shape = aura::body::BodyPartShape::Circle,
                .name = "head",
                .body = {
                    .position = {-5.0f, 6.7f},
                    .size = {0.8f, 0.8f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftThigh,
                .name = "left_thigh",
                .body = {
                    .position = {-5.3f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftShin,
                .name = "left_shin",
                .body = {
                    .position = {-5.3f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::RightThigh,
                .name = "right_thigh",
                .body = {
                    .position = {-4.7f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::RightShin,
                .name = "right_shin",
                .body = {
                    .position = {-4.7f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .name = "left_foot",
                .body = {
                    .position = {-5.15f, 0.175f},
                    .size = {1.0f, 0.35f},
                    .mass = 0.8f
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .name = "right_foot",
                .body = {
                    .position = {-4.55f, 0.175f},
                    .size = {1.0f, 0.35f},
                    .mass = 0.8f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftUpperArm,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "left_upper_arm",
                .body = {
                    .position = {-5.9f, 5.0f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.2f
                }
            },
            {
                .type = aura::body::BodyPartType::RightUpperArm,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "right_upper_arm",
                .body = {
                    .position = {-4.1f, 5.0f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.2f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftForearm,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "left_forearm",
                .body = {
                    .position = {-5.9f, 3.6f},
                    .size = {0.4f, 1.5f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::RightForearm,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "right_forearm",
                .body = {
                    .position = {-4.1f, 3.6f},
                    .size = {0.4f, 1.5f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftHand,
                .shape = aura::body::BodyPartShape::Circle,
                .name = "left_hand",
                .body = {
                    .position = {-5.9f, 2.65f},
                    .size = {0.42f, 0.42f},
                    .mass = 0.4f
                }
            },
            {
                .type = aura::body::BodyPartType::RightHand,
                .shape = aura::body::BodyPartShape::Circle,
                .name = "right_hand",
                .body = {
                    .position = {-4.1f, 2.65f},
                    .size = {0.42f, 0.42f},
                    .mass = 0.4f
                }
            }
        },
        .joints = {
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::Head,
                .localAnchorA = {0.0f, 1.25f},
                .localAnchorB = {0.0f, -0.4f},
                .minAngle = -0.3f,
                .maxAngle = 0.3f,
                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f,
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::LeftThigh,
                .localAnchorA = {-0.3f, -1.25f},
                .localAnchorB = {0.0f, 0.9f},
                .minAngle = -0.8f,
                .maxAngle = 0.8f,
                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f,
            },
            {
                .partA = aura::body::BodyPartType::LeftThigh,
                .partB = aura::body::BodyPartType::LeftShin,

                .localAnchorA = {0.0f, -0.9f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -0.1f,
                .maxAngle = 1.6f,

                .targetAngle = 0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::RightThigh,

                .localAnchorA = {0.3f, -1.25f},
                .localAnchorB = {0.0f, 0.9f},

                .minAngle = -0.8f,
                .maxAngle = 0.8f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightThigh,
                .partB = aura::body::BodyPartType::RightShin,

                .localAnchorA = {0.0f, -0.9f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -0.1f,
                .maxAngle = 1.6f,

                .targetAngle = 0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::LeftShin,
                .partB = aura::body::BodyPartType::LeftFoot,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {-0.25f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightShin,
                .partB = aura::body::BodyPartType::RightFoot,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {-0.25f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::LeftUpperArm,

                .localAnchorA = {-0.7f, 0.8f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -1.5f,
                .maxAngle = 1.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::RightUpperArm,

                .localAnchorA = {0.7f, 0.8f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -1.5f,
                .maxAngle = 1.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::LeftUpperArm,
                .partB = aura::body::BodyPartType::LeftForearm,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {0.0f, 0.75f},

                .minAngle = -0.1f,
                .maxAngle = 2.2f,

                .targetAngle = 0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightUpperArm,
                .partB = aura::body::BodyPartType::RightForearm,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {0.0f, 0.75f},

                .minAngle = -2.2f,
                .maxAngle = 0.1f,

                .targetAngle = -0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::LeftForearm,
                .partB = aura::body::BodyPartType::LeftHand,

                .localAnchorA = {0.0f, -0.7f},
                .localAnchorB = {0.0f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 8.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightForearm,
                .partB = aura::body::BodyPartType::RightHand,

                .localAnchorA = {0.0f, -0.7f},
                .localAnchorB = {0.0f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 8.0f,
                .motorDamping = 2.0f
            }
        }
    };

    auto *leftFoot = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftFoot);
    auto *rightFoot = aura::body::findPart(auraBody, aura::body::BodyPartType::RightFoot);
    auto *leftShin = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftShin);
    auto *rightShin = aura::body::findPart(auraBody, aura::body::BodyPartType::RightShin);
    auto *leftThigh = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftThigh);
    auto *rightThigh = aura::body::findPart(auraBody, aura::body::BodyPartType::RightThigh);
    auto *torso = aura::body::findPart(auraBody, aura::body::BodyPartType::Torso);

    const auto leftAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::LeftShin &&
               joint.partB == aura::body::BodyPartType::LeftFoot;
    });
    const auto rightAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::RightShin &&
               joint.partB == aura::body::BodyPartType::RightFoot;
    });
    const auto leftKnee = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::LeftThigh &&
               joint.partB == aura::body::BodyPartType::LeftShin;
    });
    const auto rightKnee = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::RightThigh &&
               joint.partB == aura::body::BodyPartType::RightShin;
    });
    const auto leftHip = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::Torso &&
               joint.partB == aura::body::BodyPartType::LeftThigh;
    });
    const auto rightHip = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::Torso &&
               joint.partB == aura::body::BodyPartType::RightThigh;
    });

    if (leftFoot != nullptr && rightFoot != nullptr &&
        leftShin != nullptr && rightShin != nullptr &&
        leftAnkle != auraBody.joints.end() && rightAnkle != auraBody.joints.end()) {
        leftFoot->body.position.y = 0.175f;
        rightFoot->body.position.y = 0.175f;

        const auto leftAnkleWorld =
                aura::physics::worldAnchorB(leftFoot->body, *leftAnkle);
        leftShin->body.position = leftAnkleWorld - leftAnkle->localAnchorA;

        const auto rightAnkleWorld =
                aura::physics::worldAnchorB(rightFoot->body, *rightAnkle);
        rightShin->body.position = rightAnkleWorld - rightAnkle->localAnchorA;
    }

    if (leftShin != nullptr && rightShin != nullptr &&
        leftThigh != nullptr && rightThigh != nullptr &&
        leftKnee != auraBody.joints.end() && rightKnee != auraBody.joints.end()) {
        const auto leftKneeWorld =
                aura::physics::worldAnchorB(leftShin->body, *leftKnee);
        leftThigh->body.position = leftKneeWorld - leftKnee->localAnchorA;

        const auto rightKneeWorld =
                aura::physics::worldAnchorB(rightShin->body, *rightKnee);
        rightThigh->body.position = rightKneeWorld - rightKnee->localAnchorA;
    }

    if (torso != nullptr && leftThigh != nullptr && rightThigh != nullptr &&
        leftHip != auraBody.joints.end() && rightHip != auraBody.joints.end()) {
        const auto leftHipWorld =
                aura::physics::worldAnchorB(leftThigh->body, *leftHip);
        const auto leftTorsoPosition =
                leftHipWorld - aura::math::rotate(leftHip->localAnchorA, torso->body.angle);

        const auto rightHipWorld =
                aura::physics::worldAnchorB(rightThigh->body, *rightHip);
        const auto rightTorsoPosition =
                rightHipWorld - aura::math::rotate(rightHip->localAnchorA, torso->body.angle);

        std::cout << "left torso candidate: ("
                << leftTorsoPosition.x << ", "
                << leftTorsoPosition.y << ")\n"
                << "right torso candidate: ("
                << rightTorsoPosition.x << ", "
                << rightTorsoPosition.y << ")\n";

        torso->body.position = (leftTorsoPosition + rightTorsoPosition) * 0.5f;
    }

    for (const auto &joint: auraBody.joints) {
        const bool isKnee =
                (joint.partA == aura::body::BodyPartType::LeftThigh &&
                 joint.partB == aura::body::BodyPartType::LeftShin) ||
                (joint.partA == aura::body::BodyPartType::RightThigh &&
                 joint.partB == aura::body::BodyPartType::RightShin);
        const bool isHip =
                (joint.partA == aura::body::BodyPartType::Torso &&
                 joint.partB == aura::body::BodyPartType::LeftThigh) ||
                (joint.partA == aura::body::BodyPartType::Torso &&
                 joint.partB == aura::body::BodyPartType::RightThigh);
        const bool isAnkle =
                (joint.partA == aura::body::BodyPartType::LeftShin &&
                 joint.partB == aura::body::BodyPartType::LeftFoot) ||
                (joint.partA == aura::body::BodyPartType::RightShin &&
                 joint.partB == aura::body::BodyPartType::RightFoot);

        if (!isHip && !isKnee && !isAnkle) {
            continue;
        }

        auto *partA =
                aura::body::findPart(auraBody, joint.partA);

        auto *partB =
                aura::body::findPart(auraBody, joint.partB);

        if (partA == nullptr || partB == nullptr) {
            continue;
        }

        const auto error =
                aura::physics::jointError(partA->body, partB->body, joint);

        std::cout << partA->name << " -> " << partB->name << " | joint error: (" << error.x << ", " << error.y << ")" <<
                " | length: " << error.length() << '\n';
    }

    if (leftFoot != nullptr && rightFoot != nullptr) {
        std::cout
                << "LEFT bottom=" << aura::physics::bottom(leftFoot->body)
                << '\n';

        std::cout
                << "RIGHT bottom=" << aura::physics::bottom(rightFoot->body)
                << '\n';
    }

    double previousTime = glfwGetTime();
    constexpr int stepsBetweenPrints = 60;
    int stepsSinceLastPrint = 0;

    constexpr bool SHOW_JOINT_DEBUG = false;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        auto dt = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // for (auto &parts: auraBody.parts)
        //     if (aura::math::nearlyEqual(aura::physics::bottom(parts.body), world.floorHeight))
        //         aura::physics::applyHorizontalDrag(parts.body, 100.0f);


        // aura::body::applyBalanceController(auraBody, world, 0.3f);
        // aura::body::applyAllJointMotors(auraBody);

        aura::body::stepAllBodyParts(auraBody, world, dt);

        aura::body::solveBodyConstraints(auraBody, world, 8);

        ++stepsSinceLastPrint;
        if (stepsSinceLastPrint >= stepsBetweenPrints) {
            const bool leftGrounded =
                    aura::body::isLeftFootGrounded(
                        auraBody,
                        world
                    );

            const bool rightGrounded =
                    aura::body::isRightFootGrounded(
                        auraBody,
                        world
                    );

            const auto com = aura::body::centerOfMass(auraBody);

            const auto support =
                    aura::body::supportInterval(
                        auraBody,
                        world
                    );

            const float balanceError =
                    aura::body::normalizedBalanceErrorX(
                        auraBody,
                        world
                    );

            std::cout << "left: " << leftGrounded << " | right: " << rightGrounded << " | COM x: " << com.x <<
                    " | support: " << support.valid <<
                    " | balance error: " << balanceError << '\n';

            stepsSinceLastPrint = 0;
        }

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        if (height == 0) height = 1;

        glViewport(0, 0, width, height);

        g_aspectRatioModifier = static_cast<float>(height) / static_cast<float>(width);

        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        // ...
        drawFloor(world);


        for (const auto &part: auraBody.parts) {
            const aura::math::Vec2 localFront{part.body.size.x * 0.5f, 0.0f};
            const aura::math::Vec2 worldFront =
                    aura::physics::localToWorldPoint(part.body, localFront);

            switch (part.shape) {
                case aura::body::BodyPartShape::Circle:
                    drawCircle(part.body);

                    if (part.type == aura::body::BodyPartType::Head) {
                        drawPoint(worldFront, 0.015f);
                    }
                    break;

                case aura::body::BodyPartShape::Rectangle:
                case aura::body::BodyPartShape::Capsule:
                    drawCapsule(part.body);
                    break;
            }
        }

        const auto &neck =
                auraBody.joints[0];

        auto *torso =
                aura::body::findPart(auraBody, aura::body::BodyPartType::Torso);

        auto *head =
                aura::body::findPart(auraBody, aura::body::BodyPartType::Head);

        if (SHOW_JOINT_DEBUG) {
            drawJoint(torso->body, head->body, neck);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
