#pragma once
#include <functional>

struct GLFWwindow;

namespace aura::render {
    class Window {
    public:
        using ResizeCallback = std::function<void(int width, int height)>;

        Window(int width, int height, const char *title);

        ~Window();

        [[nodiscard]] bool shouldClose() const;

        static void pollEvents();

        void swapBuffers();

        [[nodiscard]] int framebufferWidth() const;

        [[nodiscard]] int framebufferHeight() const;

        void setResizeCallback(ResizeCallback callback);

    private:
        GLFWwindow *window_ = nullptr;
        ResizeCallback resizeCallback_;
    };
}
