#pragma once
#include <string>

namespace aura::render {
    class Shader {
    public:
        Shader(const std::string &vertexSource, const std::string &fragmentSource);

        ~Shader();

        void use() const;

    static Shader fromFiles(const std::string& vertexPath,const std::string& fragmentPath);
    private:
        unsigned int program_ = 0;
    };
}
