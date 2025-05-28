from math import ceil, floor
import matplotlib
matplotlib.use('Agg')  # Use the Agg backend to avoid opening a window
import matplotlib.pyplot as plt
import pandas as pd
import ast
import numpy as np
import seaborn as sns
import matplotlib.lines as mlines
import matplotlib.patches as mpatches


shared_color = '#048519'


def calculate_Ts(mu_max, M, X, N, max_numtx , n_s, housekeeping_collision_period):
    # Calculate T_a
    T_a = sum((M / i) + (1 / (i + 1)) + 0.5 for i in range(1, mu_max))
    
    t_h = ceil((max_numtx * 1.01) / housekeeping_collision_period) * housekeeping_collision_period

    # Calculate T_r (assuming constant T_r per step)
    def calculate_Tr(mu_i):
        t_r = t_h * min(floor(E_sigma_O), 1) + ((1 / (mu_i)) + 0.5) * ceil(floor(E_sigma_O)/ rl)
        return t_r

    # Calculate p_nov(mu_i)
    p_values = []
    def p_ov(mu_i):
        p_value = (N - n_s) / (X - (mu_i - 1) - n_s)
        if n_s == 1:
            p_value = 0
        p_values.append(p_value)
        return p_value
    
    # Calculate E_Sigma[O]
    E_sigma_O = sum((p_ov(i) / (1 - p_ov(i))) for i in range(1, mu_max + 1))

    # Calculate T_s
    T_r = calculate_Tr(mu_max)
    Ts = T_a + T_r

    return Ts

experiment_number = 4 # Specify the experiment number
experiment_number_sens = 41
mu_max = 25  # Maximum value for mu_i - maximum cell allocated number
M1 = 50
X = 400       # Total number of cells
N1 = 40
rl = 12
max_numtx1 = 32
housekeeping_collision_period = 60  # Relocation

mu_values = range(2, mu_max + 1)
Ts_values1 = [calculate_Ts(mu, M1, X, N1, max_numtx1, 0, housekeeping_collision_period) for mu in mu_values]
Ts_values2 = [calculate_Ts(mu, M1, X, N1, max_numtx1, 1, housekeeping_collision_period) for mu in mu_values]

# Seaborn settings
sns.set(style="whitegrid", font_scale=1.2)

max_cells = mu_max  # Maximum number of cells

# Path to the CSV file
csv_file = f'results/results_experiment{experiment_number}.csv'
csv_file_sens = f'results/results_experiment{experiment_number_sens}.csv'

# Load the CSV data into a pandas DataFrame
data = pd.read_csv(csv_file)
data_sens = pd.read_csv(csv_file_sens)

# Initialize DataFrames for plotting
plot_data = []

# Function to process data and calculate T_s
def process_data(data, experiment_label, sens):
    for idx, row in data.iterrows():
        start_time = int(row['start_time'])
        network_stable_times = ast.literal_eval(row['network_stable_times'])
        cell_times = row[6:].values

        for i in range(1, max_cells):
            if pd.notna(cell_times[i]):
                allocation_time = int(cell_times[i]) - start_time
                stable_time = int(network_stable_times[i]) - start_time if network_stable_times[i] != 0 else np.nan
                if sens==0:
                    plot_data.append({
                        'Cell Number': i + 1,
                        'Time': allocation_time,
                        'Type': '$T_a$ experimental',
                        'Experiment': experiment_label
                    })
                plot_data.append({
                    'Cell Number': i + 1,
                    'Time': stable_time,
                    'Type': '$T_s$ experimental',
                    'Experiment': experiment_label
                })

# Process both datasets
process_data(data, 'Without Sensing', 0)
process_data(data_sens, 'With Sensing', 1)

# Convert to DataFrame
plot_df = pd.DataFrame(plot_data)

# Plot with Seaborn
plt.figure(figsize=(12, 7))
sns.lineplot(x='Cell Number', y='Time', hue='Type', style='Experiment', data=plot_df, errorbar=('ci', 95), marker='o', legend=False)
plt.plot(mu_values, Ts_values1, color=shared_color, label='$T_s$ Analytical model')
plt.plot(mu_values, Ts_values2, linestyle='--', color=shared_color)

# Create a proxy artist to show the color in the legend
color_patch = mlines.Line2D([], [], color=shared_color, label='Shared Color')

# Add the proxy artist to the legend
plt.legend(handles=[color_patch], title="Legend", loc="best")

header_exp = mlines.Line2D([], [], color='none', label=r'$\mathbf{Experimental}$')
header_ana = mlines.Line2D([], [], color='none', label=r'$\mathbf{Analytical}$')

# Custom legend handles
blue_solid = mlines.Line2D([], [], color="#597cb5", linestyle='-', label='$T_a$ With sensing')
orange_solid = mlines.Line2D([], [], color="#dd8452", linestyle='-', label='$T_s$ Without sensing')
orange_dotted = mlines.Line2D([], [], color="#dd8452", linestyle='--', label='$T_s$ With sensing')
green_solid = mlines.Line2D([], [], color="#048519", linestyle='-', label='$T_s$ Without sensing')
green_dashed = mlines.Line2D([], [], color="#048519", linestyle='--', label='$T_s$ With sensing')

legend = plt.legend(
    handles=[
        header_exp,
        blue_solid,
        orange_solid,
        orange_dotted,
        header_ana,
        green_solid,
        green_dashed
    ],
    loc='lower right',
    frameon=True,
    edgecolor='gray',
    facecolor='white',
    handlelength=2,
    alignment='left'  # This makes the legend entries (including headers) left-aligned
)

# Customize plot
plt.xlabel('Number of allocated cells $\mu_{max}$')
plt.ylabel('Time (seconds)')
plt.title('Average Time for Cell Allocation and Network Stability (95% CI)')
plt.grid(True)
plt.xticks(range(2, max_cells + 1))

# Save the plot
output_file = f'results/plots/experiment{experiment_number}.png'
plt.savefig(output_file, dpi=300)
plt.close()

print(f"Plot saved as {output_file}")

#
# #597cb5 blue ta
#dd8452

