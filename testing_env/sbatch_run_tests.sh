#!/bin/bash -l
#SBATCH --job-name sssp-tests
#SBATCH --output output.txt
#SBATCH --account "g101-2284"
#SBATCH --ntasks-per-node 24
#SBATCH --nodes 8
#SBATCH --time 00:05:00

module load common/python/3.11
python3 run_tests.py
