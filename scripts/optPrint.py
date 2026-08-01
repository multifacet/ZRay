import sys

filename = sys.argv[1]

with open(filename, "r") as file:
    for line in file:
        words = line.split()
        for word in words:
            if word.startswith("-"):
                print(word)
