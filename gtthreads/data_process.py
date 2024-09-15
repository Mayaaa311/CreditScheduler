import pandas as pd

# Define the file paths
files = [
    "output_data1.csv",
    "output_data2.csv",
    "output_data3.csv",
    "output_data4.csv"
]

# Read and concatenate the files into a single DataFrame
combined_df = pd.concat([pd.read_csv(file, header=None, names=[ 'thread_number', 'group_name','cpu_time(us)', 'wait_time(us)', 'exec_time(us)']) for file in files])
combined_df['cpu_time(us)'] *= 1000
combined_df['wait_time(us)'] *= 1000
combined_df['exec_time(us)'] *= 1000
# Save the combined DataFrame as Detailed_output.csv
combined_df.to_csv('Detailed_output.csv', index=False)


# Remove 'thread_number' before calculating means
combined_df = combined_df.drop(columns=['thread_number'])

# Calculate the mean values grouped by 'group_name'
summary_df = combined_df.groupby('group_name').mean().reset_index()

# Rename the columns appropriately for the summary
summary_df.columns = ['group_name', 'mean_cpu_time', 'mean_wait_time', 'mean_exec_time']

# Save the summary DataFrame as Cummulative_output.csv
summary_df.to_csv('Cummulative_output.csv', index=False)

print("Data processed and files created successfully.")