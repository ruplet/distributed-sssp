#!/bin/bash -l
#SBATCH --job-name sssp-tests
#SBATCH --output output.txt
#SBATCH --account "g101-2284"
#SBATCH --cpus-per-task 1
#SBATCH --ntasks-per-node 24
#SBATCH --time 00:60:00

module load common/python/3.11
python3 run_tests_big.py
