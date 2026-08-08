add_executable(aura_body
        src/main.cpp
        src/bridge/ActionParser.cpp
        src/bridge/ActionUtils.cpp
        src/bridge/ObservationSerializer.cpp
        src/world/CellTypeUtils.cpp
        src/world/World.cpp
        src/render/TerminalRenderer.cpp
        src/agent/Agent.cpp
        src/sensors/LocalSensor.cpp
)

target_include_directories(aura_body PRIVATE include)