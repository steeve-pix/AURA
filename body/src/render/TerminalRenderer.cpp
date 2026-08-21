#include "render/TerminalRenderer.hpp"

#include <iostream>

namespace
{
    // Glyph selection remains local so terminal presentation cannot alter world state.
    char symbolFor(aura::world::CellType cell)
    {
        switch (cell)
        {
        case aura::world::CellType::Empty:
            return '.';
        case aura::world::CellType::Wall:
            return '#';
        case aura::world::CellType::Battery:
            return 'B';
        case aura::world::CellType::Unknown:
            return '?';
        }
        return '$';
    }
}

namespace aura::render
{
    void TerminalRenderer::render(const world::World& world, const agent::Agent agent) const
    {
        const world::Position agentPosition = agent.position();

        for (int y = 0; y < world.height(); ++y)
        {
            for (int x = 0; x < world.width(); ++x)
            {
                if (x == agentPosition.x && y == agentPosition.y)
                {
                    // AURA is an overlay; the underlying cell still matters to simulation logic.
                    std::cout << 'A';
                    continue;
                }

                std::cout << symbolFor(world.cellAt({x, y}));
            }
            std::cout << '\n';
        }
    }
}
