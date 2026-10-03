#pragma once
#include <functional>

struct GLFWwindow;

namespace aura::render {
    using MouseMoveCallback =
    std::function<void(double x, double y)>;

    using MouseButtonCallback =
    std::function<void(int button, int action)>;

    using ScrollCallback =
    std::function<void(double xOffset, double yOffset)>;

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

        void setMouseMoveCallback(MouseMoveCallback callback);

        void setMouseButtonCallback(MouseButtonCallback callback);

        void setScrollCallback(ScrollCallback callback);

    private:
        GLFWwindow *window_ = nullptr;
        ResizeCallback resizeCallback_;

        MouseMoveCallback mouseMoveCallback_;
        MouseButtonCallback mouseButtonCallback_;
        ScrollCallback scrollCallback_;
    };
}
