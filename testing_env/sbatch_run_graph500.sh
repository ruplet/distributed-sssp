#!/bin/bash -l
#SBATCH --job-name sssp-tests
#SBATCH --output output.txt
#SBATCH --account "g101-2284"
#SBATCH --cpus-per-task 1
#SBATCH --ntasks-per-node 12
#SBATCH --time 02:00:00

module load common/python/3.11
python3 run_graph500.py
