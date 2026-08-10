add_library(aura_core
        src/agent/Agent.cpp
        src/bridge/ActionParser.cpp
        src/bridge/ActionUtils.cpp
        src/bridge/ObservationSerializer.cpp
        src/navigation/Pathfinder.cpp
        src/render/TerminalRenderer.cpp
        src/sensors/LocalSensor.cpp
        src/sensors/RangeSensor.cpp
        src/world/CellTypeUtils.cpp
        src/world/World.cpp
)

target_include_directories(aura_core PUBLIC include)

include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
)

FetchContent_MakeAvailable(glfw)

target_link_libraries(aura_core
        PRIVATE
        nlohmann_json::nlohmann_json
)

add_executable(aura_body
        src/main.cpp
        src/bridge/BrainProcess.cpp
        src/render/Window.cpp
)

target_link_libraries(aura_body
        PRIVATE
        aura_core
        glfw
)
