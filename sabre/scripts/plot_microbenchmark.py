import pandas as pd
import matplotlib.pyplot as plt
import sys
import subprocess

# Specify the path to your CSV file
csv_file_path = sys.argv[1]
sparsities = ["1",  "2",   "4",    "10",   "20", "50", "100", "1k", "5k", "20k"]

text_size_big = 20
text_size_medium = 18
text_size_small = 12

# Filter log file.
output_csv_file_path = f'{csv_file_path}.out'
command = f'grep -E "^(name,iterations|\\"BM_Benchmark)" {csv_file_path} > {output_csv_file_path}'
subprocess.run(command, shell=True, check=True)

# Read the CSV file into a DataFrame
df = pd.read_csv(output_csv_file_path)

# Convert the 'label' column to strings
df['name'] = df['name'].astype(str)
df['real_time'] = df['real_time'] / (1000 * 1000)

# Filter data for handling_0 and handling_1
df_handling_I_hw = df[df['name'].str.contains("handling_0_passthrough_0_path_qpl_path_hardware_mean")]
df_handling_II_hw = df[df['name'].str.contains("handling_1_passthrough_0_path_qpl_path_hardware_mean")]
df_handling_I_sw = df[df['name'].str.contains("handling_0_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw = df[df['name'].str.contains("handling_1_passthrough_0_path_qpl_path_software_mean")]
df_handling_passthrough = df[df['name'].str.contains("passthrough_1_path_qpl_path_hardware_mean")]

# Create plots for handling_0 and handling_1
fig, ax = plt.subplots(1, 2, figsize=(16, 4))
ticks = [t for t in range(len(df_handling_I_hw['name']))]
ax[0].set_title("Hardware-accelerated", fontsize=text_size_medium)
ax[0].plot(ticks, df_handling_I_hw['real_time'], color='black', marker='o', linewidth=3, label='Single-chunk prefetching')
ax[0].plot(ticks, df_handling_II_hw['real_time'], color='darkred', marker='o', linewidth=3, label='Scattered prefetching')
ax[0].plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')
ax[1].set_title("Software-based", fontsize=text_size_medium)
ax[1].plot(ticks, df_handling_I_sw['real_time'], color='black', marker='o', linewidth=3, label='Single-chunk prefetching')
ax[1].plot(ticks, df_handling_II_sw['real_time'], color='darkred', marker='o', linewidth=3, label='Scattered prefetching')
ax[1].plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')
for axx in ax:
    axx.set_xticks(ticks)
    axx.set_xticklabels(sparsities, fontsize=text_size_medium, rotation=45)
    # axx.set_yticks(range(0, 1800, 400))
    axx.yaxis.set_tick_params(labelsize=text_size_medium, rotation=0)
    axx.set_xlabel("Sparsity, 4kB pages", fontsize=text_size_medium)
    axx.set_ylabel("Restoration time, ms", fontsize=text_size_medium)
    axx.grid()

ax[0].annotate("Compression ratio: 2.2x", xy=(3.5, 520), fontsize=text_size_big, color='darkred', weight='bold')
ax[1].legend(fontsize=text_size_medium)

plt.tight_layout()

# Save the figures as PNG and PDF
plot_name = 'handle_plots'
plt.savefig(f'{plot_name}.png')
plt.savefig(f'{plot_name}.pdf', format='pdf')
print(f'plots saved into {plot_name}')
