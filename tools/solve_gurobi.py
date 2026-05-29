#!/usr/bin/env python3
"""Solve two-stage capacitated facility location using Gurobi."""

from __future__ import annotations

import csv
import os
import time
from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple

import gurobipy as gp
from gurobipy import GRB


@dataclass
class ProblemData:
    capacity_b: List[float]
    fixed_cost_f: List[float]
    capacity_p: List[float]
    fixed_cost_g: List[float]
    demand_q: List[float]
    cost_c: List[List[float]]  # J x I
    cost_d: List[List[float]]  # K x J

    @property
    def num_plants(self) -> int:
        return len(self.capacity_b)

    @property
    def num_depots(self) -> int:
        return len(self.capacity_p)

    @property
    def num_customers(self) -> int:
        return len(self.demand_q)


def _read_vector_csv(path: str, cap_col: str, fix_col: str) -> Tuple[List[float], List[float]]:
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if cap_col not in reader.fieldnames or fix_col not in reader.fieldnames:
            raise ValueError(f"Missing columns in {path}")
        capacity, fixed = [], []
        for row in reader:
            capacity.append(float(row[cap_col]))
            fixed.append(float(row[fix_col]))
    return capacity, fixed


def _read_demand_csv(path: str, demand_col: str) -> List[float]:
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if demand_col not in reader.fieldnames:
            raise ValueError(f"Missing demand column in {path}")
        return [float(row[demand_col]) for row in reader]


def _read_matrix_csv(path: str) -> List[List[float]]:
    with open(path, newline="") as f:
        reader = csv.reader(f)
        headers = next(reader, None)
        if headers is None or len(headers) < 2:
            raise ValueError(f"Matrix CSV missing headers: {path}")
        matrix: List[List[float]] = []
        for row in reader:
            if not row:
                continue
            matrix.append([float(cell) for cell in row[1:]])
        return matrix


def load_problem(data_dir: str) -> ProblemData:
    capacity_b, fixed_cost_f = _read_vector_csv(
        os.path.join(data_dir, "I.csv"), "Capacity_b", "FixedCost_f"
    )
    capacity_p, fixed_cost_g = _read_vector_csv(
        os.path.join(data_dir, "J.csv"), "Capacity_p", "FixedCost_g"
    )
    demand_q = _read_demand_csv(os.path.join(data_dir, "K.csv"), "Demand_q")
    cost_c = _read_matrix_csv(os.path.join(data_dir, "C.csv"))
    cost_d = _read_matrix_csv(os.path.join(data_dir, "D.csv"))

    if len(cost_c) != len(capacity_p):
        raise ValueError("C.csv row count does not match J size")
    if cost_c and len(cost_c[0]) != len(capacity_b):
        raise ValueError("C.csv col count does not match I size")
    if len(cost_d) != len(demand_q):
        raise ValueError("D.csv row count does not match K size")
    if cost_d and len(cost_d[0]) != len(capacity_p):
        raise ValueError("D.csv col count does not match J size")

    return ProblemData(
        capacity_b=capacity_b,
        fixed_cost_f=fixed_cost_f,
        capacity_p=capacity_p,
        fixed_cost_g=fixed_cost_g,
        demand_q=demand_q,
        cost_c=cost_c,
        cost_d=cost_d,
    )


