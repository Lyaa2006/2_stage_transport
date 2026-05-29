#include "GA.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

namespace {
constexpr double PENALTY_COST = 1e12;
}

GeneticAlgorithm::GeneticAlgorithm(const ProblemData &data, const GAConfig &config, std::ostream *logStream)
    : data_(data), config_(config), flowSolver_(data), lpSolver_(data), logStream_(logStream) {
    std::random_device rd;
    rng_ = std::mt19937(rd());
}

Chromosome GeneticAlgorithm::evaluate(Chromosome chrom) const {
    auto result = flowSolver_.solve(chrom.genes);
    if (!result.feasible) {
        chrom.fitness = PENALTY_COST + result.cost.fixedPlant + result.cost.fixedDepot;
        chrom.cost = result.cost;
    } else {
        chrom.cost = result.cost;
        chrom.fitness = chrom.cost.total();
    }
    return chrom;
}

std::vector<Chromosome> GeneticAlgorithm::initializePopulation() {
    std::vector<Chromosome> population;
    population.reserve(config_.populationSize);

    Chromosome allOpen;
    allOpen.genes.assign(flowSolver_.numGenes(), 1);
    population.push_back(allOpen);
    population.push_back(generateCH2Seed());
    while (population.size() < config_.populationSize) {
        population.push_back(generateCH1Individual());
    }

    for (auto &chrom : population) {
        chrom = evaluate(chrom);
    }
    return population;
}

Chromosome GeneticAlgorithm::generateCH1Individual() {
    const size_t nI = data_.numPlants();
    const size_t nJ = data_.numDepots();

    Chromosome chrom;
    chrom.genes.assign(nI + nJ, 0);

    std::vector<double> bcp(nI, 0.0);
    for (size_t i = 0; i < nI; ++i) {
        double sumC = 0.0;
        for (size_t j = 0; j < nJ; ++j) {
            sumC += data_.cost_c[j][i];
        }
        bcp[i] = (data_.fixed_cost_f[i] + sumC) / data_.capacity_b[i];
    }

    double maxScore = *std::max_element(bcp.begin(), bcp.end());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    double totalDemand = std::accumulate(data_.demand_q.begin(), data_.demand_q.end(), 0.0);
    double currentCap = 0.0;

    std::vector<size_t> plantOrder(nI);
    std::iota(plantOrder.begin(), plantOrder.end(), 0);
    std::sort(plantOrder.begin(), plantOrder.end(), [&](size_t a, size_t b) { return bcp[a] < bcp[b]; });

    for (size_t idx = 0; idx < nI; ++idx) {
        size_t i = plantOrder[idx];
        double prob = 1.0 - bcp[i] / (maxScore + 1e-6);
        if (dist(rng_) < prob) {
            chrom.genes[i] = 1;
            currentCap += data_.capacity_b[i];
        }
    }
    for (size_t idx = 0; idx < nI && currentCap < totalDemand; ++idx) {
        size_t i = plantOrder[idx];
        if (!chrom.genes[i]) {
            chrom.genes[i] = 1;
            currentCap += data_.capacity_b[i];
        }
    }

    std::vector<double> bcs(nJ, 0.0);
    for (size_t j = 0; j < nJ; ++j) {
        double sumC = 0.0;
        for (size_t i = 0; i < nI; ++i) {
            if (chrom.genes[i]) {
                sumC += data_.cost_c[j][i];
            }
        }
        double sumD = std::accumulate(data_.cost_d.begin(), data_.cost_d.end(), 0.0,
                                      [j](double acc, const std::vector<double> &row) {
                                          return acc + row[j];
                                      });
        bcs[j] = (sumC + data_.fixed_cost_g[j] + sumD) / data_.capacity_p[j];
    }

    double maxDepot = *std::max_element(bcs.begin(), bcs.end());
    double depotCap = 0.0;

    std::vector<size_t> depotOrder(nJ);
    std::iota(depotOrder.begin(), depotOrder.end(), 0);
    std::sort(depotOrder.begin(), depotOrder.end(), [&](size_t a, size_t b) { return bcs[a] < bcs[b]; });

    for (size_t idx = 0; idx < nJ; ++idx) {
        size_t j = depotOrder[idx];
        double prob = 1.0 - bcs[j] / (maxDepot + 1e-6);
        if (dist(rng_) < prob) {
            chrom.genes[nI + j] = 1;
            depotCap += data_.capacity_p[j];
        }
    }
    for (size_t idx = 0; idx < nJ && depotCap < totalDemand; ++idx) {
        size_t j = depotOrder[idx];
        if (!chrom.genes[nI + j]) {
            chrom.genes[nI + j] = 1;
            depotCap += data_.capacity_p[j];
        }
    }

    return chrom;
}

Chromosome GeneticAlgorithm::generateCH2Seed() {
    const size_t nI = data_.numPlants();
    const size_t nJ = data_.numDepots();

    Chromosome chrom;
    chrom.genes.assign(nI + nJ, 0);

    RelaxationSolution relax = lpSolver_.solveRelaxation();
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (size_t i = 0; i < nI; ++i) {
        if (relax.y[i] >= 0.7) {
            chrom.genes[i] = 1;
        } else if (relax.y[i] <= 0.3) {
            chrom.genes[i] = 0;
        } else {
            chrom.genes[i] = dist(rng_) < relax.y[i] ? 1 : 0;
        }
    }

    for (size_t j = 0; j < nJ; ++j) {
        if (relax.z[j] >= 0.7) {
            chrom.genes[nI + j] = 1;
        } else if (relax.z[j] <= 0.3) {
            chrom.genes[nI + j] = 0;
        } else {
            chrom.genes[nI + j] = dist(rng_) < relax.z[j] ? 1 : 0;
        }
    }

    return chrom;
}

