import sys
import struct
import os
import stat
from pathlib import Path

def make_fifos(dir_path, n):
    for i in range(n):
        fifo_path = dir_path / f"{i}.in"
        if not fifo_path.exists():
            os.mkfifo(fifo_path)

def read_6byte_uint(f):
    b = f.read(6)
    if len(b) < 6:
        return None
    return int.from_bytes(b, byteorder='little', signed=False)

def get_owner_process(vertex, num_vertices, num_procs):
    base = num_vertices // num_procs
    extras = num_vertices % num_procs

    acc = 0
    for p in range(num_procs):
        count = base + (1 if p < extras else 0)
        if vertex < acc + count:
            return p
        acc += count
    return num_procs - 1  # fallback, should not happen

def main(edges_path, weights_path, num_vertices, num_procs, fifo_paths):
    # Open all FIFOs for writing
    fifos = []
    base_load = num_vertices // num_procs
    extra = num_vertices % num_procs
    for i, path in enumerate(fifo_paths):
        if not os.path.exists(path) or not stat.S_ISFIFO(os.stat(path).st_mode):
            print(f"Error: {path} is not a named pipe (FIFO)")
            sys.exit(1)
        fifos.append(open(path, 'w'))
        if i < extra:
            first_resp = i * (base_load + 1)
            last_resp = first_resp + (base_load + 1) - 1
        else:
            first_resp = extra * (base_load + 1) + (i - extra) * base_load
            last_resp = first_resp + base_load - 1
        line = f"{num_vertices} {first_resp} {last_resp}\n"
        fifos[-1].write(line)

    with open(edges_path, 'rb') as ef, open(weights_path, 'rb') as wf:
        while True:
            start = read_6byte_uint(ef)
            if start is None:
                break
            end = read_6byte_uint(ef)
            if end is None:
                break
            w_bytes = wf.read(4)
            if len(w_bytes) < 4:
                break
            (w,) = struct.unpack('<f', w_bytes)
            weight_int = min(int(w * 256), 255)

            owner_start = get_owner_process(start, num_vertices, num_procs)
            owner_end   = get_owner_process(end, num_vertices, num_procs)

            line = f"{start} {end} {weight_int}\n"

            # Send to both owners if different
            if owner_start == owner_end:
                fifos[owner_start].write(line)
            else:
                fifos[owner_start].write(line)
                fifos[owner_end].write(line)

    for f in fifos:
        f.close()

if __name__ == '__main__':
    if len(sys.argv) != 7:
        print(f"Usage: {sys.argv[0]} <edges.bin> <weights.bin> <num_vertices> <num_procs> <test_name> <tests_dir>", file=sys.stderr)
        sys.exit(1)

    edges_file = sys.argv[1]
    weights_file = sys.argv[2]
    num_vertices = int(sys.argv[3])
    num_procs = int(sys.argv[4])
    test_name = sys.argv[5]
    tests_dir = Path(sys.argv[6])

    out_dir = tests_dir / f"{test_name}_{num_vertices}_{num_procs}"
    out_dir.mkdir(parents=True, exist_ok=True)

    make_fifos(out_dir, num_procs)
    fifo_paths = fifos = [out_dir / f"{i}.in" for i in range(num_procs)]

    main(edges_file, weights_file, num_vertices, num_procs, fifo_paths)
