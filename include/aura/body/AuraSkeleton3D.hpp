#pragma once
#include "AuraBody3D.hpp"
#include "Joint3D.hpp"
#include <vector>

namespace aura::body {
    struct AuraSkeleton3D {
        Joint3D waist;
        Joint3D neck;
        Joint3D head;

        Joint3D leftShoulder;
        Joint3D leftElbow;
        Joint3D leftWrist;

        Joint3D rightShoulder;
        Joint3D rightElbow;
        Joint3D rightWrist;

        Joint3D leftHip;
        Joint3D leftKnee;
        Joint3D leftAnkle;

        Joint3D rightHip;
        Joint3D rightKnee;
        Joint3D rightAnkle;

        // Query this skeleton's topology against the supplied body. No body pointers
        // are retained, so copies of a body/skeleton can be queried independently.
        // jointToCut must be one of this skeleton's joint members.
        std::vector<BodyPart3D *> collectComponent(AuraBody3D &body, BodyPart3D &start,
                                                  const Joint3D &jointToCut) const;
    };

    // Create joints from an assembled body with identity orientations.
    AuraSkeleton3D createAuraSkeleton3D(AuraBody3D &body);
}
