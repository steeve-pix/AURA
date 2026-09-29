#include <GLFW/glfw3.h>
#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>

#include "aura/body/AuraBody.hpp"
#include "aura/body/BalanceController.hpp"
#include "aura/math/Rotation.hpp"
#include "aura/physics/Body2D.hpp"
#include "aura/physics/Joint2D.hpp"
#include "aura/physics/JointConstraint.hpp"
#include "aura/physics/Physics.hpp"
#include "aura/physics/World2D.hpp"
#include "aura/training/ActionJson.hpp"
#include "aura/training/ObservationBuilder.hpp"
#include "aura/training/ObservationJson.hpp"
static float g_aspectRatioModifier = 1.0f;

static float worldToScreenY(float worldY) {
    constexpr float scale = 0.1f;
    constexpr float verticalOffset = -0.6f;

    return worldY * scale + verticalOffset;
}

static void drawFloor(const aura::physics::World2D &world) {
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

static void drawPoint(const aura::math::Vec2 &worldPoint, float size = 0.02f) {
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

static void drawJoint(const aura::physics::Body2D &bodyA, const aura::physics::Body2D &bodyB,
                      const aura::physics::Joint2D &joint) {
    const auto anchorA =
            aura::physics::worldAnchorA(bodyA, joint);

    const auto anchorB =
            aura::physics::worldAnchorB(bodyB, joint);

    drawPoint(anchorA);
    drawPoint(anchorB);
}

static void drawRectangle(const aura::physics::Body2D &body) {
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

    glColor3f(1.0f, 1.0f, 1.0f);

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

static void drawCircle(const aura::physics::Body2D &body, int segments = 32) {
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

static void drawCapsule(const aura::physics::Body2D &body, int segments = 16) {
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

int main(int argc, char *argv[]) {
    const bool observationOnce =
            argc > 1 && std::string_view(argv[1]) == "--observation-once";
    const bool actionOnce =
            argc > 1 && std::string_view(argv[1]) == "--action-once";
    const bool trainingLoop =
            argc > 1 && std::string_view(argv[1]) == "--training-loop";

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
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "left_thigh",
                .body = {
                    .position = {-5.3f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftShin,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "left_shin",
                .body = {
                    .position = {-5.3f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::RightThigh,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "right_thigh",
                .body = {
                    .position = {-4.7f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::RightShin,
                .shape = aura::body::BodyPartShape::Capsule,
                .name = "right_shin",
                .body = {
                    .position = {-4.7f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .shape = aura::body::BodyPartShape::Rectangle,
                .name = "left_foot",
                .body = {
                    .position = {-5.15f, 0.175f},
                    .size = {1.0f, 0.35f},
                    .mass = 0.8f
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .shape = aura::body::BodyPartShape::Rectangle,
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

                .targetAngle = 0.0f,
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

                .targetAngle = 0.0f,
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

                .targetAngle = 0.0f,
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

                .targetAngle = 0.0f,
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
    auto *head = aura::body::findPart(auraBody, aura::body::BodyPartType::Head);
    auto *leftUpperArm = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftUpperArm);
    auto *rightUpperArm = aura::body::findPart(auraBody, aura::body::BodyPartType::RightUpperArm);
    auto *leftForearm = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftForearm);
    auto *rightForearm = aura::body::findPart(auraBody, aura::body::BodyPartType::RightForearm);
    auto *leftHand = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftHand);
    auto *rightHand = aura::body::findPart(auraBody, aura::body::BodyPartType::RightHand);

    auto leftAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::LeftShin &&
               joint.partB == aura::body::BodyPartType::LeftFoot;
    });
    auto rightAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
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
    const auto neck = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::Torso &&
               joint.partB == aura::body::BodyPartType::Head;
    });
    const auto leftShoulder = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::Torso &&
               joint.partB == aura::body::BodyPartType::LeftUpperArm;
    });
    const auto rightShoulder = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::Torso &&
               joint.partB == aura::body::BodyPartType::RightUpperArm;
    });
    const auto leftElbow = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::LeftUpperArm &&
               joint.partB == aura::body::BodyPartType::LeftForearm;
    });
    const auto rightElbow = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::RightUpperArm &&
               joint.partB == aura::body::BodyPartType::RightForearm;
    });
    const auto leftWrist = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::LeftForearm &&
               joint.partB == aura::body::BodyPartType::LeftHand;
    });
    const auto rightWrist = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
        return joint.partA == aura::body::BodyPartType::RightForearm &&
               joint.partB == aura::body::BodyPartType::RightHand;
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

        torso->body.position = (leftTorsoPosition + rightTorsoPosition) * 0.5f;
    }

    if (torso != nullptr && head != nullptr && neck != auraBody.joints.end()) {
        const auto neckWorld =
                aura::physics::worldAnchorA(torso->body, *neck);
        head->body.position =
                neckWorld - aura::math::rotate(neck->localAnchorB, head->body.angle);
    }

    if (torso != nullptr && leftUpperArm != nullptr && rightUpperArm != nullptr &&
        leftShoulder != auraBody.joints.end() && rightShoulder != auraBody.joints.end()) {
        const auto leftShoulderWorld =
                aura::physics::worldAnchorA(torso->body, *leftShoulder);
        leftUpperArm->body.position =
                leftShoulderWorld - aura::math::rotate(leftShoulder->localAnchorB, leftUpperArm->body.angle);

        const auto rightShoulderWorld =
                aura::physics::worldAnchorA(torso->body, *rightShoulder);
        rightUpperArm->body.position =
                rightShoulderWorld - aura::math::rotate(rightShoulder->localAnchorB, rightUpperArm->body.angle);
    }

    if (leftForearm != nullptr && rightForearm != nullptr &&
        leftUpperArm != nullptr && rightUpperArm != nullptr &&
        leftElbow != auraBody.joints.end() &&
        rightElbow != auraBody.joints.end()) {
        const auto leftElbowWorld =
                aura::physics::worldAnchorA(leftUpperArm->body, *leftElbow);
        leftForearm->body.position =
                leftElbowWorld - aura::math::rotate(leftElbow->localAnchorB, leftForearm->body.angle);

        const auto rightElbowWorld =
                aura::physics::worldAnchorA(rightUpperArm->body, *rightElbow);
        rightForearm->body.position =
                rightElbowWorld - aura::math::rotate(rightElbow->localAnchorB, rightForearm->body.angle);
    }

    if (leftForearm != nullptr && rightForearm != nullptr &&
        leftHand != nullptr && rightHand != nullptr &&
        leftWrist != auraBody.joints.end() && rightWrist != auraBody.joints.end()) {
        const auto leftWristWorld =
                aura::physics::worldAnchorA(leftForearm->body, *leftWrist);
        leftHand->body.position =
                leftWristWorld - aura::math::rotate(leftWrist->localAnchorB, leftHand->body.angle);

        const auto rightWristWorld =
                aura::physics::worldAnchorA(rightForearm->body, *rightWrist);
        rightHand->body.position =
                rightWristWorld - aura::math::rotate(rightWrist->localAnchorB, rightHand->body.angle);
    }

    // Keep the fully assembled initial pose as the reset state.
    const aura::body::AuraBody initialAuraBody = auraBody;

    constexpr float FIXED_DT = 1.0f / 120.0f;
    constexpr int LOG_INTERVAL_STEPS = 120;
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
    int diagnosticStep = 0;
    aura::body::FootContactState footContactState{};
    float previousBalanceError = 0.0f;
    bool hasPreviousBalanceError = false;

    constexpr bool SHOW_JOINT_DEBUG = false;

    if (actionOnce || trainingLoop) {
        if (leftShin == nullptr || leftFoot == nullptr ||
            rightShin == nullptr || rightFoot == nullptr ||
            leftAnkle == auraBody.joints.end() || rightAnkle == auraBody.joints.end()) {
            std::cerr << "AURA is missing an ankle joint or body part.\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return 2;
        }
        const auto advanceWithAction = [&](const aura::training::Action &action) {
            aura::body::updateFootContactState(auraBody, world, footContactState);
            aura::physics::applyJointTorque(
                leftShin->body, leftFoot->body, action.leftAnkleTorque);
            aura::physics::applyJointTorque(
                rightShin->body, rightFoot->body, action.rightAnkleTorque);

            aura::body::stepAllBodyParts(auraBody, world, FIXED_DT);
            aura::body::solveBodyConstraints(auraBody, world, 8);
            aura::body::updateFootContactState(auraBody, world, footContactState);

            const auto observation = aura::training::makeObservation(
                auraBody, footContactState, world, previousBalanceError,
                hasPreviousBalanceError, FIXED_DT);
            const bool hasSupport =
                    observation.leftFootContact || observation.rightFootContact;
            if (hasSupport) {
                previousBalanceError = observation.balanceError;
                hasPreviousBalanceError = true;
            } else {
                hasPreviousBalanceError = false;
            }
            return observation;
        };

        const auto resetSimulation = [&](float pushX) {
            auraBody = initialAuraBody;

            // Restoring the body may relocate its vectors, so refresh saved references.
            torso = aura::body::findPart(auraBody, aura::body::BodyPartType::Torso);
            leftFoot = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftFoot);
            rightFoot = aura::body::findPart(auraBody, aura::body::BodyPartType::RightFoot);
            leftShin = aura::body::findPart(auraBody, aura::body::BodyPartType::LeftShin);
            rightShin = aura::body::findPart(auraBody, aura::body::BodyPartType::RightShin);
            leftAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
                return joint.partA == aura::body::BodyPartType::LeftShin &&
                       joint.partB == aura::body::BodyPartType::LeftFoot;
            });
            rightAnkle = std::find_if(auraBody.joints.begin(), auraBody.joints.end(), [](const auto &joint) {
                return joint.partA == aura::body::BodyPartType::RightShin &&
                       joint.partB == aura::body::BodyPartType::RightFoot;
            });

            footContactState = {};
            previousBalanceError = 0.0f;
            hasPreviousBalanceError = false;
            aura::body::updateFootContactState(auraBody, world, footContactState);

            if (torso != nullptr && pushX != 0.0f) {
                const aura::math::Vec2 impulse{pushX, 0.0f};
                aura::physics::applyImpulseAtPoint(
                        torso->body, impulse, torso->body.position);
            }

            return aura::training::makeObservation(
                    auraBody, footContactState, world, previousBalanceError,
                    hasPreviousBalanceError, FIXED_DT);
        };

        if (actionOnce) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                std::cerr << "Expected one JSON action on stdin.\n";
                glfwDestroyWindow(window);
                glfwTerminate();
                return 2;
            }

            try {
                const auto action = aura::training::actionFromJson(line);
                const auto observation = advanceWithAction(action);
                std::cout << aura::training::ObservationToJson(observation) << '\n';
            } catch (const std::exception &error) {
                std::cerr << "Could not process action JSON: " << error.what() << '\n';
                glfwDestroyWindow(window);
                glfwTerminate();
                return 2;
            }

            glfwDestroyWindow(window);
            glfwTerminate();
            return 0;
        }

        if (trainingLoop) {
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    const auto message = aura::training::trainingMessageFromJson(line);
                    const auto observation =
                            message.type == aura::training::TrainingMessageType::Reset
                                ? resetSimulation(message.pushX)
                                : advanceWithAction(message.action);
                    std::cout << aura::training::ObservationToJson(observation) << '\n';
                    std::cout.flush();
                } catch (const std::exception &error) {
                    std::cerr << "Could not process training message: " << error.what() << '\n';
                    glfwDestroyWindow(window);
                    glfwTerminate();
                    return 2;
                }
            }
        }

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    if (observationOnce) {
        aura::body::updateFootContactState(auraBody, world, footContactState);
        aura::body::applyBalanceController(auraBody, world, footContactState, 0.1f);
        aura::body::applyAllJointMotors(auraBody);
        aura::body::stepAllBodyParts(auraBody, world, FIXED_DT);
        aura::body::solveBodyConstraints(auraBody, world, 8);
        aura::body::updateFootContactState(auraBody, world, footContactState);

        const auto observation = aura::training::makeObservation(
                auraBody, footContactState, world, previousBalanceError,
                hasPreviousBalanceError, FIXED_DT);
        std::cout << aura::training::ObservationToJson(observation) << '\n';

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    constexpr auto printTableBorder = [] {
        std::cout <<
                "+----------+-------------+---------------+---------+---------+-----------+---------------+---------------+\n";
    };
    printTableBorder();
    std::cout << std::left
            << "| " << std::setw(8) << "Time (s)"
            << " | " << std::setw(11) << "Torso (deg)"
            << " | " << std::setw(13) << "Omega (rad/s)"
            << " | " << std::setw(7) << "Support"
            << " | " << std::setw(7) << "Balance"
            << " | " << std::setw(9) << "Rate (/s)"
            << " | " << std::setw(13) << "L ankle (rad)"
            << " | " << std::setw(13) << "R ankle (rad)" << " |\n";
    printTableBorder();

    while (!glfwWindowShouldClose(window)) {
        const double currentTime = glfwGetTime();
        accumulator += currentTime - previousTime;
        previousTime = currentTime;

        while (accumulator >= FIXED_DT) {
            const int step = diagnosticStep + 1;
            aura::body::updateFootContactState(auraBody, world, footContactState);
            aura::body::applyBalanceController(auraBody, world, footContactState, 0.1f);
            aura::body::applyAllJointMotors(auraBody);

            aura::body::stepAllBodyParts(auraBody, world, FIXED_DT);
            aura::body::solveBodyConstraints(auraBody, world, 8);
            aura::body::updateFootContactState(auraBody, world, footContactState);

            const auto observation = aura::training::makeObservation(
                auraBody, footContactState, world, previousBalanceError, hasPreviousBalanceError, FIXED_DT);

            const bool hasSupport =
                    observation.leftFootContact || observation.rightFootContact;
            if (hasSupport) {
                previousBalanceError = observation.balanceError;
                hasPreviousBalanceError = true;
            } else {
                hasPreviousBalanceError = false;
            }

            if (step % LOG_INTERVAL_STEPS == 0) {
                const char *support = observation.leftFootContact
                                          ? (observation.rightFootContact ? "both" : "left")
                                          : (observation.rightFootContact ? "right" : "none");
                const float torsoAngleDegrees =
                        observation.torsoAngle * 180.0f / std::numbers::pi_v<float>;
                const float simulationTime = static_cast<float>(step) * FIXED_DT;

                std::cout << std::fixed << std::setprecision(2) << std::right
                        << "| " << std::setw(8) << simulationTime
                        << " | " << std::setw(11) << torsoAngleDegrees
                        << " | " << std::setw(13) << observation.torsoAngularVelocity
                        << " | " << std::left << std::setw(7) << support << std::right << " | ";
                if (hasSupport) {
                    std::cout << std::setw(7) << observation.balanceError
                            << " | " << std::setw(9) << observation.balanceErrorRate;
                } else {
                    std::cout << std::setw(7) << "n/a"
                            << " | " << std::setw(9) << "n/a";
                }
                std::cout << " | " << std::setw(13) << observation.leftAnkleAngle
                        << " | " << std::setw(13) << observation.rightAnkleAngle << " |\n";
            }
            ++diagnosticStep;
            accumulator -= FIXED_DT;
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
                    drawRectangle(part.body);
                    break;
                case aura::body::BodyPartShape::Capsule:
                    drawCapsule(part.body);
                    break;
            }
        }

        if (SHOW_JOINT_DEBUG)
            drawJoint(torso->body, head->body, *neck);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
