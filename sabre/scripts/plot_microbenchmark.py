import pandas as pd
import matplotlib.pyplot as plt
import sys
import subprocess
import matplotlib.gridspec as gridspec

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
df_handling_I_hw = df[df['name'].str.contains("handling_0_additional_path_0_passthrough_0_path_qpl_path_hardware_mean")]
df_handling_II_hw = df[df['name'].str.contains("handling_1_additional_path_0_passthrough_0_path_qpl_path_hardware_mean")]
df_handling_I_sw = df[df['name'].str.contains("handling_0_additional_path_0_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw = df[df['name'].str.contains("handling_1_additional_path_0_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_snappy = df[df['name'].str.contains("handling_0_additional_path_1_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_snappy = df[df['name'].str.contains("handling_1_additional_path_1_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_zstd1 = df[df['name'].str.contains("handling_0_additional_path_2_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_zstd1 = df[df['name'].str.contains("handling_1_additional_path_2_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_zstd3 = df[df['name'].str.contains("handling_0_additional_path_3_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_zstd3 = df[df['name'].str.contains("handling_1_additional_path_3_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_zstd10 = df[df['name'].str.contains("handling_0_additional_path_4_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_zstd10 = df[df['name'].str.contains("handling_1_additional_path_4_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_zstd20 = df[df['name'].str.contains("handling_0_additional_path_5_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_zstd20 = df[df['name'].str.contains("handling_1_additional_path_5_passthrough_0_path_qpl_path_software_mean")]
df_handling_I_sw_lz4 = df[df['name'].str.contains("handling_0_additional_path_6_passthrough_0_path_qpl_path_software_mean")]
df_handling_II_sw_lz4 = df[df['name'].str.contains("handling_1_additional_path_6_passthrough_0_path_qpl_path_software_mean")]
df_handling_passthrough = df[df['name'].str.contains("additional_path_0_passthrough_1_path_qpl_path_software_mean")]

# Annotate.
def add_value_labels(bars, axis):
    c = 0
    for bar in bars:
        height = bar.get_height()
        value = height
        if height > axis.get_ylim()[1]:
            height = axis.get_ylim()[1]
        axis.text(bar.get_x() + bar.get_width() / 2., 0.9 * height - (c * 0.1 * height), f'{value:.2f}', ha='center', va='bottom', fontsize=14, rotation=0)
        c = c ^ 1

# Create plots for handling_0 and handling_1
fig = plt.figure(figsize=(8, 20))
gs = gridspec.GridSpec(5, 2, height_ratios=[1,1,1,1,0.7])
ticks = [t for t in range(len(df_handling_I_hw['name']))]

ax0 = fig.add_subplot(gs[0, 0:2]) 
ax0.set_title("Hardware-accelerated", fontsize=text_size_medium)
ax0.plot(ticks, df_handling_I_hw['real_time'], color='black', marker='o', linewidth=3, label='Single-chunk prefetching')
ax0.plot(ticks, df_handling_II_hw['real_time'], color='darkred', marker='o', linewidth=3, label='Scattered prefetching')
ax0.plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')

ax1 = fig.add_subplot(gs[1, 0:2]) 
ax1.set_title("Software-based", fontsize=text_size_medium)
ax1.plot(ticks, df_handling_I_sw['real_time'], color='black', marker='o', linewidth=3, label='Single-chunk prefetching')
ax1.plot(ticks, df_handling_II_sw['real_time'], color='darkred', marker='o', linewidth=3, label='Scattered prefetching')
ax1.plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')

ax2 = fig.add_subplot(gs[2, 0:2])
ax2.set_title("Single-chunk prefetching", fontsize=text_size_medium)
ax2.plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')
ax2.plot(ticks, df_handling_I_sw['real_time'], color='gray', marker='^', linewidth=1, label='DEFLATE-sw')
ax2.plot(ticks, df_handling_I_sw_snappy['real_time'], color='gray', marker='x', linewidth=1, label='Snappy')
ax2.plot(ticks, df_handling_I_sw_zstd1['real_time'], color='gray', marker='X', linewidth=1, label='zstd-1')
ax2.plot(ticks, df_handling_I_sw_zstd3['real_time'], color='gray', marker='+', linewidth=1, label='zstd-3')
ax2.plot(ticks, df_handling_I_sw_zstd10['real_time'], color='gray', marker='v', linewidth=1, label='zstd-10')
ax2.plot(ticks, df_handling_I_sw_zstd20['real_time'], color='gray', marker='*', linewidth=1, label='zstd-20')
ax2.plot(ticks, df_handling_I_hw['real_time'], color='darkred', marker='o', linewidth=3, label='IAA')
ax2.set_ylim([230, 450])
ax2.legend(loc='upper right', fontsize=text_size_small)

ax3 = fig.add_subplot(gs[3, 0:2])
ax3.set_title("Scattered prefetching", fontsize=text_size_medium)
ax3.plot(ticks, df_handling_passthrough['real_time'], color='black', linewidth=2, label='Passthrough', linestyle='--')
ax3.plot(ticks, df_handling_II_sw['real_time'], color='gray', marker='^', linewidth=1, label='DEFLATE-sw')
ax3.plot(ticks, df_handling_II_sw_snappy['real_time'], color='gray', marker='x', linewidth=1, label='Snappy')
ax3.plot(ticks, df_handling_II_sw_zstd1['real_time'], color='gray', marker='X', linewidth=1, label='zstd-1')
ax3.plot(ticks, df_handling_II_sw_zstd3['real_time'], color='gray', marker='+', linewidth=1, label='zstd-3')
ax3.plot(ticks, df_handling_II_sw_zstd10['real_time'], color='gray', marker='v', linewidth=1, label='zstd-10')
ax3.plot(ticks, df_handling_II_sw_zstd20['real_time'], color='gray', marker='*', linewidth=1, label='zstd-20')
ax3.plot(ticks, df_handling_II_hw['real_time'], color='darkred', marker='o', linewidth=3, label='IAA')
ax3.set_ylim([150, 450])
ax3.legend(loc='upper right', fontsize=text_size_small)

ax4 = fig.add_subplot(gs[4, 0])
snapshot_creation_single_chunk = [47, 693, 917, 605, 770, 3103, 27326]
snapshot_creation_names = ["IAA", "DEFLATE", "Snappy", "zstd-1", "zstd-3", "zstd-10", "zstd-20"]
colors = ["darkred", "gray", "gray", "gray", "gray", "gray", "gray"]
label_colors = ["darkred", "black", "black", "black", "black", "black", "black"]
snapshot_creation_single_chunk = [t / 1000 for t in snapshot_creation_single_chunk]
width = 0.6
x_positions = [t for t in range(len(snapshot_creation_single_chunk))]
ax4.set_xticks(x_positions)
ax4.set_title("Single-chunk", fontsize=text_size_medium)
ax4.set_ylim([0, 1])
bars = ax4.bar(x_positions, snapshot_creation_single_chunk, width, align='center', label=f'Ratio', color=colors)
ax4.set_xticklabels(snapshot_creation_names, fontsize=text_size_medium, rotation=45)
for tick_label, color in zip(ax4.get_xticklabels(), label_colors):
    tick_label.set_color(color)
add_value_labels(bars, ax4)
ax4.set_ylabel("Snapshot creation\n CPU time, s", fontsize=text_size_medium)
ax4.set_yticks([])

ax5 = fig.add_subplot(gs[4, 1])
snapshot_creation_single_chunk = [4, 934, 948, 807, 948, 4300, 27326]
snapshot_creation_names = ["IAA", "DEFLATE", "Snappy", "zstd-1", "zstd-3", "zstd-10", "zstd-20"]
snapshot_creation_single_chunk = [t / 1000 for t in snapshot_creation_single_chunk]
width = 0.6
x_positions = [t for t in range(len(snapshot_creation_single_chunk))]
ax5.set_xticks(x_positions)
ax5.set_title("Scattered", fontsize=text_size_medium)
ax5.set_ylim([0, 1])
bars = ax5.bar(x_positions, snapshot_creation_single_chunk, width, align='center', label=f'Ratio', color=colors)
ax5.set_xticklabels(snapshot_creation_names, fontsize=text_size_medium, rotation=45)
for tick_label, color in zip(ax5.get_xticklabels(), label_colors):
    tick_label.set_color(color)
add_value_labels(bars, ax5)
ax5.set_ylabel('')
ax5.set_yticks([])

for axx in [ax0, ax1, ax2, ax3]:
    axx.set_xticks(ticks)
    axx.set_xticklabels(sparsities, fontsize=text_size_medium, rotation=45)
    # axx.set_yticks(range(0, 1800, 400))
    axx.yaxis.set_tick_params(labelsize=text_size_medium, rotation=0)
    axx.set_xlabel("Sparsity, 4kB pages", fontsize=text_size_medium)
    axx.set_ylabel("Restoration time, ms", fontsize=text_size_medium)
    axx.grid()

for axx in [ax4, ax5]:
    axx.yaxis.set_tick_params(labelsize=text_size_medium, rotation=0)
    # axx.set_ylabel("Creation CPU time, s", fontsize=text_size_medium)
    axx.grid()

ax0.annotate("Compression ratio: 2.2x", xy=(3.5, 520), fontsize=text_size_big, color='darkred', weight='bold')
ax1.legend(fontsize=text_size_medium)
plt.tight_layout()

# Save the figures as PNG and PDF
plot_name = 'handle_plots'
plt.savefig(f'{plot_name}.png')
plt.savefig(f'{plot_name}.pdf', format='pdf')
print(f'plots saved into {plot_name}')
