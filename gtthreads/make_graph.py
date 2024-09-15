import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Load the Cummulative_output.csv dataset
summary_df = pd.read_csv('Cummulative_output.csv')

# Extract the unique matrix sizes from the group_name column
matrix_sizes = ['32', '64', '128', '256']

# Function to filter by matrix size
def filter_by_matrix_size(df, matrix_size):
    return df[df['group_name'].str.contains(f"_m_{matrix_size}")]

# Generate bar charts with stacked mean CPU time and wait time
for matrix_size in matrix_sizes:
    filtered_df = filter_by_matrix_size(summary_df, matrix_size)
    
    # Sort by the credits (assuming 'c_' denotes credit and it's the first part of the group_name)
    filtered_df['credit'] = filtered_df['group_name'].str.extract(r'c_(\d+)_m_')[0].astype(int)
    filtered_df = filtered_df.sort_values(by='credit')
    
    # Set the positions for the bars
    x = np.arange(len(filtered_df['group_name']))  # the label locations
    width = 0.35  # the width of the bars
    
    # Create the figure and axes
    plt.figure(figsize=(12, 6))
    
    # Plotting the stacked bars
    cpu_bars = plt.bar(x, filtered_df['mean_cpu_time'], width, label='Mean CPU Time (us)')
    wait_bars = plt.bar(x, filtered_df['mean_wait_time'], width, bottom=filtered_df['mean_cpu_time'], label='Mean Wait Time (us)')
    
    # Adding labels and title
    plt.xlabel('Group Name')
    plt.ylabel('Time (us)')
    plt.title(f'Mean Execution Time for Matrix Size {matrix_size} (Stacked)')
    plt.xticks(x, filtered_df['group_name'], rotation=45)
    
    # Move legend outside the plot
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left', borderaxespad=0.)
    
    # Annotate bars with the exact values
    for cpu_bar, wait_bar in zip(cpu_bars, wait_bars):
        # Annotate CPU time
        plt.text(cpu_bar.get_x() + cpu_bar.get_width() / 2, cpu_bar.get_height() / 2, f'{cpu_bar.get_height():.2f}', 
                 ha='center', va='center', color='black')
        # Annotate Wait time on top of CPU time
        total_height = cpu_bar.get_height() + wait_bar.get_height()
        plt.text(wait_bar.get_x() + wait_bar.get_width() / 2, total_height, f'{total_height:.2f}', 
                 ha='center', va='bottom', color='black')
    
    # Save the plot
    plt.tight_layout()  # Adjust the layout to ensure everything fits
    plt.savefig(f'stacked_execution_time_matrix_{matrix_size}.png', bbox_inches='tight')
    plt.show()
