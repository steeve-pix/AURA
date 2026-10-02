#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "aura/render/ShadowMap.hpp"

#include <stdexcept>

namespace aura::render {
    ShadowMap::ShadowMap(int width, int height)
        : width_(width), height_(height) {
        glGenFramebuffers(1, &frameBuffer_);
        glGenTextures(1, &depthTexture_);
        glBindTexture(GL_TEXTURE_2D, depthTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0,GL_DEPTH_COMPONENT, width_, height_, 0,GL_DEPTH_COMPONENT,GL_FLOAT, nullptr);

        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);

        const float borderColor[] = {
            1.0f, 1.0f, 1.0f, 1.0f
        };

        glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR, borderColor);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D, depthTexture_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Shadow framebuffer is incomplete");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ShadowMap::~ShadowMap() {
        glDeleteTextures(1, &depthTexture_);
        glDeleteFramebuffers(1, &frameBuffer_);
    }

    void ShadowMap::bindForWriting() const {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer_);
        glViewport(0, 0, width_, height_);
    }

    void ShadowMap::bindTexture(unsigned int textureUnit) const {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, depthTexture_);
    }

    int ShadowMap::width() const {
        return width_;
    }

    int ShadowMap::height() const {
        return height_;
    }
}
