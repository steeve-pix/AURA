#include "AngularCOMProjectionReplay.hpp"
#include "AngularCOMRecenterReplay.hpp"
#include <fstream>
#include <sstream>
#include <set>

int main(int argc, char **argv) {
    using namespace aura;
    const bool recenter = argc == 3 && std::string(argv[2]) == "--recenter";
    if (argc != 2 && !recenter)return 2;
    auto before = body::createAuraBody3D(), after = before;
    const auto skeleton = body::createAuraSkeleton3D(before);
    std::ifstream file(argv[1]);
    std::string line, jointName;
    std::set<std::string> loaded;
    while (std::getline(file, line)) {
        if (line.starts_with("angularCOMWorst,")) {
            std::cout << line << '\n';
            const auto pos = line.find(",joint=");
            if (pos == std::string::npos)return 2;
            const auto end = line.find(',', pos + 7);
            jointName = line.substr(pos + 7, end - pos - 7);
            continue;
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string tag, state, name;
        row >> tag >> state >> name;
        if (tag != "angularCOMInput" || (state != "before" && state != "after") || !loaded.insert(state + name).second)
            return 2;
        auto parts = joint_com_replay::parts(state == "before" ? before : after);
        auto it = std::find_if(parts.begin(), parts.end(), [&](auto *p) { return p->name == name; });
        if (it == parts.end())return 2;
        auto &p = **it;
        auto &b = p.body;
        if (!(row >> p.size.x >> p.size.y >> p.size.z >> b.mass >> b.position.x >> b.position.y >> b.position.z
              >> b.orientation.w >> b.orientation.x >> b.orientation.y >> b.orientation.z
              >> b.velocity.x >> b.velocity.y >> b.velocity.z >> b.angularVelocity.x >> b.angularVelocity.y >> b.
              angularVelocity.z
              >> b.momentOfInertia.x >> b.momentOfInertia.y >> b.momentOfInertia.z))
            return 2;
    }
    if (loaded.size() != 32)return 2;
    const auto edges = angular_com_replay::edges(skeleton);
    const auto cut = std::find_if(edges.begin(), edges.end(), [&](const auto &e) { return e.name == jointName; });
    if (cut == edges.end())return 2;
    if (recenter)return angular_recenter_replay::report(before, after, skeleton, *cut) ? 0 : 1;
    return angular_com_replay::report(before, after, skeleton, *cut) ? 0 : 1;
}
