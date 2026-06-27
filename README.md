# TSCFLP GA with Elitism

（基于论文A simple and effective genetic algorithm for the two-stage capacitated facility location problem的复现）

该工程基于论文描述实现了带精英策略的遗传算法，用于求解两阶段容量限制设施选址问题（TSCFLP）。

## 依赖
- C++17
- LEMON
- HiGHS
- Python 3（用于将 Excel 转为 CSV）

## 构建与运行
示例数据位于 `data/data_100200400.xlsx` 与 `data/data_200400800.xlsx`。
需要先将 Excel 转为 CSV 目录（`I.csv/J.csv/K.csv/C.csv/D.csv`）。

```bash
python tools/convert_xlsx_to_csv.py data/data_100200400.xlsx data/csv_100200400
python tools/convert_xlsx_to_csv.py data/data_200400800.xlsx data/csv_200400800
```

```bash
cmake -S . -B build
cmake --build build
./build/tscflp_ga data/csv_100200400 run.log
```

输出目录可替换为 `data/csv_200400800` 等其他转换后的数据集路径。第二个参数为日志文件路径，
日志会记录迭代过程、最终最优解和运行时间。程序默认在连续 200 代无改进时触发早停。

## 输出说明
程序会输出每 10 代的最优适应度，并在结束时输出固定成本、运输成本与总耗时。

# MAAT Python 脚本（客户聚合 + 精确求解）

（基于论文Formulation and solution of a two-stage capacitated facility location problem with multilevel capacities的复现）

新增脚本 `tools/maat_solver.py`，按照 MAAT（Matheuristic based on Aggregation Approach and Exact method）流程
执行客户聚合、聚合模型求解、变量固定与原问题精确求解。

运行示例：

```bash
python tools/maat_solver.py data/csv_100200400
python tools/maat_solver.py data/csv_200400800
```

脚本输出包含：
- 阶段 1 聚类前后维度对比（例如 800 -> 120）
- 阶段 2 求解耗时与潜在打开仓库数量
- 阶段 4 最终总成本、Gap 与总运行时间
