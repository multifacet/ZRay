import sys
import pandas as pd

filename = sys.argv[1]

def convert_time_to_seconds(time_str):
    parts = time_str.split(':')
    minutes = int(parts[0])
    seconds = float(parts[1])
    return minutes * 60 + seconds

# Initialize a dictionary to store the data
data = {}
data["Command"] = []
data["Threads"] = []
data["User time (seconds)"] = []
data["System time (seconds)"] = []
data["Wall clock"] = []
data["Max RSS"] = []

with open(filename, 'r') as file:
    for line in file:
        if "User time (seconds):" in line:
            user_time = float(line.split()[-1])
            data["User time (seconds)"].append(user_time)
        elif "System time (seconds):" in line:
            system_time = float(line.split()[-1])
            data["System time (seconds)"].append(system_time)
        elif "Elapsed (wall clock) time (h:mm:ss or m:ss):" in line:
            elapsed_time = line.split()[-1]
            data["Wall clock"].append(float(convert_time_to_seconds(elapsed_time)))
        elif "Command being timed:" in line:
            command = line.split(':')[-1]
            threadCount = int(line.split()[-1].strip('"'))
            data["Command"].append(command)
            data["Threads"].append(threadCount)
        elif "Maximum resident set size (kbytes):" in line:
            rss = line.split(':')[-1]
            data["Max RSS"].append(int(rss))


print("Command:", command)
print("User time (seconds):", user_time)
print("System time (seconds):", system_time)
print("Elapsed (wall clock) time (h:mm:ss or m:ss):", elapsed_time)

df = pd.DataFrame(data)
df.sort_values(by='Threads', ascending=True, inplace=True)
print(df)
