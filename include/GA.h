#pragma once

#include "FlowSolver.h"
#include "LPSolver.h"
#include "ProblemData.h"

#include <random>
#include <vector>

struct Chromosome {
    std::vector<uint8_t> genes;
    double fitness = 0.0;
    CostBreakdown cost;
};

struct GAConfig {
    size_t populationSize = 50;
    size_t maxGenerations = 1000;
    double mutationRate = 0.03;
    size_t localSearchInterval = 50;
    size_t tournamentSize = 3;
    size_t earlyStopPatience = 200; // stop if no improvement for N generations
};

class GeneticAlgorithm {
public:
    GeneticAlgorithm(const ProblemData &data, const GAConfig &config, std::ostream *logStream = nullptr);

    Chromosome run();

private:
    Chromosome evaluate(Chromosome chrom) const;
    std::vector<Chromosome> initializePopulation();
    Chromosome generateCH1Individual();
    Chromosome generateCH2Seed();

    Chromosome tournamentSelect(const std::vector<Chromosome> &population);
    std::pair<Chromosome, Chromosome> crossover(const Chromosome &a, const Chromosome &b);
    void mutate(Chromosome &chrom);

    bool localSearch(Chromosome &chrom);
    bool localSearch1Flip(Chromosome &chrom);
    bool localSearchSwap(Chromosome &chrom);

    const ProblemData &data_;
    GAConfig config_;
    FlowSolver flowSolver_;
    LPSolver lpSolver_;
    mutable std::mt19937 rng_;
    std::ostream *logStream_ = nullptr;
};
