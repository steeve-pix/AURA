#include <GLFW/glfw3.h>

#include "render/GridRenderer.hpp"
#include "render/Window.hpp"

int main() {
    using aura::render::Window;
    using aura::render::GridRenderer;

    Window window{1280, 720, "AURA"};

    aura::world::World world{10, 5};
    world.setCell({6,3},aura::world::CellType::Battery);

    aura::agent::Agent agent{{.x = 3, .y = 2}};

    while (!window.shouldClose()) {
        Window::pollEvents();
        window.clear();

        GridRenderer::render(world, agent);

        window.display();
    }

    return 0;
}
