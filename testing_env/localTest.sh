#!/bin/bash
make -C LOGIN69 local
mpirun -n 4 ./test_command.sh LOGIN69 path_20_4
