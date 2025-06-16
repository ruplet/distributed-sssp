#!/bin/bash -l
#SBATCH --job-name sssp-tests
#SBATCH --output output.txt
#SBATCH --account "g101-2284"
#SBATCH --ntasks-per-node 24
#SBATCH --nodes 64
#SBATCH --time 00:08:00

module load common/python/3.11
python3 run_graph500.py
