#include "aura/render/AuraViewer.hpp"
#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

namespace aura::render {
    void AuraViewer::render(const body::AuraBody3D &body, const std::vector<JointMarker> &jointMarkers) {
        // The standalone app used to enable depth testing in main.cpp. Keep this
        // render requirement with the viewer so Python and native callers match.
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        shadowMap_.bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);

        shadowShader_.use();
        shadowShader_.setMat4("uLightSpaceMatrix", light_.lightSpaceMatrix());
        shadowShader_.setMat4("uModel", math::Mat4::identity());
        floor_.draw();
        bodyRenderer_.draw(body, jointMarkers, shadowShader_, false);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        const int width = window_.framebufferWidth();
        const int height = window_.framebufferHeight();
        glViewport(0, 0, width, height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (width > 0 && height > 0) {
            camera_.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));
        }

        const auto view = camera_.viewMatrix();
        const auto projection = camera_.projectionMatrix();

        litShader_.use();
        shadowMap_.bindTexture(0);
        litShader_.setInt("uShadowMap", 0);
        litShader_.setMat4("uView", view);
        litShader_.setMat4("uProjection", projection);
        litShader_.setMat4("uLightSpaceMatrix", light_.lightSpaceMatrix());
        litShader_.setVec3("uLightDirection", {-0.5f, -1.0f, -0.3f});

        // Keep the floor visible from above and the grid visible from below.
        if (camera_.position().y >= 0.0f) {
            litShader_.setVec3("uColor", {0.015f, 0.015f, 0.015f});
            litShader_.setMat4("uModel", math::Mat4::identity());
            floor_.draw();
        }

        unlitShader_.use();
        unlitShader_.setMat4("uView", view);
        unlitShader_.setMat4("uProjection", projection);
        unlitShader_.setVec3("uColor", {0.38f, 0.38f, 0.4f});
        unlitShader_.setMat4("uModel", math::Mat4::translation({0.0f, 0.02f, 0.0f}));
        grid_.draw();
        unlitShader_.setVec3("uColor", {0.23f, 0.38f, 0.46f});
        unlitShader_.setMat4("uModel", math::Mat4::translation(roomOrigin_));
        roomLines_.draw();

        outlineShader_.use();
        outlineShader_.setMat4("uView", view);
        outlineShader_.setMat4("uProjection", projection);
        outlineShader_.setVec3("uColor", {0.12f, 0.13f, 0.15f});
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        bodyRenderer_.draw(body, jointMarkers, outlineShader_, false);
        glDisable(GL_CULL_FACE);

        skinShader_.use();
        skinShader_.setMat4("uView", view);
        skinShader_.setMat4("uProjection", projection);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 2.0f);
        bodyRenderer_.draw(body, jointMarkers, skinShader_, true);
        glDisable(GL_POLYGON_OFFSET_FILL);

        unlitShader_.use();
        unlitShader_.setVec3("uColor", {0.18f, 0.19f, 0.22f});
        bodyRenderer_.draw(body, jointMarkers, unlitShader_, false, true);

        window_.swapBuffers();
    }
}
