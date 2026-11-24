#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "aura/render/Mesh.hpp"

#include <stdexcept>

namespace aura::render {
    Mesh::Mesh(const std::vector<float> &vertices, MeshPrimitive primitive, VertexLayout layout)
        : primitive_(primitive) {
        const int stride =
                layout == VertexLayout::PositionNormal ? 6 : 3;

        if (vertices.size() % stride != 0) {
            throw std::invalid_argument("Vertex data does not match its layout");
        }

        vertexCount_ = static_cast<int>(vertices.size() / stride);


        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                     GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(stride * sizeof(float)), nullptr);
        glEnableVertexAttribArray(0);

        if (layout == VertexLayout::PositionNormal) {
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                  static_cast<GLsizei>(stride * sizeof(float)),
                                  reinterpret_cast<const void *>(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    Mesh::~Mesh() {
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
    }

    void Mesh::draw() const {
        glBindVertexArray(vao_);
        const GLenum mode = primitive_ == MeshPrimitive::Lines ? GL_LINES : GL_TRIANGLES;
        glDrawArrays(mode, 0, vertexCount_);
        glBindVertexArray(0);
    }
}
