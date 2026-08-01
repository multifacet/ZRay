import sys

def compare_lists(list1, list2):
    list1_only = []
    list2_only = []
    for item in list1:
        if item not in list2:
            list1_only.append(item)
    for item in list2:
        if item not in list1:
            list2_only.append(item)
    return list1_only, list2_only

def record_lines(file1, file2):
    list1 = []
    list2 = []
    with open(file1, 'r') as f1:
        for line in f1:
            list1.append(line.strip())
    with open(file2, 'r') as f2:
        for line in f2:
            list2.append(line.strip())
    return list1, list2


#list1 = [1, 2, 3, 4, 5]
#list2 = [3, 4, 5, 6, 7]
#list1_only, list2_only = compare_lists(list1, list2)
#
#print("List 1 only:", list1_only)
#print("List 2 only:", list2_only)

lists = (record_lines(sys.argv[1], sys.argv[2]))

diff = (compare_lists(lists[0], lists[1]))

print("Only in list : ", sys.argv[1])
print(diff[0])
print("=================")
print("Only in list : ", sys.argv[2])
print(diff[1])


