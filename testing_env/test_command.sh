#!/bin/bash
if [ -z ${OMPI_COMM_WORLD_LOCAL_RANK+x} ]; then RANK=$SLURM_PROCID; else RANK=$OMPI_COMM_WORLD_LOCAL_RANK; fi

# Check at least 2 positional arguments: binary subdir and test name
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <binary_subdir> <test_name> [options...]"
    echo "Example: $0 build test01 --logging debug --progress-freq 5 --noios"
    exit 1
fi

# Extract required arguments
BINARY_SUBDIR="$1"
TEST_NAME="$2"
shift 2  # Remove $1 and $2 from the list, leave remaining args

# Run the binary with appropriate input/output and forwarded optional args
stdbuf -o0 -e0 ./$BINARY_SUBDIR/sssp "tests/$TEST_NAME/$RANK.in" "outputs/$RANK.out" "$@"