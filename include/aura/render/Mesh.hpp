#pragma once
#include <vector>

namespace aura::render {
    enum class MeshPrimitive {
        Triangles,
        Lines
    };

    class Mesh {
    public:
        explicit Mesh(const std::vector<float> &vertices,
                      MeshPrimitive primitive = MeshPrimitive::Triangles);

        ~Mesh();

        Mesh(const Mesh &) = delete;

        Mesh &operator=(const Mesh &) = delete;

        void draw() const;

    private:
        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        int vertexCount_ = 0;
        MeshPrimitive primitive_;
    };
}
