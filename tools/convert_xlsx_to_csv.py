import argparse
from pathlib import Path

import pandas as pd

SHEETS = ["I", "J", "K", "C", "D"]


def convert_xlsx_to_csv(xlsx_path: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    workbook = pd.ExcelFile(xlsx_path)

    for sheet in SHEETS:
        if sheet not in workbook.sheet_names:
            raise ValueError(f"Missing sheet: {sheet}")

    df_i = pd.read_excel(workbook, sheet_name="I", header=1)
    df_j = pd.read_excel(workbook, sheet_name="J", header=1)
    df_k = pd.read_excel(workbook, sheet_name="K", header=1)

    df_i.to_csv(output_dir / "I.csv", index=False)
    df_j.to_csv(output_dir / "J.csv", index=False)
    df_k.to_csv(output_dir / "K.csv", index=False)

    n_i = len(df_i)
    n_j = len(df_j)
    n_k = len(df_k)

    df_c = pd.read_excel(workbook, sheet_name="C", header=1, index_col=0)
    if df_c.shape[0] == n_i and df_c.shape[1] == n_j:
        df_c = df_c.transpose()
    df_c.to_csv(output_dir / "C.csv", index=True)

    df_d = pd.read_excel(workbook, sheet_name="D", header=1, index_col=0)
    if df_d.shape[0] == n_j and df_d.shape[1] == n_k:
        df_d = df_d.transpose()
    df_d.to_csv(output_dir / "D.csv", index=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert TSCFLP xlsx sheets to CSV files.")
    parser.add_argument("xlsx", type=Path, help="Path to xlsx file")
    parser.add_argument("output", type=Path, help="Output directory for CSV files")
    args = parser.parse_args()

    convert_xlsx_to_csv(args.xlsx, args.output)


if __name__ == "__main__":
    main()
