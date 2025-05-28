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

experiment_number80 = 1
experiment_number40 = 2
experiment_runs = 10
max_cell = 25
max_num_cell = 100

csv_file80 = f'results/results_experiment{experiment_number80}.csv'
csv_file80_sens = f'results/results_experiment{experiment_number80}1.csv'
csv_file40 = f'results/results_experiment{experiment_number40}.csv'
csv_file40_sens = f'results/results_experiment{experiment_number40}1.csv'


# Load the CSV data into a pandas DataFrame
data80 = pd.read_csv(csv_file80)
data80_sens = pd.read_csv(csv_file80_sens)
data40 = pd.read_csv(csv_file40)
data40_sens = pd.read_csv(csv_file40_sens)

# def calculate_pov_average(data):
#     total = 0
#     # Process each row in the dataset
#     for idx, row in data.iterrows():
#         # Parse relocation times and overlap arrays
#         relocation_times = ast.literal_eval(row['relocation_times'])
#         total = sum(relocation_times[:25]) + total
#     return (total/(experiment_runs * max_cell))

def calculate_pov_average_with_ci(data):
    # Store individual sample means
    sample_means = []

    # Process each row in the dataset (each row is a sample)
    for idx, row in data.iterrows():
        # Parse relocation times (10 values per sample)
        relocation_times = ast.literal_eval(row['relocation_times'])
        
        # Calculate the mean for each sample
        sample_mean = np.mean(relocation_times[:10])  # Assuming 10 values per sample
        sample_means.append(sample_mean)
    
    # Calculate overall mean across samples
    overall_mean = np.mean(sample_means)

    # Calculate 95% confidence interval
    ci = stats.t.interval(0.95, len(sample_means) - 1, loc=overall_mean, scale=stats.sem(sample_means))

    return overall_mean

# Initialize lists for plotting
network_loads = ["10%", "20%"]
# approaches = ['No Sensing', 'With Sensing', 'No Sensing', 'With Sensing']
overlap_probs = []

# Calculate overlap probabilities for each condition
overlap_probs.append(calculate_pov_average_with_ci(data40))  # 40 No Sensing
# overlap_probs.append(calculate_pov_average_with_ci(data40_sens))  # 40 With Sensing
overlap_probs.append(calculate_pov_average_with_ci(data80))  # 80 No Sensing
# overlap_probs.append(calculate_pov_average_with_ci(data80_sens))  # 80 With Sensing

# Create a DataFrame for easier plotting
plot_df = pd.DataFrame({
    'Network Load (N)': network_loads,
    # 'Approach': approaches,
    'Overlap Probability': overlap_probs
})

# Plot
plt.figure(figsize=(10, 6))
sns.barplot(x='Network Load (N)', y='Overlap Probability',  data=plot_df, palette='Blues_r')

# Customize plot
plt.title('Overlap Probability vs Network Load (With and Without Sensing)')
plt.xlabel('Network Load (N)')
plt.ylabel('Overlap Probability')
plt.grid(axis='y')

# Save and close the plot
plt.savefig(f'results/plots/experiment_N{max_num_cell}_pov.png', dpi=300)
plt.close()

print('Plot saved as experiment_N_pov.png')