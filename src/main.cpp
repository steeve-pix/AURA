#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "aura/render/Camera.hpp"
#include "aura/render/Mesh.hpp"
#include "aura/render/MeshFactory.hpp"
#include "aura/render/Renderer3D.hpp"
#include "aura/render/Shader.hpp"
#include "aura/render/Window.hpp"

int main() {
    aura::render::Window window{1280, 720, "AURA"};
    aura::render::Mesh cube(aura::render::MeshFactory::createCube());
    aura::render::Mesh grid{aura::render::MeshFactory::createGrid(20, 1.0f), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh floor{aura::render::MeshFactory::createFloor(40.0f)};

    aura::render::Camera camera{
        {0.0f, 3.0f, 6.0f},
        {0.0f, 0.0f, 0.0f},
        1280.0f / 720.0f
    };

    auto shader =
            aura::render::Shader::fromFiles("../assets/shaders/basic.vert", "../assets/shaders/basic.frag");

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose()) {
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("uView", camera.viewMatrix());
        shader.setMat4("uProjection", camera.projectionMatrix());

        // Solid Floor
        shader.setVec3("uColor", {0.1f, 0.1f, 0.1f});
        shader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        // Grid
        shader.setVec3("uColor", {0.35f, 0.35f, 0.4f});
        shader.setMat4("uModel", aura::math::Mat4::translation({0.0f, 0.002f, 0.0f}));
        grid.draw();

        //Cube
        shader.setVec3("uColor", {0.1f, 0.6f, 0.9f});
        shader.setMat4("uModel", aura::math::Mat4::translation({0.0, 0.5, 0.0f}));
        cube.draw();


        window.swapBuffers();
        aura::render::Window::pollEvents();
    }
    return 0;
}
