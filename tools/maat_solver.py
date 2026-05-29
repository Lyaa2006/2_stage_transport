#!/usr/bin/env python3
"""MAAT (Matheuristic based on Aggregation Approach and Exact method) solver.

This script follows the four-stage MAAT flow:
1) Customer aggregation (pseudo-customers).
2) Solve aggregated MIP.
3) Fix closed depots from stage 2.
4) Solve reduced original MIP.

Notes for this dataset:
- Original data does not provide explicit customer/depot coordinates nor multi-product demand.
- We approximate customer spatial coordinates using PCA on depot-to-customer cost vectors.
- Aggregated depot-to-pseudo-customer costs are computed as demand-weighted averages
  of original costs within each cluster.
"""
from __future__ import annotations

import argparse
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

import numpy as np
import pandas as pd
from sklearn.cluster import AgglomerativeClustering
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler

try:
    import gurobipy as gp
    from gurobipy import GRB
except ImportError as exc:  # pragma: no cover - runtime dependency
    raise ImportError(
        "gurobipy is required to run this script. "
        "Please ensure Gurobi is installed and licensed."
    ) from exc


@dataclass
class ProblemData:
    capacity_b: np.ndarray  # |I|
    fixed_cost_f: np.ndarray  # |I|
    capacity_p: np.ndarray  # |J|
    fixed_cost_g: np.ndarray  # |J|
    demand_q: np.ndarray  # |K| x |P|
    cost_c: np.ndarray  # |J| x |I|
    cost_d: np.ndarray  # |K| x |J|

    @property
    def num_plants(self) -> int:
        return int(self.capacity_b.shape[0])

    @property
    def num_depots(self) -> int:
        return int(self.capacity_p.shape[0])

    @property
    def num_customers(self) -> int:
        return int(self.demand_q.shape[0])

    @property
    def num_products(self) -> int:
        return int(self.demand_q.shape[1])


def _read_vector_csv(path: Path, cap_col: str, fix_col: str) -> Tuple[np.ndarray, np.ndarray]:
    df = pd.read_csv(path)
    return df[cap_col].to_numpy(dtype=float), df[fix_col].to_numpy(dtype=float)


def _read_demand_csv(path: Path) -> np.ndarray:
    df = pd.read_csv(path)
    demand_cols = [c for c in df.columns if c.lower() not in {"customer_id", "customer", "id"}]
    if not demand_cols:
        raise ValueError(f"No demand columns found in {path}")
    return df[demand_cols].to_numpy(dtype=float)


def _read_matrix_csv(path: Path) -> np.ndarray:
    df = pd.read_csv(path)
    return df.iloc[:, 1:].to_numpy(dtype=float)


def load_problem(data_dir: Path) -> ProblemData:
    capacity_b, fixed_cost_f = _read_vector_csv(data_dir / "I.csv", "Capacity_b", "FixedCost_f")
    capacity_p, fixed_cost_g = _read_vector_csv(data_dir / "J.csv", "Capacity_p", "FixedCost_g")
    demand_q = _read_demand_csv(data_dir / "K.csv")
    cost_c = _read_matrix_csv(data_dir / "C.csv")
    cost_d = _read_matrix_csv(data_dir / "D.csv")

    if cost_c.shape != (capacity_p.shape[0], capacity_b.shape[0]):
        raise ValueError("C.csv size mismatch with I/J")
    if cost_d.shape != (demand_q.shape[0], capacity_p.shape[0]):
        raise ValueError("D.csv size mismatch with K/J")

    return ProblemData(
        capacity_b=capacity_b,
        fixed_cost_f=fixed_cost_f,
        capacity_p=capacity_p,
        fixed_cost_g=fixed_cost_g,
        demand_q=demand_q,
        cost_c=cost_c,
        cost_d=cost_d,
    )


def _detect_customer_coords(k_df: pd.DataFrame) -> Tuple[np.ndarray, np.ndarray] | None:
    candidate_pairs = [
        ("X", "Y"),
        ("x", "y"),
        ("CoordX", "CoordY"),
        ("X_k", "Y_k"),
        ("Lon", "Lat"),
        ("Longitude", "Latitude"),
    ]
    for x_col, y_col in candidate_pairs:
        if x_col in k_df.columns and y_col in k_df.columns:
            return k_df[x_col].to_numpy(dtype=float), k_df[y_col].to_numpy(dtype=float)
    return None


