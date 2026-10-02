#pragma once

namespace aura::render {
    class ShadowMap {
    public:
        ShadowMap(int width, int height);

        ~ShadowMap();

        ShadowMap(const ShadowMap &) = delete;

        ShadowMap &operator=(const ShadowMap &) = delete;

        void bindForWriting() const;

        void bindTexture(unsigned int textureUnit) const;

        [[nodiscard]] int width() const;

        [[nodiscard]] int height() const;

    private:
        unsigned int frameBuffer_ = 0;
        unsigned int depthTexture_ = 0;

        int width_ = 0;
        int height_ = 0;
    };
}
