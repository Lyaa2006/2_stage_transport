#include "LPSolver.h"

#include <highs/Highs.h>

#include <numeric>
#include <stdexcept>

LPSolver::LPSolver(const ProblemData &data) : data_(data) {}

RelaxationSolution LPSolver::solveRelaxation() const {
    const size_t nI = data_.numPlants();
    const size_t nJ = data_.numDepots();
    const size_t nK = data_.numCustomers();

    const size_t numY = nI;
    const size_t numZ = nJ;
    const size_t numX = nI * nJ;
    const size_t numS = nJ * nK;

    const size_t numCols = numY + numZ + numX + numS;
    const size_t numRows = nI + nJ + nJ + nK;

    std::vector<double> colCost(numCols, 0.0);
    std::vector<double> colLower(numCols, 0.0);
    std::vector<double> colUpper(numCols, 1.0);

    auto idxY = [&](size_t i) { return i; };
    auto idxZ = [&](size_t j) { return numY + j; };
    auto idxX = [&](size_t i, size_t j) { return numY + numZ + i * nJ + j; };
    auto idxS = [&](size_t j, size_t k) { return numY + numZ + numX + j * nK + k; };

    for (size_t i = 0; i < nI; ++i) {
        colCost[idxY(i)] = data_.fixed_cost_f[i];
    }
    for (size_t j = 0; j < nJ; ++j) {
        colCost[idxZ(j)] = data_.fixed_cost_g[j];
    }

    for (size_t i = 0; i < nI; ++i) {
        for (size_t j = 0; j < nJ; ++j) {
            colCost[idxX(i, j)] = data_.cost_c[j][i];
            colUpper[idxX(i, j)] = data_.capacity_b[i];
        }
    }

    for (size_t j = 0; j < nJ; ++j) {
        for (size_t k = 0; k < nK; ++k) {
            colCost[idxS(j, k)] = data_.cost_d[k][j];
            colUpper[idxS(j, k)] = data_.demand_q[k];
        }
    }

    // y and z are continuous [0,1], x and s are continuous >=0
    for (size_t i = 0; i < numX + numS; ++i) {
    colUpper[numY + numZ + i] = kHighsInf;
    }

    // Rows: plant capacity, depot capacity, depot flow balance, customer demand
    std::vector<double> rowLower(numRows, -kHighsInf);
    std::vector<double> rowUpper(numRows, kHighsInf);

    size_t row = 0;
    // Plant capacity: sum_j x_ij - b_i * y_i <= 0
    for (size_t i = 0; i < nI; ++i, ++row) {
    rowLower[row] = -kHighsInf;
        rowUpper[row] = 0.0;
    }
    // Depot capacity: sum_k s_jk - p_j * z_j <= 0
    for (size_t j = 0; j < nJ; ++j, ++row) {
    rowLower[row] = -kHighsInf;
        rowUpper[row] = 0.0;
    }
    // Depot flow balance: sum_i x_ij - sum_k s_jk = 0
    for (size_t j = 0; j < nJ; ++j, ++row) {
        rowLower[row] = 0.0;
        rowUpper[row] = 0.0;
    }
    // Customer demand: sum_j s_jk = q_k
    for (size_t k = 0; k < nK; ++k, ++row) {
        rowLower[row] = data_.demand_q[k];
        rowUpper[row] = data_.demand_q[k];
    }

    std::vector<int> astart(numCols + 1, 0);
    std::vector<int> aindex;
    std::vector<double> avalue;
    aindex.reserve(numCols * 4);
    avalue.reserve(numCols * 4);

    auto addCoeff = [&](size_t col, size_t rowIndex, double value) {
        aindex.push_back(static_cast<int>(rowIndex));
        avalue.push_back(value);
        ++astart[col + 1];
    };

    // y columns
    for (size_t i = 0; i < nI; ++i) {
        size_t rowIndex = i;
        addCoeff(idxY(i), rowIndex, -data_.capacity_b[i]);
    }

    // z columns
    for (size_t j = 0; j < nJ; ++j) {
        size_t rowIndex = nI + j;
        addCoeff(idxZ(j), rowIndex, -data_.capacity_p[j]);
    }

    // x columns
    for (size_t i = 0; i < nI; ++i) {
        for (size_t j = 0; j < nJ; ++j) {
            size_t col = idxX(i, j);
            addCoeff(col, i, 1.0); // plant capacity row
            size_t balanceRow = nI + nJ + j;
            addCoeff(col, balanceRow, 1.0); // depot flow balance
        }
    }

    // s columns
    for (size_t j = 0; j < nJ; ++j) {
        for (size_t k = 0; k < nK; ++k) {
            size_t col = idxS(j, k);
            size_t capacityRow = nI + j;
            addCoeff(col, capacityRow, 1.0); // depot capacity
            size_t balanceRow = nI + nJ + j;
            addCoeff(col, balanceRow, -1.0); // depot flow balance
            size_t demandRow = nI + nJ + nJ + k;
            addCoeff(col, demandRow, 1.0); // customer demand
        }
    }

    // prefix sum for astart
    for (size_t col = 0; col < numCols; ++col) {
        astart[col + 1] += astart[col];
    }

    HighsLp lp;
    lp.num_col_ = static_cast<int>(numCols);
    lp.num_row_ = static_cast<int>(numRows);
    lp.col_cost_ = std::move(colCost);
    lp.col_lower_ = std::move(colLower);
    lp.col_upper_ = std::move(colUpper);
    lp.row_lower_ = std::move(rowLower);
    lp.row_upper_ = std::move(rowUpper);

    lp.a_matrix_.format_ = MatrixFormat::kColwise;
    lp.a_matrix_.start_ = std::move(astart);
    lp.a_matrix_.index_ = std::move(aindex);
    lp.a_matrix_.value_ = std::move(avalue);

    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.passModel(lp);
    auto status = highs.run();
    if (status != HighsStatus::kOk) {
        throw std::runtime_error("HiGHS failed to solve LP relaxation");
    }

    auto solution = highs.getSolution();
    if (solution.col_value.empty()) {
        throw std::runtime_error("HiGHS returned empty solution");
    }

    RelaxationSolution relax;
    relax.y.resize(nI);
    relax.z.resize(nJ);
    for (size_t i = 0; i < nI; ++i) {
        relax.y[i] = solution.col_value[idxY(i)];
    }
    for (size_t j = 0; j < nJ; ++j) {
        relax.z[j] = solution.col_value[idxZ(j)];
    }

    return relax;
}
