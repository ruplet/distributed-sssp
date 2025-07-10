import re
from pprint import pprint
import sys

def extract_graph_name(line):
    # Match a token (non-whitespace) that contains '-bench-' and ends with a colon
    match = re.search(r'(\S*-bench-\S*?)(?=[:\s])', line)
    if match:
        return match.group(1)
    return None

def parse_metrics_from_log(log_path):
    results = {}
    current_test = None
    collecting = False
    metrics = {}

    with open(log_path, 'r') as f:
        for line in f:
            # line = line.strip()

            # Detect skipped tests
            # if line.startswith("Skipping:"):
            #     current_test = None
            #     collecting = False
            #     continue

            # Detect running test
            # match = re.match(r"Running: (\S+)", line)
            # if match:
            #     current_test = match.group(1)
            #     metrics = {}
            #     collecting = True
            #     continue

            # Detect `srun` line
            # if 'srun' in line and "test_command.sh" in line:
                # Extract last argument as test name
            graph_name = extract_graph_name(line)
            if graph_name:
                # current_entry["graph"] = graph_name
            # parts = line.split()
            # if parts:
                # current_test = parts[-1].rstrip('/')
                current_test = graph_name
                metrics = {}
                collecting = True
                continue

            # Parse metrics if collecting
            if collecting:
                if "Finished!" in line:
                    continue

                time_match = re.match(r"Time: ([\d.]+)s", line)
                if time_match:
                    metrics['time'] = float(time_match.group(1))

                parse_time = re.match(r"Parsing data took: ([\d.]+)s", line)
                if parse_time:
                    metrics['parsing_time'] = float(parse_time.group(1))

                short_relax = re.match(r"Short relaxations: (\d+)", line)
                if short_relax:
                    metrics['short_relax'] = int(short_relax.group(1))

                bypassed = re.match(r"from which bypassed: (\d+)", line)
                if bypassed:
                    metrics['bypassed'] = int(bypassed.group(1))

                long_relax = re.match(r"Long relaxations: (\d+)", line)
                if long_relax:
                    metrics['long_relax'] = int(long_relax.group(1))

                total_phases = re.match(r"Total phases: (\d+)", line)
                if total_phases:
                    metrics['phases'] = int(total_phases.group(1))
                    results[current_test] = metrics
                    collecting = False

                # last_phase = re.match(r"Last phase before bellman: (\d+)", line)
                # if last_phase:
                #     metrics['last_phase_bf'] = int(last_phase.group(1))
                #     results[current_test] = metrics
                #     collecting = False

                # total_phases = re.match(r"Total phases: (\d+)", line)
                # if total_phases:
                #     metrics['phases'] = int(total_phases.group(1))

                # last_phase = re.match(r"Last phase before bellman: (\d+)", line)
                # if last_phase:
                #     metrics['last_phase_bf'] = int(last_phase.group(1))
                #     results[current_test] = metrics
                #     collecting = False

    return results

# Example usage
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    log_file_path = sys.argv[1]
    metrics_dict = parse_metrics_from_log(log_file_path)
    pprint(metrics_dict)
