#include "render/Window.hpp"

#include <stdexcept>
#include <GLFW/glfw3.h>

namespace aura::render{
    Window::Window(int width, int height, const char *title) {
        if(!glfwInit()) {
            throw std::runtime_error("Failed to initialise GFLW");
        }

        handle_=glfwCreateWindow(width, height, title, nullptr, nullptr);

        if (!handle_) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(handle_);
    }

    Window::~Window() {
        if (handle_) {
            glfwDestroyWindow(handle_);
        }

        glfwTerminate();
    }

    bool Window::shouldClose() const {
        return glfwWindowShouldClose(handle_);
    }

    void Window::pollEvents() const {
        glfwPollEvents();
    }   
}
