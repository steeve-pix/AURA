#pragma once
#include <string>

#include "aura/math/Mat4.hpp"

namespace aura::render {
    class Shader {
    public:
        Shader(const std::string &vertexSource, const std::string &fragmentSource);

        ~Shader();

        void use() const;

        static Shader fromFiles(const std::string &vertexPath, const std::string &fragmentPath);

        void setMat4(const std::string &name, const math::Mat4 &matrix);

        void setVec3(const std::string &name, const math::Vec3 &value);

    private:
        unsigned int program_ = 0;
    };
}
