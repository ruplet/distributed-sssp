import sys
import struct
import os

def read_6byte_uint(f):
    """Reads a 6-byte unsigned int (little-endian)"""
    b = f.read(6)
    if len(b) < 6:
        return None
    return int.from_bytes(b, byteorder='little', signed=False)

def main(edges_path, weights_path, fifo_path):
    with open(edges_path, 'rb') as ef, open(weights_path, 'rb') as wf:
        # Open FIFO for writing
        with open(fifo_path, 'w') as fifo_out:
            while True:
                start = read_6byte_uint(ef)
                if start is None:
                    break  # EOF

                end = read_6byte_uint(ef)
                if end is None:
                    break  # Unexpected EOF

                w_bytes = wf.read(4)
                if len(w_bytes) < 4:
                    break  # Unexpected EOF

                (w,) = struct.unpack('<f', w_bytes)
                weight_int = min(int(w * 256), 255)

                fifo_out.write(f"{start} {end} {weight_int}\n")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <edges_file> <weights_file> <fifo_path>")
        sys.exit(1)

    edges_file = sys.argv[1]
    weights_file = sys.argv[2]
    fifo_path = sys.argv[3]

    # Check that the FIFO exists and is a FIFO
    if not os.path.exists(fifo_path) or not stat.S_ISFIFO(os.stat(fifo_path).st_mode):
        print(f"Error: {fifo_path} is not a named pipe (FIFO)")
        sys.exit(1)

    main(edges_file, weights_file, fifo_path)
