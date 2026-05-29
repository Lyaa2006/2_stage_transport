#include "DataLoader.h"
#include "GA.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char **argv) {
    std::string dirPath = "data/csv_100200400";
    std::string logPath = "run.log";
    if (argc > 1) {
        dirPath = argv[1];
    }
    if (argc > 2) {
        logPath = argv[2];
    }

    try {
    DataLoader loader;
    ProblemData data = loader.loadFromCsvDir(dirPath);

    GAConfig config;
        config.populationSize = 50;
        config.maxGenerations = 1000;
        config.mutationRate = 0.03;
        config.localSearchInterval = 50;
        config.tournamentSize = 3;
    config.earlyStopPatience = 0;

    std::ofstream logFile(logPath);
    if (!logFile) {
        throw std::runtime_error("Failed to open log file: " + logPath);
    }
    logFile << std::fixed << std::setprecision(4);
    std::cout << std::fixed << std::setprecision(4);
    logFile << "TSCFLP GA Run\n";
    logFile << "Data dir: " << dirPath << "\n";
    logFile << "Population: " << config.populationSize << ", MaxGen: " << config.maxGenerations
        << ", Mutation: " << config.mutationRate << ", LS interval: " << config.localSearchInterval
        << ", EarlyStopPatience: " << config.earlyStopPatience << "\n\n";

        auto start = std::chrono::steady_clock::now();
    GeneticAlgorithm ga(data, config, &logFile);
        Chromosome best = ga.run();
        auto end = std::chrono::steady_clock::now();

        std::chrono::duration<double> elapsed = end - start;

        std::cout << "\n=== Final Result ===\n";
        std::cout << "Best total cost: " << best.fitness << "\n";
        std::cout << "  Fixed plant cost: " << best.cost.fixedPlant << "\n";
        std::cout << "  Fixed depot cost: " << best.cost.fixedDepot << "\n";
        std::cout << "  Transport cost: " << best.cost.transport << "\n";
        std::cout << "Elapsed time: " << elapsed.count() << " s\n";

    logFile << "\n=== Final Result ===\n";
    logFile << "Best total cost: " << best.fitness << "\n";
    logFile << "  Fixed plant cost: " << best.cost.fixedPlant << "\n";
    logFile << "  Fixed depot cost: " << best.cost.fixedDepot << "\n";
    logFile << "  Transport cost: " << best.cost.transport << "\n";
    logFile << "Elapsed time: " << elapsed.count() << " s\n";

        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
