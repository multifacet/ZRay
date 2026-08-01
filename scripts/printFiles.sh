#!/bin/bash

directory="$1"

targetFile="$2"

if [ -d "$directory" ]; then
  for file in "$directory"/*; do
    if [ -d "$file" ]; then
      # If the file is a directory, call the script recursively
      ./"$0" "$file" "$targetFile"
    elif [ "${file##*/}" = "$2" ]; then
      # If the file is the target, call cat on it
      echo "Current Directory: $(basename "$(dirname "$file")")"
      cat "$file"
      echo "==================================="
    fi
  done
else
  echo "Error: $directory is not a directory."
fi
