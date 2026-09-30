#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>
#include "aura/render/Renderer3D.hpp"
#include "aura/render/Shader.hpp"
#include "aura/render/Window.hpp"

int main() {
    aura::render::Window window{1280, 720, "AURA"};
    aura::physics::Renderer3D renderer;

    float vertices[] = {
        -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        0.0f, 0.5f, 0.0f
    };

    unsigned int vao = 0;
    unsigned int vbo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3,GL_FLOAT,GL_FALSE, 3 * sizeof(float), nullptr);

    glEnableVertexAttribArray(0);

    auto shader =
            aura::render::Shader::fromFiles("../assets/shaders/basic.vert", "../assets/shaders/basic.frag");

    glEnable(GL_DEPTH_TEST);

    while (!window.shouldClose()) {
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        glBindVertexArray(vao);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        window.swapBuffers();
        aura::render::Window::pollEvents();
    }
    return 0;
}
