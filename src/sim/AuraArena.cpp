#include "aura/sim/AuraArena.hpp"
#include "aura/physics/CollisionShape.hpp"
#include "../app/SimulationStep.hpp"
#include <cmath>
#include <future>
#include <stdexcept>

namespace aura::sim {
    AuraArena::AuraArena(std::size_t count, bool parallel, float spacing) : parallel_(parallel), spacing_(spacing) {
        if (count < 1 || count > 4) throw std::invalid_argument("Arena supports 1–4 agents");
        for (std::size_t i = 0; i < count; ++i) agents_.push_back(std::make_unique<AuraSimulation>());
        if (!std::isfinite(spacing) || spacing < 6.f || spacing > 30.f)
            throw std::invalid_argument("Arena spacing must be between 6 and 30 metres");
        reset();
    }

    void AuraArena::reset(std::size_t count) {
        if (count == 0) count = size();
        if (count > size()) throw std::invalid_argument("Too many active arena agents");
        active_.assign(size(), false);
        contacts_ = 0;
        const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(double(size()))));
        const auto rows = (size() + columns - 1) / columns;
        for (std::size_t i = 0; i < size(); ++i) {
            agents_[i]->reset();
            active_[i] = i < count;
            const math::Vec3 origin{float((double(i % columns) - double(columns - 1) / 2) * spacing_), 0,
                                    float((double(i / columns) - double(rows - 1) / 2) * spacing_)};
            for (auto *part : agents_[i]->getParts()) part->body.position += origin;
        }
    }

    void AuraArena::step(const std::vector<AuraAction> &actions) {
        if (actions.size() != size()) throw std::invalid_argument("One action per arena slot required");
        if (std::none_of(active_.begin(), active_.end(), [](bool x) { return x; }))
            throw std::logic_error("Arena wave ended; reset before stepping");
        std::vector<std::future<void>> jobs;
        for (std::size_t i = 0; i < size(); ++i) if (active_[i]) {
            agents_[i]->setAction(actions[i]);
            if (parallel_) jobs.push_back(std::async(std::launch::async, [this, i] { agents_[i]->stepArenaPhysics(); }));
            else agents_[i]->stepArenaPhysics();
        }
        for (auto &job : jobs) job.get();
        resolveContacts();
        for (std::size_t i = 0; i < size(); ++i) if (active_[i]) {
            agents_[i]->classifyArenaTick();
            if (agents_[i]->isDone()) active_[i] = false;
        }
    }

    void AuraArena::resolveContacts() {
        contacts_ = 0;
        // Experimental whole-agent pose projection preserves every internal
        // anchor/relative angle. Contact velocities use existing local physical
        // impulses, never copied descendant velocities. This is not a general
        // articulated multi-body contact solver; room projection may reopen contact.
        for (int pass = 0; pass < 8; ++pass) {
            bool corrected = false;
            for (std::size_t a = 0; a < size(); ++a) for (std::size_t b = a + 1; b < size(); ++b) {
                if (!active_[a] || !active_[b]) continue;
                auto &first = *agents_[a]; auto &second = *agents_[b];
                const auto bounds = [](AuraSimulation &simulation) {
                    math::Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
                    for (auto *part : simulation.getParts()) {
                        const float r = part->size.length() * .5f;
                        const auto p = part->body.position;
                        low = {std::min(low.x, p.x-r), std::min(low.y, p.y-r), std::min(low.z, p.z-r)};
                        high = {std::max(high.x, p.x+r), std::max(high.y, p.y+r), std::max(high.z, p.z+r)};
                    }
                    return std::pair{low, high};
                };
                const auto [loA, hiA] = bounds(first);
                const auto [loB, hiB] = bounds(second);
                if (loA.x > hiB.x || loB.x > hiA.x || loA.y > hiB.y || loB.y > hiA.y ||
                    loA.z > hiB.z || loB.z > hiA.z) continue;
                physics::BodyContact deepest;
                float massA = 0, massB = 0;
                for (auto *part : first.getParts()) massA += part->body.mass;
                for (auto *part : second.getParts()) massB += part->body.mass;
                for (auto *pa : first.getParts()) for (auto *pb : second.getParts()) {
                    const auto contact = physics::detectBodyContact(pa->body, pa->size, pa->collisionShape,
                                                                   pb->body, pb->size, pb->collisionShape);
                    if (!contact.overlapping) continue;
                    if (pass == 0) ++contacts_;
                    physics::resolveBodyContactVelocity(pa->body, pb->body, contact);
                    if (contact.penetration > deepest.penetration) deepest = contact;
                }
                if (deepest.penetration <= 1e-4f) continue;
                const auto delta = deepest.normal * (deepest.penetration - 1e-4f);
                for (auto *part : first.getParts()) part->body.position -= delta * (massB / (massA + massB));
                for (auto *part : second.getParts()) part->body.position += delta * (massA / (massA + massB));
                physics::resolveRoomCollisions(first.getRoomBodies(), first.getRoom());
                physics::resolveRoomCollisions(second.getRoomBodies(), second.getRoom());
                corrected = true;
            }
            if (!corrected) break;
        }
    }
}
