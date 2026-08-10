#pragma once

struct GLFWwindow;

namespace aura::render {
class Window {
public:
    Window(int width,int height,const char* title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose()const;

    void pollEvents() const;

    private:
    GLFWwindow*handle_ = nullptr;
};

}
