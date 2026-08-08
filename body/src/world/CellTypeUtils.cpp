#include "world/CellTypeUtils.hpp"

namespace aura::world {
    std::string_view toString(CellType type) {
        switch (type) {
            case CellType::Empty:
                return "Empty";
            case CellType::Wall:
                return "Wall";
            case CellType::Battery:
                return "Battery";
        }

        return "Unknown";
    }
}
