#pragma once
#include "ui/BalanceReadout.hpp"
#include <vector>

namespace aura::cli {
    // Summaries retained by Model Logs; progress/tensor events stay out of history.
    inline std::string balance_activity(const std::vector<std::string> &fields) {
        using namespace screen;
        if (fields.size() < 2 || (fields[0] != "AURA_NN" && fields[0] != "AURA_EVAL")) return {};
        const auto &kind = fields[1];
        if (kind == "objective_contract" && fields.size() == 7 && fields[2] == "balance_quality" &&
            fields[3] == "3" && fields[4] == "balance_v2" && fields[5] == "2" && fields[6] == "actor_critic_mc")
            return "Balance System: quality reward / FALLEN termination / paired actor–critic";
        if (kind == "schedule" && fields.size() == 5) {
            return "Training schedule: " + fields[2] + " episodes per batch / " + fields[3] +
                   " consecutive batches / " + fields[4] + " agents";
        }
        if (kind == "critic_update" && fields.size() == 3) {
            std::istringstream value(fields[2]);
            double mse;
            char extra;
            if (value >> mse && !(value >> extra) && std::isfinite(mse) && mse >= 0)
                return "Critic update / MSE " + number(mse, 6) + " / target G divided by 100";
        }
        if (kind == "balance" && fields.size() == 12) {
            const auto &label = fields[2];
            const std::string name = label == "training" ? "Training balance" :
                                     label == "deterministic" ? "Deterministic balance" :
                                     label == "stochastic" ? "Stochastic balance" :
                                     label == "best" ? "Best balance" : "";
            if (name.empty()) return {};
            std::ostringstream joined;
            for (std::size_t i = 3; i < fields.size(); ++i) joined << fields[i] << ' ';
            std::istringstream input(joined.str());
            BalanceReadout metrics;
            if (metrics.read(input)) return name + ": " + metrics.duration() + "\n" + name + ": " + metrics.outcome();
        }
        return {};
    }
}
