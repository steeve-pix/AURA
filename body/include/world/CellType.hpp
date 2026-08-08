#pragma once

/// Physical contents a grid cell can hold.
///
/// This type belongs to the C++ body because it describes simulated world state.
enum class CellType {
    Empty,
    Wall,
    Battery
};
