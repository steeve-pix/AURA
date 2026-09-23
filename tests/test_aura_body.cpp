#include <cassert>

#include <aura/body/AuraBody.hpp>
#include <aura/body/BalanceController.hpp>
#include <aura/math/Math.hpp>

int main()
{
    aura::body::AuraBody aura{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "torso",
                .body = {
                    .position = {0.0f, 4.0f},
                    .size = {1.0f, 2.5f},
                    .mass = 4.0f
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .name = "head",
                .body = {
                    .position = {0.0f, 6.0f},
                    .size = {0.8f, 0.8f},
                    .mass = 1.0f
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
                .targetAngle = 0.0f
            }
        }
    };

    assert(aura.parts.size() == 2);
    assert(aura.parts[0].type == aura::body::BodyPartType::Torso);
    assert(aura.parts[1].type == aura::body::BodyPartType::Head);
    assert(aura.joints.size() == 1);
    assert(aura::math::nearlyEqual(aura.joints[0].maxAngle, 0.3f));
    assert(aura.joints[0].partA == aura::body::BodyPartType::Torso);
    assert(aura.joints[0].partB == aura::body::BodyPartType::Head);

    auto *torso = aura::body::findPart(
            aura,
            aura::body::BodyPartType::Torso);
    assert(torso != nullptr);
    assert(torso->name == "torso");

    auto *head = aura::body::findPart(
            aura,
            aura::body::BodyPartType::Head);
    assert(head != nullptr);
    assert(head->name == "head");

    auto *arm = aura::body::findPart(
            aura,
            aura::body::BodyPartType::UpperArm);
    assert(arm == nullptr);

    bool solved = aura::body::solveJoint(aura, aura.joints[0]);
    assert(solved);

    aura::physics::Joint2D badJoint{
        .partA = aura::body::BodyPartType::Torso,
        .partB = aura::body::BodyPartType::Forearm
    };

    bool solvedBad = aura::body::solveJoint(aura, badJoint);
    assert(!solvedBad);

    aura.joints[0].targetAngle = 0.3f;
    aura.joints[0].motorStiffness = 10.0f;
    aura.joints[0].motorDamping = 0.0f;

    aura::body::applyAllJointMotors(aura);

    assert(aura::math::nearlyEqual(
            torso->body.torque,
            -head->body.torque));
    assert(torso->body.torque != 0.0f || head->body.torque != 0.0f);

    aura::body::solveAllJoints(aura);

    const auto error = aura::physics::jointError(
            torso->body,
            head->body,
            aura.joints[0]);

    assert(aura::math::nearlyEqual(error.x, 0.0f));
    assert(aura::math::nearlyEqual(error.y, 0.0f));

    aura::body::AuraBody fallingAura{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "torso",
                .body = {
                    .position = {0.0f, 4.0f},
                    .velocity = {0.0f, 0.0f},
                    .size = {1.0f, 1.0f}
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .name = "head",
                .body = {
                    .position = {0.0f, 6.0f},
                    .velocity = {0.0f, 0.0f},
                    .size = {1.0f, 1.0f}
                }
            }
        }
    };

    const aura::physics::World2D world{};
    aura::body::stepAllBodyParts(fallingAura, world, 0.1f);

    auto *fallingTorso = aura::body::findPart(
            fallingAura,
            aura::body::BodyPartType::Torso);
    auto *fallingHead = aura::body::findPart(
            fallingAura,
            aura::body::BodyPartType::Head);

    assert(fallingTorso->body.velocity.y < 0.0f);
    assert(fallingHead->body.velocity.y < 0.0f);

    aura::body::AuraBody feetBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .name = "left foot",
                .body = {
                    .position = {-0.5f, 0.175f},
                    .size = {1.0f, 0.35f}
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .name = "right foot",
                .body = {
                    .position = {0.5f, 1.0f},
                    .size = {1.0f, 0.35f}
                }
            }
        }
    };

    assert(aura::body::isLeftFootGrounded(feetBody, world));
    assert(!aura::body::isRightFootGrounded(feetBody, world));

    aura::body::AuraBody weightedBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "light",
                .body = {
                    .position = {0.0f, 0.0f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .name = "heavy",
                .body = {
                    .position = {10.0f, 0.0f},
                    .mass = 3.0f
                }
            }
        }
    };

    const auto com = aura::body::centerOfMass(weightedBody);
    assert(aura::math::nearlyEqual(com.x, 7.5f));
    assert(aura::math::nearlyEqual(com.y, 0.0f));

    aura::body::AuraBody supportBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .name = "left foot",
                .body = {
                    .position = {-1.0f, 0.5f},
                    .size = {2.0f, 1.0f}
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .name = "right foot",
                .body = {
                    .position = {1.0f, 0.5f},
                    .size = {2.0f, 1.0f}
                }
            }
        }
    };

    const auto bothFeetSupport =
            aura::body::supportInterval(supportBody, world);
    assert(bothFeetSupport.valid);
    assert(aura::math::nearlyEqual(bothFeetSupport.minX, -2.0f));
    assert(aura::math::nearlyEqual(bothFeetSupport.maxX, 2.0f));

    supportBody.parts[1].body.position.y = 1.0f;

    const auto oneFootSupport =
            aura::body::supportInterval(supportBody, world);
    assert(oneFootSupport.valid);
    assert(aura::math::nearlyEqual(oneFootSupport.minX, -2.0f));
    assert(aura::math::nearlyEqual(oneFootSupport.maxX, 0.0f));

    aura::body::AuraBody balanceBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "torso",
                .body = {
                    .position = {0.0f, 2.0f},
                    .size = {1.0f, 2.0f},
                    .mass = 10.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .name = "left foot",
                .body = {
                    .position = {-1.0f, 0.5f},
                    .size = {1.0f, 1.0f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .name = "right foot",
                .body = {
                    .position = {1.0f, 0.5f},
                    .size = {1.0f, 1.0f},
                    .mass = 1.0f
                }
            }
        },
        .joints = {
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::LeftThigh
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::RightThigh
            }
        }
    };

    const auto balanceSupport =
            aura::body::supportInterval(balanceBody, world);
    const auto balanceCom = aura::body::centerOfMass(balanceBody);

    assert(balanceCom.x >= balanceSupport.minX &&
           balanceCom.x <= balanceSupport.maxX);
    assert(aura::body::isBalanced(balanceBody, world));
    assert(aura::math::nearlyEqual(
            aura::body::balanceErrorX(balanceBody, world),
            0.0f));
    assert(aura::math::nearlyEqual(
            aura::body::normalizedBalanceErrorX(balanceBody, world),
            0.0f));

    auto *balanceTorso = aura::body::findPart(
            balanceBody,
            aura::body::BodyPartType::Torso);

    // The feet define a support interval from -1.5 to 1.5.
    // Moving the torso to 1.8 puts the weighted COM at the right edge.
    balanceTorso->body.position.x = 1.8f;
    const auto rightEdgeCom = aura::body::centerOfMass(balanceBody);
    assert(aura::math::nearlyEqual(rightEdgeCom.x, balanceSupport.maxX));
    assert(aura::math::nearlyEqual(
            aura::body::normalizedBalanceErrorX(balanceBody, world),
            1.0f));

    balanceTorso->body.position.x = -1.8f;
    const auto leftEdgeCom = aura::body::centerOfMass(balanceBody);
    assert(aura::math::nearlyEqual(leftEdgeCom.x, balanceSupport.minX));
    assert(aura::math::nearlyEqual(
            aura::body::normalizedBalanceErrorX(balanceBody, world),
            -1.0f));

    balanceTorso->body.position.x = 10.0f;

    assert(!aura::body::isBalanced(balanceBody, world));
    assert(aura::math::nearlyEqual(
            aura::body::balanceErrorX(balanceBody, world),
            8.333333f));

    aura::body::applyBalanceController(balanceBody, world, 0.3f);
    assert(balanceBody.joints[0].targetAngle < 0.0f);
    assert(balanceBody.joints[1].targetAngle < 0.0f);

    aura::body::AuraBody skeleton{
        .parts = {
            {.type = aura::body::BodyPartType::Torso, .name = "torso"},
            {.type = aura::body::BodyPartType::Head, .name = "head"},
            {.type = aura::body::BodyPartType::LeftThigh, .name = "left thigh"},
            {.type = aura::body::BodyPartType::LeftShin, .name = "left shin"}
        },
        .joints = {
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::LeftThigh
            },
            {
                .partA = aura::body::BodyPartType::LeftThigh,
                .partB = aura::body::BodyPartType::LeftShin
            }
        }
    };

    assert(aura::body::findPart(
                   skeleton,
                   aura::body::BodyPartType::Torso) != nullptr);
    assert(aura::body::findPart(
                   skeleton,
                   aura::body::BodyPartType::Head) != nullptr);
    assert(aura::body::findPart(
                   skeleton,
                   aura::body::BodyPartType::LeftThigh) != nullptr);
    assert(aura::body::findPart(
                   skeleton,
                   aura::body::BodyPartType::LeftShin) != nullptr);
    assert(skeleton.joints[0].partA == aura::body::BodyPartType::Torso);
    assert(skeleton.joints[0].partB == aura::body::BodyPartType::LeftThigh);
    assert(skeleton.joints[1].partA == aura::body::BodyPartType::LeftThigh);
    assert(skeleton.joints[1].partB == aura::body::BodyPartType::LeftShin);

    return 0;
}
