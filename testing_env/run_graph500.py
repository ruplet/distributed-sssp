import argparse
from pathlib import Path
import subprocess
import threading
import os
import filecmp
import sys
import codecs

TIMEOUT = 7200  # seconds
test_max_str = os.getenv('TESTMAX')
if not test_max_str:
    print('TESTMAX not set. Setting to 10100200300')
    max_nodes = 10100200300
else:
    max_nodes = int(test_max_str)


def read_stream(pipe, output_list, prefix="", chunk_size=1, encoding='utf-8'):
    """
    Reads bytes from a pipe, decodes them, and prints them immediately as they arrive.
    Also reconstructs lines for storage. This ensures the most real-time feedback
    for non-newline-terminated output, potentially printing character-by-character
    if chunk_size is small.
    """
    decoder = codecs.getincrementaldecoder(encoding)()
    current_line_buffer = "" # Buffer to reconstruct full lines for output_list

    while True:
        try:
            # Read a small chunk of bytes from the pipe
            byte_chunk = pipe.read(chunk_size)
        except ValueError: # Handle I/O operation on closed pipe (can happen during termination)
            break
        except Exception as e:
            print(f"Error reading from pipe: {e}", file=sys.stderr)
            break

        if not byte_chunk: # End-of-file (EOF) reached
            break

        # Decode the bytes into a string
        decoded_chunk = decoder.decode(byte_chunk)

        # CRUCIAL CHANGE: Print the decoded chunk immediately without waiting for a newline.
        # Use end='' to prevent 'print' from adding its own newline,
        # ensuring output appears exactly as received.
        print(f"{prefix}{decoded_chunk}", end='', flush=True)

        # Add the decoded chunk to the buffer for full line reconstruction for `output_list`
        current_line_buffer += decoded_chunk

        # Process any complete lines in the buffer for the `output_list`
        # This ensures that `output_list` contains logical lines,
        # even if they were printed as smaller chunks.
        while '\n' in current_line_buffer:
            # partition('\n') splits at the first newline, returning (before, newline, after)
            line, _, current_line_buffer = current_line_buffer.partition('\n')
            output_list.append(line + '\n') # Store the full line including the newline

    # After the loop, if there's any remaining content in the buffer (no trailing newline at EOF)
    # This content has already been printed chunk by chunk. We just append it to the output_list
    # for completeness, representing a final, potentially incomplete line.
    if current_line_buffer:
        output_list.append(current_line_buffer)

    try:
        pipe.close() # Close the pipe when done reading all content
    except Exception as e:
        print(f"Error closing pipe: {e}", file=sys.stderr)

def run_tests(break_on_fail, local):
    """
    Runs tests by iterating through solution directories and test cases.
    Streams stdout and stderr of executed processes in real-time,
    handling partial lines.

    Args:
        break_on_fail (bool): If True, the script will exit immediately on the first failure.
        local (bool): If True, uses 'mpiexec'; otherwise, uses 'srun'.
    """
    Path("outputs").mkdir(parents=True, exist_ok=True)

    for solution in Path(".").iterdir():
        if solution.is_dir() and solution.name not in ["tests", "outputs"]:
            print(f"\nSolution: {solution.name}")

            # --- Step 1: Run 'make' in the solution directory ---
            print(f"  Building {solution.name}...", flush=True)
            make = subprocess.run(
                "make",
                cwd=solution.name,
                capture_output=True,
                text=True, # text=True is fine here as make output is typically line-buffered
                timeout=300
            )

            if make.returncode != 0:
                print(f"  {solution.name}: FAILED (make)", flush=True)
                print("  Make STDOUT:\n" + make.stdout, flush=True)
                print("  Make STDERR:\n" + make.stderr, flush=True)
                if break_on_fail:
                    sys.exit(1)
                continue
            print(f"  {solution.name}: Build successful.", flush=True)

            # --- Step 2: Run tests for the current solution ---
            for test in Path("tests").iterdir():
                if 'bench' not in test.name:
                    print(f"  Skipping non-bench test: {test.name}", flush=True)
                    continue

                print(f"\n  Running test: {test.name}", flush=True)

                try:
                    parts = test.name.split('_')
                    nodes = int(parts[1])
                    workers = int(parts[2])
                except (IndexError, ValueError):
                    print(f"    Error: Could not parse nodes/workers from test name '{test.name}'. Skipping.", flush=True)
                    continue

                if nodes > max_nodes:
                    print(f"    {test.name}: SKIPPED (Nodes {nodes} > max_nodes {max_nodes})", flush=True)
                    continue

                print("    Clearing old output files...", flush=True)
                for f in Path("outputs").iterdir():
                    f.unlink()

                command = "mpiexec" if local else "srun"
                cmd_args = [
                    command,
                    "-n", str(workers),
                    "./test_command.sh",
                    solution.name,
                    test.name,
                    "10000",
                    "--logging", "debug"
                ]

                print(f"    Executing command: {' '.join(cmd_args)}", flush=True)

                # --- Step 3: Execute the command using subprocess.Popen for real-time output ---
                # CRUCIAL CHANGE: text=False here, so we receive bytes and handle decoding
                process = subprocess.Popen(
                    cmd_args,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=False # This is key: we get raw bytes and decode manually in read_stream
                )

                captured_stdout = []
                captured_stderr = []

                # Create and start threads to read from stdout and stderr pipes concurrently.
                # Pass the raw byte pipes to the read_stream function.
                stdout_thread = threading.Thread(target=read_stream, args=(process.stdout, captured_stdout, "STDOUT: "))
                stderr_thread = threading.Thread(target=read_stream, args=(process.stderr, captured_stderr, "STDERR: "))

                stdout_thread.start()
                stderr_thread.start()

                # --- Step 4: Wait for the process to complete with a timeout ---
                try:
                    process.wait(timeout=TIMEOUT)
                except subprocess.TimeoutExpired:
                    print(f"    {test.name}: FAILED (TIMEOUT after {TIMEOUT}s)", flush=True)
                    process.kill()
                    stdout_thread.join()
                    stderr_thread.join()
                    print("    Full STDOUT (captured up to timeout):\n" + "".join(captured_stdout), flush=True)
                    print("    Full STDERR (captured up to timeout):\n" + "".join(captured_stderr), flush=True)
                    if break_on_fail:
                        sys.exit(1)
                    continue

                # Ensure all output has been read from the pipes by joining the threads
                stdout_thread.join()
                stderr_thread.join()

                # --- Step 5: Check the return code of the executed process ---
                return_code = process.returncode

                if return_code != 0:
                    print(f"    {test.name}: FAILED ({command}) (Exit code: {return_code})", flush=True)
                    print("    Full STDOUT:\n" + "".join(captured_stdout), flush=True)
                    print("    Full STDERR:\n" + "".join(captured_stderr), flush=True)
                    if break_on_fail:
                        sys.exit(1)
                    continue

                # --- Step 6: (Optional) Output validation logic ---
                failed = False
                if not failed:
                    print(f"    {test.name}: Finished! Skipping validation", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Test runner')
    parser.add_argument('-b', '--breakonfail', action='store_true', help='break and print stdout on fail')
    parser.add_argument('-l', '--local', action='store_true', help='run tests locally (without slurm)')

    args = parser.parse_args()
    run_tests(args.breakonfail, args.local)
