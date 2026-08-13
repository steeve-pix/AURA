#pragma once
#include <string>

struct GLFWwindow;

namespace aura::render {
    /// Owns the GLFW window and OpenGL context used by the graphical renderer.
    class Window {
    public:
        /// Initializes GLFW and creates a window with the requested dimensions.
        ///
        /// Throws std::runtime_error when initialization or creation fails.
        Window(int width, int height, const char *title);

        /// Destroys the window and terminates GLFW.
        ~Window();

        /// A window uniquely owns its native GLFW handle.
        Window(const Window &) = delete;

        /// A window uniquely owns its native GLFW handle.
        Window &operator=(const Window &) = delete;

        /// Reports whether the user or platform requested that the window close.
        [[nodiscard]] bool shouldClose() const;

        /// Dispatches pending GLFW input and window events.
        static void pollEvents();

        /// Clears the current frame buffer before rendering.
        void clear() const;

        /// Presents the completed frame by swapping the front and back buffers.
        void display() const;
        /// Replaces the native window title.
        void setTitle(const std::string &title) const;

    private:
        GLFWwindow *handle_ = nullptr;
    };
}
