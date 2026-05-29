#pragma once

#include <string>
#include <vector>

struct ProblemData {
    std::vector<double> capacity_b; // |I|
    std::vector<double> fixed_cost_f; // |I|

    std::vector<double> capacity_p; // |J|
    std::vector<double> fixed_cost_g; // |J|

    std::vector<double> demand_q; // |K|

    std::vector<std::vector<double>> cost_c; // |J| x |I|
    std::vector<std::vector<double>> cost_d; // |K| x |J|

    size_t numPlants() const { return capacity_b.size(); }
    size_t numDepots() const { return capacity_p.size(); }
    size_t numCustomers() const { return demand_q.size(); }
};
