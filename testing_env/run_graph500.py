import argparse
from pathlib import Path
import subprocess
import threading
import os
import filecmp
import sys

TIMEOUT = 7200  # seconds
test_max_str = os.getenv('TESTMAX')
if not test_max_str:
    print('TESTMAX not set. Setting to 10100200300')
    max_nodes = 10100200300
else:
    max_nodes = int(test_max_str)


def read_stream(pipe, output_list, prefix=""):
    """
    Reads lines from a given pipe and appends them to a list,
    printing each line to the console immediately with a prefix.
    """
    # iter(pipe.readline, '') creates an iterator that calls pipe.readline()
    # repeatedly until it returns an empty string (''), indicating EOF.
    for line in iter(pipe.readline, ''):
        print(f"{prefix}{line.strip()}", flush=True) # Print immediately
        output_list.append(line)
    pipe.close() # Close the pipe when done reading all content

def run_tests(break_on_fail, local):
    """
    Runs tests by iterating through solution directories and test cases.
    Streams stdout and stderr of executed processes in real-time.

    Args:
        break_on_fail (bool): If True, the script will exit immediately on the first failure.
        local (bool): If True, uses 'mpiexec'; otherwise, uses 'srun'.
    """
    Path("outputs").mkdir(parents=True, exist_ok=True) # Ensure 'outputs' directory exists

    for solution in Path(".").iterdir():
        # Iterate only through directories that are not 'tests' or 'outputs'
        if solution.is_dir() and solution.name not in ["tests", "outputs"]:
            print(f"\nSolution: {solution.name}")

            # --- Step 1: Run 'make' in the solution directory ---
            # 'make' output is not specifically requested for real-time streaming,
            # so subprocess.run is still suitable here.
            print(f"  Building {solution.name}...", flush=True)
            make = subprocess.run(
                "make",
                cwd=solution.name,
                capture_output=True, # Capture output for error reporting
                text=True,           # Decode stdout/stderr as text
                timeout=300          # Timeout for the make command
            )

            if make.returncode != 0:
                print(f"  {solution.name}: FAILED (make)", flush=True)
                print("  Make STDOUT:\n" + make.stdout, flush=True)
                print("  Make STDERR:\n" + make.stderr, flush=True)
                if break_on_fail:
                    sys.exit(1) # Exit if make fails and break_on_fail is True
                continue # Skip to the next solution if make fails
            print(f"  {solution.name}: Build successful.", flush=True)

            # --- Step 2: Run tests for the current solution ---
            for test in Path("tests").iterdir():
                # Only process test files that contain 'bench' in their name
                if 'bench' not in test.name:
                    print(f"  Skipping non-bench test: {test.name}", flush=True)
                    continue

                print(f"\n  Running test: {test.name}", flush=True)

                # Parse nodes and workers from the test file name
                try:
                    # Example format: "test_1000_16" -> nodes=1000, workers=16
                    parts = test.name.split('_')
                    nodes = int(parts[1])
                    workers = int(parts[2])
                except (IndexError, ValueError):
                    print(f"    Error: Could not parse nodes/workers from test name '{test.name}'. Skipping.", flush=True)
                    continue

                # Skip test if nodes exceed max_nodes limit
                if nodes > max_nodes:
                    print(f"    {test.name}: SKIPPED (Nodes {nodes} > max_nodes {max_nodes})", flush=True)
                    continue

                # Clean up old output files before running the test
                print("    Clearing old output files...", flush=True)
                for f in Path("outputs").iterdir():
                    f.unlink()

                # Determine the command to execute (mpiexec or srun)
                command = "mpiexec" if local else "srun"

                # Construct the full command arguments
                cmd_args = [
                    command,
                    "-n", str(workers), # Number of workers (MPI processes)
                    "./test_command.sh", # The script to execute
                    solution.name,       # Argument 1: solution directory name
                    test.name,           # Argument 2: test file name
                    "10000",             # Argument 3: some value (from original script)
                    "--logging", "debug" # Argument 4: logging level
                ]

                print(f"    Executing command: {' '.join(cmd_args)}", flush=True)

                # --- Step 3: Execute the command using subprocess.Popen for real-time output ---
                process = subprocess.Popen(
                    cmd_args,
                    stdout=subprocess.PIPE, # Capture stdout
                    stderr=subprocess.PIPE, # Capture stderr
                    text=True,              # Decode output as text (UTF-8 by default)
                    encoding='utf-8'        # Explicitly set encoding for robustness
                )

                # Lists to store all captured output for later display on failure
                captured_stdout = []
                captured_stderr = []

                # Create and start threads to read from stdout and stderr pipes concurrently.
                # This ensures that both streams are consumed, preventing deadlocks
                # and allowing real-time printing.
                stdout_thread = threading.Thread(target=read_stream, args=(process.stdout, captured_stdout, "STDOUT: "))
                stderr_thread = threading.Thread(target=read_stream, args=(process.stderr, captured_stderr, "STDERR: "))

                stdout_thread.start()
                stderr_thread.start()

                # --- Step 4: Wait for the process to complete with a timeout ---
                try:
                    process.wait(timeout=TIMEOUT) # Wait for the child process to exit
                except subprocess.TimeoutExpired:
                    # If timeout occurs, terminate the process and report failure
                    print(f"    {test.name}: FAILED (TIMEOUT after {TIMEOUT}s)", flush=True)
                    process.kill() # Terminate the process
                    # Wait for output threads to finish capturing any remaining output
                    stdout_thread.join()
                    stderr_thread.join()
                    print("    Full STDOUT (captured up to timeout):\n" + "".join(captured_stdout), flush=True)
                    print("    Full STDERR (captured up to timeout):\n" + "".join(captured_stderr), flush=True)
                    if break_on_fail:
                        sys.exit(1)
                    continue # Move to the next test

                # Ensure all output has been read from the pipes by joining the threads
                stdout_thread.join()
                stderr_thread.join()

                # --- Step 5: Check the return code of the executed process ---
                return_code = process.returncode

                if return_code != 0:
                    print(f"    {test.name}: FAILED ({command}) (Exit code: {return_code})", flush=True)
                    # Display full captured output on failure (already streamed, but useful for full log)
                    print("    Full STDOUT:\n" + "".join(captured_stdout), flush=True)
                    print("    Full STDERR:\n" + "".join(captured_stderr), flush=True)
                    if break_on_fail:
                        sys.exit(1)
                    continue # Skip to the next test if execution failed

                # --- Step 6: (Optional) Output validation logic (commented out as in original) ---
                failed = False
                # The original script had file comparison commented out.
                # If you re-enable this, ensure `filecmp` is imported.
                # for i in range(workers):
                #     if not filecmp.cmp(f"tests/{test.name}/{i}.out", f"outputs/{i}.out", shallow=False):
                #         print(f"    {test.name}: FAILED (outputs differ on rank {i})")
                #         failed = True
                #         if break_on_fail:
                #             exit(1)
                #         break

                if not failed:
                    print(f"    {test.name}: Finished! Skipping validation", flush=True)
                    # The original script printed stdout again here, but it has already been streamed.
                    # If you still want a full dump here, uncomment the line below.
                    # print('    Full STDOUT:', "".join(captured_stdout), flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Test runner')
    parser.add_argument('-b', '--breakonfail', action='store_true', help='break and print stdout on fail')
    parser.add_argument('-l', '--local', action='store_true', help='run tests locally (without slurm)')

    args = parser.parse_args()
    run_tests(args.breakonfail, args.local)
