#include "aura/render/Window.hpp"

#include <stdexcept>
#include <utility>

#include "GLFW/glfw3.h"

namespace aura::render {
    Window::Window(int width, int height, const char *title) {
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW");

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);


        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (window_ == nullptr) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwSetWindowUserPointer(window_, this);

        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow *glfwWindow, int width, int height) {
            auto *window =
                    static_cast<Window *>(glfwGetWindowUserPointer(glfwWindow));

            if (window != nullptr && window->resizeCallback_) {
                window->resizeCallback_(width, height);
            }
        });

        glfwMakeContextCurrent(window_);
    }

    Window::~Window() {
        if (window_ != nullptr)
            glfwDestroyWindow(window_);

        glfwTerminate();
    }

    bool Window::shouldClose() const {
        return glfwWindowShouldClose(window_);
    }

    void Window::pollEvents() {
        glfwPollEvents();
    }

    void Window::swapBuffers() {
        glfwSwapBuffers(window_);
    }

    int Window::framebufferWidth() const {
        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window_, &width, &height);

        return width;
    }

    int Window::framebufferHeight() const {
        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window_, &width, &height);

        return height;
    }

    void Window::setResizeCallback(ResizeCallback callback) {
        resizeCallback_ = std::move(callback);
    }
}
