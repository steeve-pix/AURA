add_executable(aura_body
        src/main.cpp
        src/world/World.cpp
        src/render/TerminalRenderer.cpp
)

target_include_directories(aura_body PRIVATE include)