def build_customer_features(demand: np.ndarray, cost_d: np.ndarray, k_df: pd.DataFrame) -> np.ndarray:
    coords = _detect_customer_coords(k_df)
    if coords is None:
        # Approximate spatial coordinates via PCA on depot-to-customer cost vectors.
        scaled_cost = StandardScaler().fit_transform(cost_d)
        pca = PCA(n_components=2, random_state=42)
        coord_embed = pca.fit_transform(scaled_cost)
    else:
        coord_embed = np.column_stack(coords)

    demand_sum = demand.sum(axis=1, keepdims=True)
    demand_profile = np.divide(demand, np.maximum(demand_sum, 1e-9))

    features = np.hstack([coord_embed, demand_profile])
    return StandardScaler().fit_transform(features)


def aggregate_customers(
    data: ProblemData,
    k_df: pd.DataFrame,
    num_clusters: int,
) -> Tuple[ProblemData, np.ndarray]:
    features = build_customer_features(data.demand_q, data.cost_d, k_df)
    clustering = AgglomerativeClustering(n_clusters=num_clusters, linkage="ward")
    labels = clustering.fit_predict(features)

    num_products = data.num_products
    num_depots = data.num_depots

    pseudo_demand = np.zeros((num_clusters, num_products), dtype=float)
    pseudo_cost_d = np.zeros((num_clusters, num_depots), dtype=float)

    demand_total = data.demand_q.sum(axis=1)

    for cluster_id in range(num_clusters):
        idx = np.where(labels == cluster_id)[0]
        if idx.size == 0:
            continue
        pseudo_demand[cluster_id] = data.demand_q[idx].sum(axis=0)

        weights = demand_total[idx]
        if weights.sum() <= 1e-9:
            pseudo_cost_d[cluster_id] = data.cost_d[idx].mean(axis=0)
        else:
            pseudo_cost_d[cluster_id] = np.average(data.cost_d[idx], axis=0, weights=weights)

    aggregated = ProblemData(
        capacity_b=data.capacity_b,
        fixed_cost_f=data.fixed_cost_f,
        capacity_p=data.capacity_p,
        fixed_cost_g=data.fixed_cost_g,
        demand_q=pseudo_demand,
        cost_c=data.cost_c,
        cost_d=pseudo_cost_d,
    )
    return aggregated, labels


def build_model(
    data: ProblemData,
    active_depots: Sequence[int] | None = None,
    fix_closed: Iterable[int] | None = None,
) -> Tuple[gp.Model, Dict[str, Dict[Tuple[int, int, int], gp.Var]]]:
    model = gp.Model("tscflp_maat")
    model.Params.OutputFlag = 0

    nI, nJ, nK, nP = data.num_plants, data.num_depots, data.num_customers, data.num_products

    if active_depots is None:
        active_depots = list(range(nJ))
    active_depots = list(active_depots)
    active_set = set(active_depots)

    if fix_closed is None:
        fix_closed = []
    fix_closed = list(fix_closed)

    y = model.addVars(nI, vtype=GRB.BINARY, name="y")
    z = model.addVars(active_depots, vtype=GRB.BINARY, name="z")

    x = model.addVars(nI, active_depots, nP, lb=0.0, name="x")
    s = model.addVars(active_depots, nK, nP, lb=0.0, name="s")

    fixed_cost = gp.quicksum(data.fixed_cost_f[i] * y[i] for i in range(nI))
    fixed_cost += gp.quicksum(data.fixed_cost_g[j] * z[j] for j in active_depots)

    transport_cost = gp.quicksum(
        data.cost_c[j][i] * x[i, j, p]
        for i in range(nI)
        for j in active_depots
        for p in range(nP)
    )
    transport_cost += gp.quicksum(
        data.cost_d[k][j] * s[j, k, p]
        for j in active_depots
        for k in range(nK)
        for p in range(nP)
    )

    model.setObjective(fixed_cost + transport_cost, GRB.MINIMIZE)

    for i in range(nI):
        model.addConstr(
            gp.quicksum(x[i, j, p] for j in active_depots for p in range(nP))
            <= data.capacity_b[i] * y[i],
            name=f"plant_cap_{i}",
        )

    for j in active_depots:
        model.addConstr(
            gp.quicksum(s[j, k, p] for k in range(nK) for p in range(nP))
            <= data.capacity_p[j] * z[j],
            name=f"depot_cap_{j}",
        )

    for j in active_depots:
        for p in range(nP):
            model.addConstr(
                gp.quicksum(x[i, j, p] for i in range(nI))
                == gp.quicksum(s[j, k, p] for k in range(nK)),
                name=f"flow_balance_{j}_{p}",
            )

    for k in range(nK):
        for p in range(nP):
            model.addConstr(
                gp.quicksum(s[j, k, p] for j in active_depots) == data.demand_q[k, p],
                name=f"demand_{k}_{p}",
            )

    for j in fix_closed:
        if j in active_set:
            model.addConstr(z[j] == 0, name=f"fix_close_{j}")

    return model, {"y": y, "z": z, "x": x, "s": s}


