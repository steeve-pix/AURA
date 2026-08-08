#include "render/TerminalRenderer.hpp"

#include <iostream>

namespace {
    // Rendering symbols are presentation-only and never become world state.
    char symbolFor(CellType cell) {
        switch (cell) {
            case CellType::Empty:
                return '.';
            case CellType::Wall:
                return '#';
            case CellType::Battery:
                return 'B';
        }
        return '?';
    }
}

namespace aura::render {
    void TerminalRenderer::render(const world::World &world, const agent::Agent agent) const {
        const world::Position agentPosition = agent.position();

        for (int y = 0; y < world.height(); ++y) {
            for (int x = 0; x < world.width(); ++x) {
                if (x == agentPosition.x && y == agentPosition.y) {
                    // Draw AURA over the underlying cell without modifying that cell.
                    std::cout << 'A';
                    continue;
                }

                std::cout << symbolFor(world.cellAt({x, y}));
            }
            std::cout << '\n';
        }
    }
}
