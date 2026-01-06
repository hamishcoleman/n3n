#!/usr/bin/env python3
"""Read the benchmark output CSV and normalise and calulate based on it"""
#
#

import argparse
import csv
import sys


def argparser():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--file",
        default=sys.stdin,
        type=argparse.FileType("r"),
        help="The csv file to read",
    )
    
    args = ap.parse_args()

    return args

def main():
    args = argparser()

    reader = csv.DictReader(args.file)
    rows = []
    for row in reader:
        # Normalise
        seconds = float(row["seconds"])
        row["bytes_in_persec"] = int(row["bytes_in"]) / seconds
        row["bytes_out_persec"] = int(row["bytes_out"]) / seconds
        row["loops_persec"] = int(row["loops"]) / seconds
        row["instr_perloop"] = int(row["instr"]) / int(row["loops"])

        rows.append(row)


    writer = csv.DictWriter(sys.stdout, rows[0].keys())
    writer.writeheader()

    for row in rows:
        writer.writerow(row)


if __name__ == '__main__':
    main()