def _extract_open_depots(z_vars: Dict[int, gp.Var], tol: float = 0.5) -> List[int]:
    return [j for j, var in z_vars.items() if var.X >= tol]


def _solution_costs(data: ProblemData, vars_dict: Dict[str, Dict]) -> Tuple[float, float]:
    y = vars_dict["y"]
    z = vars_dict["z"]
    x = vars_dict["x"]
    s = vars_dict["s"]

    fixed_cost = sum(data.fixed_cost_f[i] * y[i].X for i in range(data.num_plants))
    fixed_cost += sum(data.fixed_cost_g[j] * z[j].X for j in z.keys())

    transport_cost = 0.0
    for (i, j, p), var in x.items():
        transport_cost += data.cost_c[j][i] * var.X
    for (j, k, p), var in s.items():
        transport_cost += data.cost_d[k][j] * var.X

    return fixed_cost, transport_cost


def solve_stage(
    data: ProblemData,
    time_limit: float,
    mip_gap: float,
    heuristics: float | None = None,
    presolve: int = 2,
    threads: int = 0,
    active_depots: Sequence[int] | None = None,
    fix_closed: Iterable[int] | None = None,
) -> Tuple[gp.Model, Dict[str, Dict]]:
    model, vars_dict = build_model(
        data,
        active_depots=active_depots,
        fix_closed=fix_closed,
    )

    model.Params.TimeLimit = time_limit
    model.Params.MIPGap = mip_gap
    model.Params.Presolve = presolve
    model.Params.Threads = threads
    if heuristics is not None:
        model.Params.Heuristics = heuristics

    model.optimize()
    return model, vars_dict


def determine_cluster_size(num_customers: int) -> int:
    if num_customers == 400:
        return 80
    if num_customers == 800:
        return 120
    return max(2, int(round(0.2 * num_customers)))


def run_maat(data_dir: Path) -> None:
    start_total = time.perf_counter()
    data = load_problem(data_dir)
    k_df = pd.read_csv(data_dir / "K.csv")

    num_clusters = determine_cluster_size(data.num_customers)

    # Stage 1: aggregation
    aggregated, labels = aggregate_customers(data, k_df, num_clusters)
    print(
        f"Stage1: customers {data.num_customers} -> pseudo-customers {aggregated.num_customers}"
    )

    # Stage 2: aggregated problem
    stage2_limit = 500.0 if data.num_customers >= 800 else 300.0
    stage2_start = time.perf_counter()
    model2, vars2 = solve_stage(
        aggregated,
        time_limit=stage2_limit,
        mip_gap=0.02,
        presolve=2,
        threads=0,
    )
    stage2_elapsed = time.perf_counter() - stage2_start

    open_depots = _extract_open_depots(vars2["z"], tol=0.5)
    print(f"Stage2: runtime {stage2_elapsed:.2f}s, |J_open|={len(open_depots)}")

    # Stage 3: fix closed depots
    closed_depots = [j for j in range(data.num_depots) if j not in open_depots]

    # Stage 4: original problem with fixed closures
    if data.num_customers >= 800:
        stage4_limit = 3000.0
        stage4_gap = 0.03
    else:
        stage4_limit = 1500.0
        stage4_gap = 0.01

    stage4_start = time.perf_counter()
    model4, vars4 = solve_stage(
        data,
        time_limit=stage4_limit,
        mip_gap=stage4_gap,
        heuristics=0.15,
        presolve=2,
        threads=0,
        active_depots=open_depots,
        fix_closed=closed_depots,
    )
    stage4_elapsed = time.perf_counter() - stage4_start

    if model4.SolCount > 0:
        fixed_cost, transport_cost = _solution_costs(data, vars4)
        total_cost = fixed_cost + transport_cost
    else:
        total_cost = float("nan")

    total_elapsed = time.perf_counter() - start_total
    gap = model4.MIPGap if model4.SolCount > 0 else float("nan")

    print(
        "Stage4: TotalCost={:.4f}, Gap={:.4%}, Runtime={:.2f}s (Total {:.2f}s)".format(
            total_cost,
            gap,
            stage4_elapsed,
            total_elapsed,
        )
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="MAAT solver for TSCFLP data")
    parser.add_argument("data_dir", type=Path, help="Path to csv dataset directory")
    args = parser.parse_args()

    if not args.data_dir.is_dir():
        raise FileNotFoundError(f"Dataset directory not found: {args.data_dir}")

    run_maat(args.data_dir)


if __name__ == "__main__":
    main()