Chromosome GeneticAlgorithm::tournamentSelect(const std::vector<Chromosome> &population) {
    std::uniform_int_distribution<size_t> dist(0, population.size() - 1);
    Chromosome best = population[dist(rng_)];
    for (size_t i = 1; i < config_.tournamentSize; ++i) {
        Chromosome candidate = population[dist(rng_)];
        if (candidate.fitness < best.fitness) {
            best = candidate;
        }
    }
    return best;
}

std::pair<Chromosome, Chromosome> GeneticAlgorithm::crossover(const Chromosome &a, const Chromosome &b) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    Chromosome child1 = a;
    Chromosome child2 = b;

    for (size_t idx = 0; idx < a.genes.size(); ++idx) {
        if (dist(rng_) < 0.5) {
            child1.genes[idx] = b.genes[idx];
            child2.genes[idx] = a.genes[idx];
        }
    }
    return {child1, child2};
}

void GeneticAlgorithm::mutate(Chromosome &chrom) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto &gene : chrom.genes) {
        if (dist(rng_) < config_.mutationRate) {
            gene = gene ? 0 : 1;
        }
    }
}

bool GeneticAlgorithm::localSearch(Chromosome &chrom) {
    bool improved = false;
    if (localSearch1Flip(chrom)) {
        improved = true;
    }
    if (localSearchSwap(chrom)) {
        improved = true;
    }
    return improved;
}

bool GeneticAlgorithm::localSearch1Flip(Chromosome &chrom) {
    Chromosome best = chrom;
    for (size_t idx = 0; idx < chrom.genes.size(); ++idx) {
        Chromosome candidate = chrom;
        candidate.genes[idx] = candidate.genes[idx] ? 0 : 1;
        candidate = evaluate(candidate);
        if (candidate.fitness + 1e-6 < best.fitness) {
            chrom = candidate;
            return true;
        }
    }
    return false;
}

bool GeneticAlgorithm::localSearchSwap(Chromosome &chrom) {
    const size_t nI = data_.numPlants();
    const size_t nJ = data_.numDepots();

    auto trySwap = [&](size_t offset, size_t count) -> bool {
        for (size_t i = 0; i < count; ++i) {
            if (!chrom.genes[offset + i]) {
                continue;
            }
            for (size_t j = 0; j < count; ++j) {
                if (chrom.genes[offset + j]) {
                    continue;
                }
                Chromosome candidate = chrom;
                candidate.genes[offset + i] = 0;
                candidate.genes[offset + j] = 1;
                candidate = evaluate(candidate);
                if (candidate.fitness + 1e-6 < chrom.fitness) {
                    chrom = candidate;
                    return true;
                }
            }
        }
        return false;
    };

    if (trySwap(0, nI)) {
        return true;
    }
    if (trySwap(nI, nJ)) {
        return true;
    }
    return false;
}

Chromosome GeneticAlgorithm::run() {
    auto population = initializePopulation();
    size_t feasibleCount = 0;
    for (const auto &chrom : population) {
        if (chrom.fitness < PENALTY_COST) {
            ++feasibleCount;
        }
    }
    if (logStream_) {
        (*logStream_) << "Initial feasible solutions: " << feasibleCount << "/" << population.size() << "\n";
    }
    std::cout << "Initial feasible solutions: " << feasibleCount << "/" << population.size() << "\n";
    auto best = *std::min_element(population.begin(), population.end(),
                                 [](const Chromosome &a, const Chromosome &b) { return a.fitness < b.fitness; });
    size_t noImprove = 0;

    for (size_t gen = 1; gen <= config_.maxGenerations; ++gen) {
        std::vector<Chromosome> next;
        next.reserve(config_.populationSize);
        next.push_back(best); // elitism

        while (next.size() < config_.populationSize) {
            auto parent1 = tournamentSelect(population);
            auto parent2 = tournamentSelect(population);
            auto [child1, child2] = crossover(parent1, parent2);
            mutate(child1);
            mutate(child2);
            child1 = evaluate(child1);
            child2 = evaluate(child2);
            next.push_back(child1);
            if (next.size() < config_.populationSize) {
                next.push_back(child2);
            }
        }

        population = std::move(next);
        auto genBest = *std::min_element(population.begin(), population.end(),
                                         [](const Chromosome &a, const Chromosome &b) { return a.fitness < b.fitness; });

        bool newBest = false;
        if (genBest.fitness < best.fitness) {
            best = genBest;
            newBest = true;
            noImprove = 0;
        } else {
            ++noImprove;
        }

        if (gen % 10 == 0 || newBest) {
            std::cout << "[Gen " << gen << "] Best fitness: " << best.fitness << "\n";
            if (logStream_) {
                (*logStream_) << "[Gen " << gen << "] Best fitness: " << best.fitness << "\n";
            }
        }

        if (gen % config_.localSearchInterval == 0 || newBest) {
            auto improved = localSearch(best);
            if (improved) {
                best = evaluate(best);
            }
        }

        if (config_.earlyStopPatience > 0 && noImprove >= config_.earlyStopPatience) {
            if (logStream_) {
                (*logStream_) << "Early stop triggered at generation " << gen
                              << " (no improvement for " << noImprove << " generations)\n";
            }
            std::cout << "Early stop triggered at generation " << gen
                      << " (no improvement for " << noImprove << " generations)\n";
            break;
        }
    }

    return best;
}
