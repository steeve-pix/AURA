#pragma once
#include "Action.hpp"
#include "BrainDebugState.hpp"

namespace aura::bridge{
struct BrainResponse {
    Action action;
    BrainDebugState debug;
};
}
