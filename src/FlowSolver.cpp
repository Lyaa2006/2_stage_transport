#include "FlowSolver.h"

#include <lemon/list_graph.h>
#include <lemon/network_simplex.h>

#include <cmath>
#include <numeric>
#include <stdexcept>

using Graph = lemon::ListDigraph;
using FlowValue = long long;
using FlowCost = double;

namespace {
constexpr FlowValue FLOW_SCALE = 100; // keep two decimal places for demands/capacities
FlowValue scaleFlow(double value) {
    return static_cast<FlowValue>(std::llround(value * static_cast<double>(FLOW_SCALE)));
}
} // namespace

FlowSolver::FlowSolver(const ProblemData &data) : data_(data) {
    totalDemand_ = std::accumulate(data_.demand_q.begin(), data_.demand_q.end(), 0.0);
}

FlowResult FlowSolver::solve(const std::vector<uint8_t> &genes) const {
    if (genes.size() != numGenes()) {
        throw std::runtime_error("Gene length does not match problem size");
    }

    const size_t nI = data_.numPlants();
    const size_t nJ = data_.numDepots();
    const size_t nK = data_.numCustomers();

    double plantCap = 0.0;
    double depotCap = 0.0;

    CostBreakdown cost;
    for (size_t i = 0; i < nI; ++i) {
        if (genes[i]) {
            plantCap += data_.capacity_b[i];
            cost.fixedPlant += data_.fixed_cost_f[i];
        }
    }
    for (size_t j = 0; j < nJ; ++j) {
        if (genes[nI + j]) {
            depotCap += data_.capacity_p[j];
            cost.fixedDepot += data_.fixed_cost_g[j];
        }
    }

    if (plantCap + 1e-9 < totalDemand_ || depotCap + 1e-9 < totalDemand_) {
        return {false, cost};
    }

    Graph g;
    Graph::ArcMap<FlowCost> costMap(g);
    Graph::ArcMap<FlowValue> capacityMap(g);
    Graph::NodeMap<FlowValue> supplyMap(g);

    const FlowValue totalDemandInt = scaleFlow(totalDemand_);

    Graph::Node source = g.addNode();
    supplyMap[source] = totalDemandInt;

    std::vector<Graph::Node> plantNodes(nI);
    for (size_t i = 0; i < nI; ++i) {
        plantNodes[i] = g.addNode();
        supplyMap[plantNodes[i]] = 0.0;
    }

    struct DepotNodes { Graph::Node in; Graph::Node out; };
    std::vector<DepotNodes> depotNodes(nJ);
    for (size_t j = 0; j < nJ; ++j) {
        depotNodes[j] = {g.addNode(), g.addNode()};
        supplyMap[depotNodes[j].in] = 0.0;
        supplyMap[depotNodes[j].out] = 0.0;
    }

    std::vector<Graph::Node> customerNodes(nK);
    for (size_t k = 0; k < nK; ++k) {
        customerNodes[k] = g.addNode();
        supplyMap[customerNodes[k]] = -scaleFlow(data_.demand_q[k]);
    }

    const FlowValue INF_CAP = totalDemandInt;

    for (size_t i = 0; i < nI; ++i) {
        if (!genes[i]) {
            continue;
        }
        auto arc = g.addArc(source, plantNodes[i]);
        capacityMap[arc] = scaleFlow(data_.capacity_b[i]);
        costMap[arc] = 0.0;
    }

    for (size_t i = 0; i < nI; ++i) {
        if (!genes[i]) {
            continue;
        }
        for (size_t j = 0; j < nJ; ++j) {
            if (!genes[nI + j]) {
                continue;
            }
            auto arc = g.addArc(plantNodes[i], depotNodes[j].in);
            capacityMap[arc] = INF_CAP;
            costMap[arc] = data_.cost_c[j][i];
        }
    }

    for (size_t j = 0; j < nJ; ++j) {
        if (!genes[nI + j]) {
            continue;
        }
        auto arc = g.addArc(depotNodes[j].in, depotNodes[j].out);
        capacityMap[arc] = scaleFlow(data_.capacity_p[j]);
        costMap[arc] = 0.0;
    }

    for (size_t j = 0; j < nJ; ++j) {
        if (!genes[nI + j]) {
            continue;
        }
        for (size_t k = 0; k < nK; ++k) {
            auto arc = g.addArc(depotNodes[j].out, customerNodes[k]);
            capacityMap[arc] = INF_CAP;
            costMap[arc] = data_.cost_d[k][j];
        }
    }

    lemon::NetworkSimplex<Graph, FlowValue, FlowCost> ns(g);
    ns.upperMap(capacityMap).costMap(costMap).supplyMap(supplyMap);

    auto status = ns.run();
    if (status != lemon::NetworkSimplex<Graph, FlowValue, FlowCost>::OPTIMAL) {
        return {false, cost};
    }

    cost.transport = ns.totalCost() / static_cast<double>(FLOW_SCALE);
    return {true, cost};
}
