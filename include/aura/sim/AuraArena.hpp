#pragma once
#include "AuraSimulation.hpp"
#include <memory>

namespace aura::sim {
    // Explicit coupled task. Each agent retains its own classifier and horizon.
    class AuraArena {
    public:
        explicit AuraArena(std::size_t agents = 4, bool parallel = true, float spacing = 12.0f);
        void reset(std::size_t activeAgents = 0);
        void step(const std::vector<AuraAction> &actions);
        void resolveContacts();
        AuraSimulation &agent(std::size_t index) { return *agents_.at(index); }
        std::size_t size() const { return agents_.size(); }
        bool active(std::size_t index) const { return active_.at(index); }
        float spacing() const { return spacing_; }
        std::size_t contacts() const { return contacts_; }
        const physics::RoomBounds &room() const { return agents_.front()->getRoom(); }
    private:
        std::vector<std::unique_ptr<AuraSimulation>> agents_;
        std::vector<bool> active_;
        bool parallel_;
        float spacing_;
        std::size_t contacts_ = 0;
    };
}
