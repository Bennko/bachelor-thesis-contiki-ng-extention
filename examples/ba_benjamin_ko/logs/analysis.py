import re
import csv
import os

# Define log file pattern and output file paths
experiment_number = 21
log_folder = f"experiment{experiment_number}/"
out_file_csv = f"results/results_experiment{experiment_number}.csv"
amount_files = 10

# Regular expressions to extract relevant log information
log_patterns = {
    "START_TIME": re.compile(r"Python: Start time (?P<start_time>\d+)s"),
    "RELOCATION_TIMES": re.compile(r"Python: (?P<relocation_times>(\d+, )*\d+)"),
    "NETWORK_STABLE_TIME": re.compile(r"Python: relocation times (?P<network_stable_time>(\d+, )*\d+)"),
    "CELL_ALLOCATION": re.compile(r"Added the (?P<cell_count>\d+) cell at (?P<cell_time>\d+)s"),
    "END_TIME": re.compile(r"Python: All cells (?P<cells_evaluated>\d+) evaluated and no reloaction to be done anymore time (?P<end_time>\d+)s")
}

# List to store extracted log data
log_data = []

# Iterate over 10 log files
for i in range(1, amount_files + 1):
    log_file = os.path.join(log_folder, f"run{i}")
    if not os.path.exists(log_file):
        print(f"Warning: {log_file} not found!")
        continue
    
    entry = {
        "file": f"experiment_logs_{i}.txt", 
        "start_time": None, 
        "cells": [], 
        "end_time": None, 
        "cells_evaluated": None,
        "relocation_times": None,
        "network_stable_time": None
    }
    
    with open(log_file, "r") as f:
        lines = f.readlines()
        
        for j, line in enumerate(lines):
            for key, pattern in log_patterns.items():
                match = pattern.search(line)
                if match:
                    if key == "START_TIME":
                        entry["start_time"] = match.group("start_time")
                    elif key == "CELL_ALLOCATION":
                        cell_time = match.group("cell_time")
                        entry["cells"].append(cell_time)  # Store the time when the cell was added
                    elif key == "END_TIME":
                        entry["end_time"] = match.group("end_time")
                        entry["cells_evaluated"] = match.group("cells_evaluated")
                    elif key == "RELOCATION_TIMES":
                        relocation_str = match.group("relocation_times")
                        entry["relocation_times"] = [int(x) for x in relocation_str.split(", ")]
                    elif key == "NETWORK_STABLE_TIME":
                        network_stable_str = match.group("network_stable_time")
                        entry["network_stable_time"] = [int(x) for x in network_stable_str.split(", ")]
    
    log_data.append(entry)

# Save data as CSV
with open(out_file_csv, "w", newline='') as csv_file:
    fieldnames = ["file", "start_time", "end_time", "cells_evaluated", "relocation_times", "network_stable_times"] + [f"cell_{i+1}" for i in range(max(len(d["cells"]) for d in log_data))]
    writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
    writer.writeheader()
    
    for entry in log_data:
        row = {
            "file": entry["file"],
            "start_time": entry["start_time"],
            "end_time": entry["end_time"],
            "cells_evaluated": entry["cells_evaluated"],
            "relocation_times": entry["relocation_times"],
            "network_stable_times": entry["network_stable_time"]
        }
        for i, cell_time in enumerate(entry["cells"]):
            row[f"cell_{i+1}"] = cell_time
        writer.writerow(row)

print("Log data from multiple files successfully compiled and saved.")