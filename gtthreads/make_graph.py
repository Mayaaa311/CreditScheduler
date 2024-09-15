import pandas as pd
import matplotlib.pyplot as plt

# Load the Cummulative_output.csv dataset
summary_df = pd.read_csv('Cummulative_output.csv')

# Extract the unique matrix sizes from the group_name column
matrix_sizes = ['32', '64', '128', '256']

# Function to filter by matrix size
def filter_by_matrix_size(df, matrix_size):
    return df[df['group_name'].str.contains(f"_m_{matrix_size}")]

# Generate four bar charts, one for each matrix size
for matrix_size in matrix_sizes:
    filtered_df = filter_by_matrix_size(summary_df, matrix_size)
    
    # Sort by the credits (assuming 'c_' denotes credit and it's the first part of the group_name)
    filtered_df['credit'] = filtered_df['group_name'].str.extract(r'c_(\d+)_m_')[0].astype(int)
    filtered_df = filtered_df.sort_values(by='credit')
    
    # Create a stacked bar chart for mean execution time
    plt.figure(figsize=(10, 6))
    plt.bar(filtered_df['group_name'], filtered_df['mean_cpu_time'], label='Mean CPU Time (us)')
    plt.bar(filtered_df['group_name'], filtered_df['mean_wait_time'], bottom=filtered_df['mean_cpu_time'], label='Mean Wait Time (us)')
    
    plt.title(f'Mean Execution Time for Matrix Size {matrix_size}')
    plt.xlabel('Group Name')
    plt.ylabel('Time (us)')
    plt.xticks(rotation=45)
    plt.legend()
    
    # Save each plot
    plt.savefig(f'mean_execution_time_matrix_{matrix_size}.png')
    plt.show()
