#include "aura/render/Renderer3D.hpp"
#include <GLFW/glfw3.h>

namespace aura::physics {
    void Renderer3D::clear() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer3D::drawCube() {
    }
}
