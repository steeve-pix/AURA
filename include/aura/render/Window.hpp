#pragma once
#include <functional>
#include <utility>

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
        using KeyCallback = std::function<void(int key, int action)>;

        Window(int width, int height, const char *title);

        ~Window();

        [[nodiscard]] bool shouldClose() const;

        static void pollEvents();

        void swapBuffers();

        [[nodiscard]] int framebufferWidth() const;

        [[nodiscard]] int framebufferHeight() const;

        // Logical pixels, matching GLFW cursor coordinates on Retina displays.
        [[nodiscard]] int height() const;
        [[nodiscard]] std::pair<double, double> cursorPosition() const;

        void setResizeCallback(ResizeCallback callback);

        void setMouseMoveCallback(MouseMoveCallback callback);

        void setMouseButtonCallback(MouseButtonCallback callback);

        void setScrollCallback(ScrollCallback callback);
        void setKeyCallback(KeyCallback callback);
        void setTitle(const char *title);

    private:
        GLFWwindow *window_ = nullptr;
        ResizeCallback resizeCallback_;

        MouseMoveCallback mouseMoveCallback_;
        MouseButtonCallback mouseButtonCallback_;
        ScrollCallback scrollCallback_;
        KeyCallback keyCallback_;
    };
}
