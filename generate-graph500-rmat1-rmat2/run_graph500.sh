#!/bin/bash

# --- Usage check ---
if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <scale> <edgefactor>"
  exit 1
fi

scale="$1"
edgefactor="$2"

# --- Expected constants ---
expected_seed1=2
expected_seed2=3
expected_A=5700
expected_BC=1900
expected_D=10000

# --- Verify seeds in main.c ---
seed_line=$(sed -n '85p' main.c)
if ! echo "$seed_line" | grep -q "uint64_t seed1 = $expected_seed1, seed2 = $expected_seed2"; then
  echo "Error: Expected seed1=$expected_seed1 and seed2=$expected_seed2 in main.c"
  echo "Found: $seed_line"
  exit 2
fi

# --- Verify generation constants in graph_generator.c ---
gen_params=$(sed -n '27,29p' ../generator/graph_generator.c)

if ! echo "$gen_params" | grep -q "#define INITIATOR_A_NUMERATOR $expected_A"; then
  echo "Error: INITIATOR_A_NUMERATOR is not $expected_A"
  echo "$gen_params"
  exit 3
fi
if ! echo "$gen_params" | grep -q "#define INITIATOR_BC_NUMERATOR $expected_BC"; then
  echo "Error: INITIATOR_BC_NUMERATOR is not $expected_BC"
  echo "$gen_params"
  exit 3
fi
if ! echo "$gen_params" | grep -q "#define INITIATOR_DENOMINATOR $expected_D"; then
  echo "Error: INITIATOR_DENOMINATOR is not $expected_D"
  echo "$gen_params"
  exit 3
fi

# --- Auto-generate output directory ---
outdir="out_a${expected_A}_bc${expected_BC}_d${expected_D}_s${expected_seed1}_${expected_seed2}_n${scale}_ef${edgefactor}"
mkdir -p "$outdir" || { echo "Failed to create output directory $outdir"; exit 4; }

# --- Set environment variables ---
export REUSEFILE=1
export VERBOSE=0
export TMPFILE="${outdir}/edges.out"

# --- Run benchmark ---
stdout_file="${outdir}/output.txt"
# srun -n 64 --time 00:30 ./graph500_reference_bfs_sssp "$scale" "$edgefactor" > "$stdout_file" 2>/dev/null
srun -n 32 --time 60:00 ./graph500_reference_bfs_sssp "$scale" "$edgefactor" > "$stdout_file"
exit_status=$?

# --- Report ---
echo "Exit status: $exit_status"
if [ "$exit_status" -eq 0 ]; then
  echo "Success."
  echo "  TMPFILE: $TMPFILE"
  echo "  STDOUT:  $stdout_file"
else
  echo "Failure. See $stdout_file if partially written."
fi

exit $exit_status

