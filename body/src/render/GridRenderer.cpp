#include "render/GridRenderer.hpp"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cmath>

namespace aura::render {
    void GridRenderer::render(const world::World &world, const agent::Agent &agent, int sensorRadius,
                              const std::vector<world::Position> &path, const world::Position *target, const bridge::BrainDebugState& debug) {
        // OpenGL's visible area spans -1 to 1, so each grid cell consumes an equal
        // fraction of that two-unit width and height.
        const float cellWidth =
                2.0F / static_cast<float>(world.width());

        const float cellHeight =
                2.0F / static_cast<float>(world.height());

        const auto agentPosition = agent.position();

        // Debug collections are vectors from the protocol; membership checks stay local
        // because rendering does not own or reshape brain data.
        const auto containsPosition =
                [](const auto &positions, world::Position position) {
                    for (const auto &item : positions) {
                        if (item == position) {
                            return true;
                        }
                    }

                    return false;
                };

        for (int y = 0; y < world.height(); ++y) {
            for (int x = 0; x < world.width(); ++x) {
                const world::Position position{x, y};

                const auto cell = world.cellAt(position);

                const bool known =
                        containsPosition(debug.knownCells, position);

                const bool visited =
                        containsPosition(debug.visitedCells, position);

                // Sensor coverage is a square Chebyshev footprint centered on AURA.
                const bool insideSensorRange =
                        std::abs(x - agentPosition.x) <= sensorRadius &&
                        std::abs(y - agentPosition.y) <= sensorRadius;

                if (cell == world::CellType::Wall) {
                    glColor3f(0.35F, 0.35F, 0.35F);
                } else if (cell == world::CellType::Battery) {
                    glColor3f(0.2F, 0.8F, 0.2F);
                } else if (cell == world::CellType::Unknown) {
                    glColor3f(1.0F, 0.0F, 0.2F);

                }else {
                    glColor3f(0.12F, 0.12F, 0.15F);
                }

                // World y increases downward, while OpenGL y increases upward.
                const float left =
                        -1.0F + static_cast<float>(x) * cellWidth;

                const float right =
                        left + cellWidth;

                const float top =
                        1.0F - static_cast<float>(y) * cellHeight;

                const float bottom =
                        top - cellHeight;

                glBegin(GL_QUADS);

                glVertex2f(left, top);
                glVertex2f(right, top);
                glVertex2f(right, bottom);
                glVertex2f(left, bottom);

                glEnd();

                // Overlays are drawn from broadest history to most immediate state so
                // visited and currently sensed cells remain visually dominant.
                if (cell == world::CellType::Empty && known) {
                    glColor3f(0.16F, 0.16F, 0.20F);

                    glBegin(GL_QUADS);

                    glVertex2f(left, top);
                    glVertex2f(right, top);
                    glVertex2f(right, bottom);
                    glVertex2f(left, bottom);

                    glEnd();
                }

                if (cell == world::CellType::Empty && visited) {
                    glColor3f(0.23F, 0.23F, 0.29F);

                    glBegin(GL_QUADS);

                    glVertex2f(left, top);
                    glVertex2f(right, top);
                    glVertex2f(right, bottom);
                    glVertex2f(left, bottom);

                    glEnd();
                }

                if (cell == world::CellType::Empty && insideSensorRange) {
                    glColor3f(0.19F, 0.27F, 0.35F);

                    glBegin(GL_QUADS);

                    glVertex2f(left, top);
                    glVertex2f(right, top);
                    glVertex2f(right, bottom);
                    glVertex2f(left, bottom);

                    glEnd();
                }
            }
        }

        // Route and target overlays intentionally cover cell-history shading.
        for (const auto &position: path) {
            const float left =
                    -1.0F + static_cast<float>(position.x) * cellWidth;

            const float right =
                    left + cellWidth;

            const float top =
                    1.0F - static_cast<float>(position.y) * cellHeight;

            const float bottom =
                    top - cellHeight;

            glColor3f(0.55F, 0.35F, 0.75F);

            glBegin(GL_QUADS);

            glVertex2f(left, top);
            glVertex2f(right, top);
            glVertex2f(right, bottom);
            glVertex2f(left, bottom);

            glEnd();
        }

        if (target != nullptr) {
            const float left =
                    -1.0F + static_cast<float>(target->x) * cellWidth;

            const float right =
                    left + cellWidth;

            const float top =
                    1.0F - static_cast<float>(target->y) * cellHeight;

            const float bottom =
                    top - cellHeight;

            glColor3f(1.0F, 0.65F, 0.15F);

            glBegin(GL_QUADS);

            glVertex2f(left, top);
            glVertex2f(right, top);
            glVertex2f(right, bottom);
            glVertex2f(left, bottom);

            glEnd();
        }

        // Grid lines are rendered after cell fills so boundaries remain legible.
        glColor3f(0.18F, 0.18F, 0.20F);

        glBegin(GL_LINES);

        for (int x = 0; x <= world.width(); ++x) {
            const float px =
                    -1.0F + static_cast<float>(x) * cellWidth;

            glVertex2f(px, 1.0F);
            glVertex2f(px, -1.0F);
        }

        for (int y = 0; y <= world.height(); ++y) {
            const float py =
                    1.0F - static_cast<float>(y) * cellHeight;

            glVertex2f(-1.0F, py);
            glVertex2f(1.0F, py);
        }

        glEnd();

        // AURA is the final overlay and therefore cannot be hidden by route highlighting.
        const auto position = agent.position();

        const float left =
                -1.0F + static_cast<float>(position.x) * cellWidth;

        const float right =
                left + cellWidth;

        const float top =
                1.0F - static_cast<float>(position.y) * cellHeight;

        const float bottom =
                top - cellHeight;

        glColor3f(0.2F, 0.4F, 1.0F);

        glBegin(GL_QUADS);

        glVertex2f(left, top);
        glVertex2f(right, top);
        glVertex2f(right, bottom);
        glVertex2f(left, bottom);

        glEnd();
    }
}
