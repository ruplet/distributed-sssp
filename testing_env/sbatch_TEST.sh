#!/bin/bash -l
#SBATCH --job-name sssp-tests
#SBATCH --output output.txt
#SBATCH --account "g101-2284"
#SBATCH --nodes 1
#SBATCH --cpus-per-task 1
#SBATCH --ntasks-per-node 24
#SBATCH --time 00:10:00

module load common/python/3.11
make -C LOGIN69
srun -n 4 ./test_command.sh LOGIN69 path_20_4
