#pragma once
struct GLFWwindow;

namespace aura::render {
    class Window {
    public:
        Window(int width, int height, const char *title);

        ~Window();

        [[nodiscard]] bool shouldClose() const;

        static void pollEvents();

        void swapBuffers();

    private:
        GLFWwindow *window_ = nullptr;
    };
}
