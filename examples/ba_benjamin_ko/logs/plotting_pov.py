import matplotlib
matplotlib.use('Agg')  # Use the Agg backend to avoid opening a window
import matplotlib.pyplot as plt
import pandas as pd
import ast
import seaborn as sns
from scipy import stats
import numpy as np

# Seaborn settings
sns.set(style="whitegrid", font_scale=1.2)

# Define experiment info
experiments = [
    {"number": 1, "max_num_cells": 100, "network_load": "20%"},
    {"number": 2, "max_num_cells": 100, "network_load": "10%"},
    {"number": 3, "max_num_cells": 50,  "network_load": "20%"},
    {"number": 4, "max_num_cells": 50,  "network_load": "10%"},
]

def calculate_pov_average_with_ci(data):
    sample_means = []
    for _, row in data.iterrows():
        relocation_times = ast.literal_eval(row['relocation_times'])
        sample_mean = np.mean(relocation_times[:10])
        sample_means.append(sample_mean)
    overall_mean = np.mean(sample_means)
    ci = stats.t.interval(0.95, len(sample_means) - 1, loc=overall_mean, scale=stats.sem(sample_means))
    return overall_mean

# Gather data
plot_data = []

for exp in experiments:
    file_path = f'results/results_experiment{exp["number"]}.csv'
    df = pd.read_csv(file_path)
    avg = calculate_pov_average_with_ci(df)
    plot_data.append({
        "Network Load (N)": exp["network_load"],
        "Max Num Cells": exp["max_num_cells"],
        "Overlap Probability": avg
    })

# Convert to DataFrame for plotting
plot_df = pd.DataFrame(plot_data)

# Plot
plt.figure(figsize=(10, 6))
sns.barplot(
    x='Network Load (N)',
    y='Overlap Probability',
    hue='Max Num Cells',
    data=plot_df,
    palette='Set2'
)

# Customize
plt.title('Overlap Probability vs Network Load\n(Comparing Max Num Cells: 100 vs 50)')
plt.xlabel('Network Interference (N)')
plt.ylabel('Overlap Probability')
plt.legend(title='Max Num Cells')
plt.grid(axis='y')

# Save and close the plot
plt.savefig('results/plots/experiment_compare_maxcells_pov.png', dpi=300)
plt.close()

print('Plot saved as experiment_compare_maxcells_pov.png')
