#pragma once
#include "BodyRenderer.hpp"
#include "Camera.hpp"
#include "DirectionalLight.hpp"
#include "MeshFactory.hpp"
#include "ShadowMap.hpp"
#include "Window.hpp"
#include "../../../src/AssetPath.hpp"
#include "aura/physics/RoomCollision.hpp"

namespace aura::render {
    class AuraViewer {
    public:
        AuraViewer(const physics::RoomBounds &room)
            : window_(1024, 820, "AURA - Physics running"),
              camera_({0.0f, 3.9f, 8.2f}, {0.0f, 3.9f, 0.0f}, 1280.0f / 720.0f),
              shadowMap_(2048, 2048),
              litShader_(Shader::fromFiles(app::assetPath("shaders/basic.vert"),
                                           app::assetPath("shaders/basic.frag"))),
              unlitShader_(Shader::fromFiles(
                  app::assetPath("shaders/unlit.vert"),
                  app::assetPath("shaders/unlit.frag"))),
              shadowShader_(Shader::fromFiles(
                  app::assetPath("shaders/shadow.vert"),
                  app::assetPath("shaders/shadow.frag"))),
              outlineShader_(Shader::fromFiles(
                  app::assetPath("shaders/outline.vert"),
                  app::assetPath("shaders/unlit.frag"))),
              skinShader_(Shader::fromFiles(app::assetPath("shaders/skin.vert"),
                                            app::assetPath("shaders/skin.frag"))),
              roomLines_(MeshFactory::createRoomLines(room.maximum - room.minimum, 5.0f), MeshPrimitive::Lines),
              grid_(MeshFactory::createGrid(60, 1.0f), MeshPrimitive::Lines),
              floor_(MeshFactory::createFloor((room.maximum - room.minimum).x), MeshPrimitive::Triangles,
                     VertexLayout::PositionNormal),
              light_({4.0f, 8.0f, 4.0f}, {0.0f, 0.0f, 0.0f}) {
            camera_.setOrbit(0.55f, 0.25f);

            const auto roomSize = room.maximum - room.minimum;

            roomOrigin_ =
                    (room.minimum + room.maximum) * 0.5f - math::Vec3{0.0f, roomSize.y * 0.5f, 0.0f};
        }

        Window &window() {
            return window_;
        }

        Camera &camera() {
            return camera_;
        }

        ShadowMap &shadowMap() {
            return shadowMap_;
        }

        BodyRenderer &bodyRenderer() {
            return bodyRenderer_;
        }

        Shader &litShader() {
            return litShader_;
        }

        Shader &unlitShader() {
            return unlitShader_;
        }

        Shader &shadowShader() {
            return shadowShader_;
        }

        Shader &outlineShader() {
            return outlineShader_;
        }

        Shader &skinShader() {
            return skinShader_;
        }

        Mesh &floor() {
            return floor_;
        }

        Mesh &grid() {
            return grid_;
        }

        Mesh &roomLines() {
            return roomLines_;
        }

        DirectionalLight &light() {
            return light_;
        }

        [[nodiscard]] const math::Vec3 &roomOrigin() const {
            return roomOrigin_;
        }

        void render(const body::AuraBody3D &body, const std::vector<JointMarker> &jointMarkers);

    private:
        Window window_;
        Camera camera_;
        ShadowMap shadowMap_;
        BodyRenderer bodyRenderer_;

        Shader litShader_;
        Shader unlitShader_;
        Shader shadowShader_;
        Shader outlineShader_;
        Shader skinShader_;

        Mesh floor_;
        Mesh grid_;
        Mesh roomLines_;

        DirectionalLight light_;
        math::Vec3 roomOrigin_;
    };
}
