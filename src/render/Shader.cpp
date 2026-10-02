#include <GLFW/glfw3.h>

#include <fstream>
#include <sstream>
#include "aura/render/Shader.hpp"

namespace aura::render {
    namespace {
        unsigned int compileShader(unsigned int type, const std::string &source) {
            const unsigned int shader =
                    glCreateShader(type);

            const char *sourcePointer =
                    source.c_str();

            glShaderSource(shader, 1, &sourcePointer, nullptr);

            glCompileShader(shader);
            int success = 0;
            glGetShaderiv(shader,GL_COMPILE_STATUS, &success);

            if (!success) {
                glDeleteShader(shader);
                throw std::runtime_error("Failed to compile shader");
            }

            return shader;
        }

        std::string readFile(const std::string &path) {
            std::ifstream file{path};

            if (!file) {
                throw std::runtime_error("Failed to open shader file: " + path);
            }

            std::stringstream stream;
            stream << file.rdbuf();

            return stream.str();
        }
    }

    Shader::Shader(const std::string &vertexSource, const std::string &fragmentSource) {
        const unsigned int vertexShader =
                compileShader(GL_VERTEX_SHADER, vertexSource);

        const unsigned int fragmentShader =
                compileShader(GL_FRAGMENT_SHADER, fragmentSource);

        program_ = glCreateProgram();

        glAttachShader(program_, vertexShader);
        glAttachShader(program_, fragmentShader);

        glLinkProgram(program_);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        int success = 0;

        glGetProgramiv(program_,GL_LINK_STATUS, &success);

        if (!success) {
            glDeleteProgram(program_);
            program_ = 0;

            throw std::runtime_error("Failed to link shader program");
        }
    }

    Shader::~Shader() {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    void Shader::use() const {
        glUseProgram(program_);
    }

    Shader Shader::fromFiles(const std::string &vertexPath, const std::string &fragmentPath) {
        return Shader{
            readFile(vertexPath), readFile(fragmentPath)
        };
    }

    void Shader::setMat4(const std::string &name, const math::Mat4 &matrix) const {
        const int location =
                glGetUniformLocation(program_, name.c_str());

        glUniformMatrix4fv(location, 1,GL_FALSE, matrix.data());
    }

    void Shader::setVec3(const std::string &name, const math::Vec3 &value) const {
        const int location =
                glGetUniformLocation(program_, name.c_str());

        glUniform3f(location, value.x, value.y, value.z);
    }

    void Shader::setInt(const std::string &name, int value) const {
        const int location =
                glGetUniformLocation(program_, name.c_str());

        glUniform1i(location, value);
    }
}
