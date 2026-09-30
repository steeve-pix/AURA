#include <GLFW/glfw3.h>
#include "aura/render/Renderer3D.hpp"
#include "aura/render/Window.hpp"

int main() {
    aura::render::Window window{1280, 720, "AURA"};
    aura::physics::Renderer3D renderer;

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose()) {
        renderer.clear();
        renderer.drawCube();

        window.swapBuffers();
        aura::render::Window::pollEvents();
    }
    return 0;
}
