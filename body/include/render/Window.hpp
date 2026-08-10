#pragma once

struct GLFWwindow;

namespace aura::render {
    class Window {
    public:
        Window(int width, int height, const char *title);

        ~Window();

        Window(const Window &) = delete;

        Window &operator=(const Window &) = delete;

        [[nodiscard]] bool shouldClose() const;

        static void pollEvents();

        void clear() const;

        void display() const;

    private:
        GLFWwindow *handle_ = nullptr;
    };
}
