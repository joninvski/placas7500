import pdb
import os, glob
import re
import matplotlib.pyplot as plt


def read_data(file_path):
    f = open(file_path, 'r')
    return f.readlines()[1:]

def treat_data(data):
    treated_data = []

    for line in data:
        treated_line = {}
        s = line.split('\t')
        treated_line['timestamp'] = s[0]
        treated_line['delta'] = s[3]
        treated_line['sequence_number'] = s[6]
        treated_line['source'] = s[9][:-1]
        treated_data.append(treated_line)

    return treated_data

def get_test_results(file_list):
    results = {}

    for f in file_list:
        data = read_data(f)
        treated_data = treat_data(data)
        distance = get_distance(f)
        results[int(distance)] = treated_data

    return results

def get_distance(filename):
    m = re.search('[1-9][0-9][0-9]*', filename)
    return m.group(0)

def get_file_test_a():
    return ['a_test/' + path for path in os.listdir('a_test')]

def get_file_test_b():
    return ['b_test/' + path for path in os.listdir('b_test')]

def sort_dict_values(adict):
    keys = adict.keys()
    keys.sort()
    return [adict[key] for key in keys]

def draw_packets_dropped(results):

    for r in results:
        count = {}

        for v in r.keys():
            count[v] = len(r[v])

        x = sorted(r.keys())
        y = sort_dict_values(count)
        pdb.set_trace() ############################## Breakpoint ##############################

        plt.plot(x, y)
        plt.ylabel('some numbers')
    plt.show()

def main():
    files_a = get_file_test_a()
    files_b = get_file_test_b()

    results_a = get_test_results(files_a)
    results_b = get_test_results(files_b)


    draw_packets_dropped([results_a, results_b])

if __name__ == "__main__":
    main()
