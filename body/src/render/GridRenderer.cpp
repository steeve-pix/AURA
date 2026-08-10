#include "render/GridRenderer.hpp"

#include <GLFW/glfw3.h>

namespace aura::render {
    void GridRenderer::render(
        const world::World &world,
        const agent::Agent &agent
    ) {
        const float cellWidth =
                2.0F / static_cast<float>(world.width());

        const float cellHeight =
                2.0F / static_cast<float>(world.height());

        for (int y = 0; y < world.height(); ++y) {
            for (int x = 0; x < world.width(); ++x) {
                const auto cell =
                        world.cellAt({x, y});

                if (cell == aura::world::CellType::Wall) {
                    glColor3f(0.35F, 0.35F, 0.35F);
                } else if (cell == aura::world::CellType::Battery) {
                    glColor3f(0.2F, 0.8F, 0.2F);
                } else {
                    glColor3f(0.12F, 0.12F, 0.15F);
                }

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
            }
        }

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