def build_model(data: ProblemData) -> Tuple[gp.Model, Dict[str, Dict[Tuple[int, int], gp.Var]]]:
    model = gp.Model("tscflp")
    model.Params.OutputFlag = 0

    y = model.addVars(data.num_plants, vtype=GRB.BINARY, name="y")
    z = model.addVars(data.num_depots, vtype=GRB.BINARY, name="z")
    x = model.addVars(data.num_plants, data.num_depots, lb=0.0, name="x")
    s = model.addVars(data.num_depots, data.num_customers, lb=0.0, name="s")

    fixed_cost = gp.quicksum(data.fixed_cost_f[i] * y[i] for i in range(data.num_plants))
    fixed_cost += gp.quicksum(data.fixed_cost_g[j] * z[j] for j in range(data.num_depots))

    transport_cost = gp.quicksum(
        data.cost_c[j][i] * x[i, j] for i in range(data.num_plants) for j in range(data.num_depots)
    )
    transport_cost += gp.quicksum(
        data.cost_d[k][j] * s[j, k] for j in range(data.num_depots) for k in range(data.num_customers)
    )

    model.setObjective(fixed_cost + transport_cost, GRB.MINIMIZE)

    for i in range(data.num_plants):
        model.addConstr(
            gp.quicksum(x[i, j] for j in range(data.num_depots))
            <= data.capacity_b[i] * y[i],
            name=f"plant_cap_{i}",
        )

    for j in range(data.num_depots):
        model.addConstr(
            gp.quicksum(s[j, k] for k in range(data.num_customers))
            <= data.capacity_p[j] * z[j],
            name=f"depot_cap_{j}",
        )

    for j in range(data.num_depots):
        model.addConstr(
            gp.quicksum(x[i, j] for i in range(data.num_plants))
            == gp.quicksum(s[j, k] for k in range(data.num_customers)),
            name=f"flow_balance_{j}",
        )

    for k in range(data.num_customers):
        model.addConstr(
            gp.quicksum(s[j, k] for j in range(data.num_depots)) == data.demand_q[k],
            name=f"demand_{k}",
        )

    return model, {"y": y, "z": z, "x": x, "s": s}


def _solution_costs(data: ProblemData, vars_dict: Dict[str, Dict[Tuple[int, int], gp.Var]]) -> Tuple[float, float]:
    y = vars_dict["y"]
    z = vars_dict["z"]
    x = vars_dict["x"]
    s = vars_dict["s"]

    fixed_cost = sum(data.fixed_cost_f[i] * y[i].X for i in range(data.num_plants))
    fixed_cost += sum(data.fixed_cost_g[j] * z[j].X for j in range(data.num_depots))

    transport_cost = sum(
        data.cost_c[j][i] * x[i, j].X for i in range(data.num_plants) for j in range(data.num_depots)
    )
    transport_cost += sum(
        data.cost_d[k][j] * s[j, k].X for j in range(data.num_depots) for k in range(data.num_customers)
    )

    return fixed_cost, transport_cost


def solve_dataset(data_dir: str, log_path: str) -> None:
    data = load_problem(data_dir)
    model, vars_dict = build_model(data)

    start = time.perf_counter()
    model.optimize()
    elapsed = time.perf_counter() - start

    if model.status != GRB.OPTIMAL:
        status_name = model.status
        message = f"Model not optimal. Status={status_name}"
    else:
        fixed_cost, transport_cost = _solution_costs(data, vars_dict)
        message = (
            f"Objective={model.objVal:.4f}\n"
            f"FixedCost={fixed_cost:.4f}\n"
            f"TransportCost={transport_cost:.4f}"
        )

    with open(log_path, "w", encoding="utf-8") as f:
        f.write(f"Dataset={data_dir}\n")
        f.write(
            "Sizes="
            f"I={data.num_plants}, J={data.num_depots}, K={data.num_customers}\n"
        )
        f.write(f"RuntimeSeconds={elapsed:.6f}\n")
        f.write(message)
        f.write("\n")


def main() -> None:
    base = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    datasets = [
        (os.path.join(base, "data", "csv_100200400"), os.path.join(base, "run_gurobi_100200400.log")),
        (os.path.join(base, "data", "csv_200400800"), os.path.join(base, "run_gurobi_200400800.log")),
    ]

    for data_dir, log_path in datasets:
        if not os.path.isdir(data_dir):
            raise FileNotFoundError(f"Missing dataset directory: {data_dir}")
        solve_dataset(data_dir, log_path)


if __name__ == "__main__":
    main()
