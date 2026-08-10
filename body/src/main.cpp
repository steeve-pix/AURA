#include <iostream>
#include <exception>

#include "agent/Agent.hpp"
#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "navigation/Pathfinder.hpp"
#include "render/TerminalRenderer.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"
#include "world/CellType.hpp"
#include "world/Position.hpp"
#include "world/World.hpp"

#include "render/Window.hpp"

int main() {
    using aura::render::Window;

    Window window{1280,720,"AURA"};

    while (!window.shouldClose()) {
        window.pollEvents();
    }

    return 0;
}