#!/usr/bin/python3

"""
This script generates the write-heavy access pattern for ABDB vs Redis evaluation.
"""
import pandas as pd
import argparse
import numpy as np

parser = argparse.ArgumentParser(description='Read-heavy access pattern generator')
parser.add_argument('function_count', type=int, help='Number of functions')
parser.add_argument('mu', type=float, help='Expected value of the write count per sec')
parser.add_argument('sigma', type=float, help='Deviation of the write count per sec')
parser.add_argument('max_read_count', type=int,
                    help='The maximum possible read count of a function. The actual value is calculated according to uniform distribution between 0 and this given value.')
parser.add_argument('max_iter_to_migrate', type=int,
                    help='The maximum possible iteration number after a function is migrated. The actual value is calculated according to uniform distribution between 0 and this given value.')
parser.add_argument('keys_count', type=int,
                    help='The number of keys which are accessed by the functions')
parser.add_argument('server_count', type=int,
                    help='The number of the server cluster. These servers run the functions and store the data')
args = parser.parse_args()

df = pd.DataFrame({}, columns=['function_name', 'reading_count', 'writing_count', 'when_to_move', 'key_to_access',
                               'initial_server'])

for i in range(args.function_count):
    func_name = 'function_{}'.format(i)
    write_count = int(np.random.normal(args.mu, args.sigma, 1)[0])
    read_count = np.random.randint(0, args.max_read_count, 1)[0]
    # Migrate the function in each (calculated value below)th iteration
    migration = np.random.randint(0, args.max_iter_to_migrate, 1)[0]
    key = 'key{}'.format(np.random.randint(0, args.keys_count, 1)[0])
    server = 'server{}'.format(np.random.randint(0, args.server_count, 1)[0])
    df = df.append(
        {'function_name': func_name, 'reading_count': read_count, 'writing_count': write_count, 'when_to_move': migration,
         'key_to_access': key, 'initial_server': server},
        ignore_index=True)
pd.options.display.width = 0
print(df)
print("Writing dataframe to a csv file...")
df.to_csv('write_heavy_access_pattern.csv',index=False)
print("DONE")

