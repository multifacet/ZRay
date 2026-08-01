#!/bin/bash

# Get the name of the script to be executed
if [ $# -eq 0 ]; then
    echo "Error: Please provide the name of the script to run as an argument." >&2
    exit 1
else
    script_to_run=$1
    script_name=$(basename "$script_to_run" .sh)
fi

timestamp=$(date +%Y%m%d_%H%M%S)

if [ $# -eq 1 ]; then
    # If there is only 1 argument, the combined string should be empty
    combined_args=""

    # Create the directory for test results
    results_dir=./test_results/${script_name}_${timestamp}
    mkdir -p $results_dir
else
    # Combine all arguments except the first one with "_"
    combined_args="$(echo "${@:2}" | tr ' ' '_')"

    # Create the directory for test results
    results_dir=./test_results/${script_name}_${combined_args}_${timestamp}
    mkdir -p $results_dir
fi

# Redirect stdout and stderr to separate files in the results directory
stdout_file=$results_dir/stdout.txt
stderr_file=$results_dir/stderr.txt
$@ > $stdout_file 2> $stderr_file

# Move tool_log_file.txt to the results directory if it exists
if [ -f tool_log_file.txt ]; then
    mv tool_log_file.txt $results_dir
fi
