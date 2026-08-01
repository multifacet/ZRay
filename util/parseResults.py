import os
import sys

def main():

    if len(sys.argv) == 1:
        print("Usage: {} resultsDirectory".format(sys.argv[0]))
        return 1


    directory=sys.argv[1]

    for file in os.listdir(directory):
        print(directory + "/" + file)
        
        with open(directory + "/" + file) as f:
            foundTSC = False
            for l in f.readlines():
                if "TSC_DATA" in l:
                    foundTSC = True
                elif foundTSC:
                    l = l.split(' ')
                    if "Average:" in l[0]:
                        print("Average Cycles: ", int(l[1]))

if __name__ == "__main__":
    main()
