#include "DataLoader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::vector<std::string> splitCsvLine(const std::string &line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        tokens.push_back(item);
    }
    return tokens;
}

std::string joinPath(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + file;
    }
    return dir + "/" + file;
}

void readVectorCsv(const std::string &filePath,
                   std::vector<double> &capacity,
                   std::vector<double> &fixedCost,
                   const std::string &capCol,
                   const std::string &fixCol) {
    std::ifstream in(filePath);
    if (!in) {
        throw std::runtime_error("Failed to open CSV: " + filePath);
    }

    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error("Empty CSV: " + filePath);
    }

    auto headers = splitCsvLine(line);
    int capIndex = -1;
    int fixIndex = -1;
    for (size_t col = 0; col < headers.size(); ++col) {
        if (headers[col] == capCol) {
            capIndex = static_cast<int>(col);
        } else if (headers[col] == fixCol) {
            fixIndex = static_cast<int>(col);
        }
    }
    if (capIndex == -1 || fixIndex == -1) {
        throw std::runtime_error("Missing columns in CSV: " + filePath);
    }

    capacity.clear();
    fixedCost.clear();

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto tokens = splitCsvLine(line);
        if (tokens.size() <= static_cast<size_t>(std::max(capIndex, fixIndex))) {
            throw std::runtime_error("Malformed row in CSV: " + filePath);
        }
        capacity.push_back(std::stod(tokens[capIndex]));
        fixedCost.push_back(std::stod(tokens[fixIndex]));
    }
}

void readDemandCsv(const std::string &filePath,
                   std::vector<double> &demand,
                   const std::string &demandCol) {
    std::ifstream in(filePath);
    if (!in) {
        throw std::runtime_error("Failed to open CSV: " + filePath);
    }

    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error("Empty CSV: " + filePath);
    }

    auto headers = splitCsvLine(line);
    int demandIndex = -1;
    for (size_t col = 0; col < headers.size(); ++col) {
        if (headers[col] == demandCol) {
            demandIndex = static_cast<int>(col);
            break;
        }
    }
    if (demandIndex == -1) {
        throw std::runtime_error("Missing demand column in CSV: " + filePath);
    }

    demand.clear();
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto tokens = splitCsvLine(line);
        if (tokens.size() <= static_cast<size_t>(demandIndex)) {
            throw std::runtime_error("Malformed row in CSV: " + filePath);
        }
        demand.push_back(std::stod(tokens[demandIndex]));
    }
}

std::vector<std::vector<double>> readMatrixCsv(const std::string &filePath) {
    std::ifstream in(filePath);
    if (!in) {
        throw std::runtime_error("Failed to open CSV: " + filePath);
    }

    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error("Empty CSV: " + filePath);
    }

    auto headers = splitCsvLine(line);
    if (headers.size() < 2) {
        throw std::runtime_error("Matrix CSV missing headers: " + filePath);
    }

    std::vector<std::vector<double>> matrix;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto tokens = splitCsvLine(line);
        if (tokens.size() < 2) {
            throw std::runtime_error("Malformed matrix row in CSV: " + filePath);
        }
        std::vector<double> row;
        row.reserve(tokens.size() - 1);
        for (size_t i = 1; i < tokens.size(); ++i) {
            row.push_back(std::stod(tokens[i]));
        }
        matrix.push_back(std::move(row));
    }

    return matrix;
}
} // namespace

ProblemData DataLoader::loadFromCsvDir(const std::string &dirPath) const {
    ProblemData data;

    readVectorCsv(joinPath(dirPath, "I.csv"), data.capacity_b, data.fixed_cost_f, "Capacity_b", "FixedCost_f");
    readVectorCsv(joinPath(dirPath, "J.csv"), data.capacity_p, data.fixed_cost_g, "Capacity_p", "FixedCost_g");
    readDemandCsv(joinPath(dirPath, "K.csv"), data.demand_q, "Demand_q");
    data.cost_c = readMatrixCsv(joinPath(dirPath, "C.csv"));
    data.cost_d = readMatrixCsv(joinPath(dirPath, "D.csv"));

    if (data.cost_c.size() != data.capacity_p.size()) {
        throw std::runtime_error("C.csv row count does not match J size");
    }
    if (!data.cost_c.empty() && data.cost_c.front().size() != data.capacity_b.size()) {
        throw std::runtime_error("C.csv col count does not match I size");
    }
    if (data.cost_d.size() != data.demand_q.size()) {
        throw std::runtime_error("D.csv row count does not match K size");
    }
    if (!data.cost_d.empty() && data.cost_d.front().size() != data.capacity_p.size()) {
        throw std::runtime_error("D.csv col count does not match J size");
    }

    return data;
}
