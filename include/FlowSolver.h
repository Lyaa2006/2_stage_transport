#pragma once

#include "ProblemData.h"

#include <vector>

struct CostBreakdown {
    double fixedPlant = 0.0;
    double fixedDepot = 0.0;
    double transport = 0.0;
    double total() const { return fixedPlant + fixedDepot + transport; }
};

struct FlowResult {
    bool feasible = false;
    CostBreakdown cost;
};

class FlowSolver {
public:
    explicit FlowSolver(const ProblemData &data);

    FlowResult solve(const std::vector<uint8_t> &genes) const;
    size_t numGenes() const { return data_.numPlants() + data_.numDepots(); }

private:
    const ProblemData &data_;
    double totalDemand_ = 0.0;
};
