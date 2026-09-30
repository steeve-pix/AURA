#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "aura/render/Mesh.hpp"

#include <stdexcept>

namespace aura::render {
    Mesh::Mesh(const std::vector<float> &vertices, MeshPrimitive primitive)
        : primitive_(primitive) {
        if (vertices.size() % 3 != 0) {
            throw std::invalid_argument("Each vertex needs x, y, and z");
        }

        vertexCount_ = static_cast<int>(vertices.size() / 3);

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                     GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3,GL_FLOAT,GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

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
