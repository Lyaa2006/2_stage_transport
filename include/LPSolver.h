#pragma once

#include "ProblemData.h"

#include <vector>

struct RelaxationSolution {
    std::vector<double> y;
    std::vector<double> z;
};

class LPSolver {
public:
    explicit LPSolver(const ProblemData &data);
    RelaxationSolution solveRelaxation() const;

private:
    const ProblemData &data_;
};
