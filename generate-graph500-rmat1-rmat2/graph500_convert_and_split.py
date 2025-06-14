import sys
import struct
import os
import stat
import shutil
from pathlib import Path


def prepare_outputs(dir_path, n, reuse_files):
    paths = []
    outputs = []

    for i in range(n):
        path = dir_path / f"{i}.in"
        paths.append(path)

        if not path.exists():
            if reuse_files:
                path.touch()
            else:
                os.mkfifo(path)

        if not reuse_files and not stat.S_ISFIFO(path.stat().st_mode):
            print(f"Error: {path} is not a FIFO")
            sys.exit(1)

        outputs.append(open(path, "w"))

    return paths, outputs


def remove_fifos(paths):
    for path in paths:
        try:
            if path.exists() and stat.S_ISFIFO(path.stat().st_mode):
                path.unlink()
        except Exception as e:
            print(f"Warning: could not remove FIFO {path}: {e}", file=sys.stderr)


def read_6byte_uint(f, scale):
    b = f.read(6)
    if len(b) < 6:
        return None
    val = int.from_bytes(b, byteorder="little", signed=False)
    mask = (1 << scale) - 1
    return val & mask


def get_owner_process(vertex, num_vertices, num_procs):
    base = num_vertices // num_procs
    extras = num_vertices % num_procs

    # Vertices [0, extras * (base + 1)) are assigned one extra
    threshold = extras * (base + 1)
    if vertex < threshold:
        return vertex // (base + 1)
    else:
        return extras + (vertex - threshold) // base


def main(edges_path, weights_path, scale, num_procs, outputs):
    num_vertices = 2 ** scale
    base_load = num_vertices // num_procs
    extra = num_vertices % num_procs

    for i in range(num_procs):
        if i < extra:
            first_resp = i * (base_load + 1)
            last_resp = first_resp + (base_load + 1) - 1
        else:
            first_resp = extra * (base_load + 1) + (i - extra) * base_load
            last_resp = first_resp + base_load - 1
        outputs[i].write(f"{num_vertices} {first_resp} {last_resp}\n")

    with open(edges_path, "rb") as ef, open(weights_path, "rb") as wf:
        while True:
            start = read_6byte_uint(ef, scale)
            if start is None:
                break
            end = read_6byte_uint(ef, scale)
            if end is None:
                break
            w_bytes = wf.read(4)
            if len(w_bytes) < 4:
                break
            (w,) = struct.unpack("<f", w_bytes)
            weight_int = min(int(w * 256), 255)

            owner_start = get_owner_process(start, num_vertices, num_procs)
            owner_end = get_owner_process(end, num_vertices, num_procs)

            line = f"{start} {end} {weight_int}\n"
            outputs[owner_start].write(line)
            if owner_start != owner_end:
                outputs[owner_end].write(line)

    for f in outputs:
        f.close()


if __name__ == "__main__":
    if len(sys.argv) != 6:
        print(
            f"Usage: {sys.argv[0]} <edges_folder> <scale> <num_procs> <tests_dir> <reuse_files>",
            file=sys.stderr,
        )
        sys.exit(1)

    edges_folder = Path(sys.argv[1])
    edges_file = edges_folder / "edges.out"
    weights_file = edges_folder / "edges.out.weights"
    scale = int(sys.argv[2])
    num_procs = int(sys.argv[3])
    tests_dir = Path(sys.argv[4])
    reuse_files_raw = sys.argv[5].strip().lower()

    if reuse_files_raw == 'reuse':
        reuse_files = True
    elif reuse_files_raw == 'noreuse':
        reuse_files = False
    else:
        print(f"<reuse_files> must be 'reuse' or 'noreuse', got: {reuse_files_raw}")
        sys.exit(1)

    out_dir = tests_dir / f"graph500-scale-{scale}_{2 ** scale}_{num_procs}"
    out_dir.mkdir(parents=True, exist_ok=True)

    fifo_paths, outputs = prepare_outputs(out_dir, num_procs, reuse_files)

    main(edges_file, weights_file, scale, num_procs, outputs)

    if not reuse_files:
        remove_fifos(fifo_paths)
        try:
            shutil.rmtree(out_dir)
        except Exception as e:
            print(f"Warning: could not remove directory {out_dir}: {e}", file=sys.stderr)
