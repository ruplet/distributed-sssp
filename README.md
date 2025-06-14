# Distributed delta-stepping algorithm for single-source shortest path problem

Code presented here implements the techniques from the paper "Scalable Single Source Shortest Path Algorithms for Massively Parallel Systems". We provide comprehensive testing machinery for correctness at edge cases,
stress testing the limits of your hardware and random average-case tests for performing benchmarks and measuring weak-scaling for code with and without the different optimizations.

## Graph500 generation

I use this implementation: https://github.com/graph500/graph500
I'm unable to compile this code on my local machine, neither on students
But on Okeanos, it works

Here are instructions on how to configure graph generation parameters:
https://github.com/graph500/graph500/blob/newreference/generator/README

in generator/user_settings.h we see
#define GENERATOR_USE_PACKED_EDGE_TYPE /* -- 48 bits per edge */
which means that every weights in the binary output file is 6 bytes wide

in generator/graph_generator.c we have
/* Initiator settings: for faster random number generation, the initiator
 * probabilities are defined as fractions (a = INITIATOR_A_NUMERATOR /
 * INITIATOR_DENOMINATOR, b = c = INITIATOR_BC_NUMERATOR /
 * INITIATOR_DENOMINATOR, d = 1 - a - b - c. */
#define INITIATOR_A_NUMERATOR 5700
#define INITIATOR_BC_NUMERATOR 1900
#define INITIATOR_DENOMINATOR 10000

which tells us how to setup RMAT-1 and RMAT-2 classes of graphs at generation!

from the paper:
RMAT-1: A = 0.57, B = C = 0.19, D = 0.05 (they say it's Graph500 BFS benchmark spec)
RMAT-2: A = 0.50, B = C = 0.1,  D = 0.3  (Graph500 SSSP benchmark spec)
weights in [0, 255]
the graphs are sparse
egde factor = 16, so number of edges = 16 * N
vertices per node = 2^23
number of nodes varied from 32 do 32,768


sed -n '27,29p' ../generator/graph_generator.c && sed '85q;d' main.c && srun -n 1 ./graph500_reference_bfs 5 2 > & log.txt


generalnie to te skrypty jako weights generuja floaty z [0, 1)
a jako edgefile generują po 6 bajtow per node id, po prostu zapisuja na pałę pary
e.g. rozmiar edges.out dla scale=20, edgefactor=500:
2 ^ 20 * 500 * 6 * 2 = 6291456000
czyli takiego rozmiaru oczekujemy od pliku edges.out
a plik .weights powinien mieć
2 ^ 20 * 500 * 4 (zamiast 2 * 6B mamy 1 * sizeof(float) = 4B)
= 2097152000

okeanos-login1 src/out_a5700_bc1900_d10000_s2_3_n20_ef500> ls -al
total 1703952
drwxr-xr-x 2 balawend icm-users       4096 Jun 14 14:02 .
drwxr-xr-x 4 balawend icm-users       4096 Jun 14 14:17 ..
-rw-r--r-- 1 balawend icm-users 6291456000 Jun 14 14:03 edges.out
-rw-r--r-- 1 balawend icm-users 2097152000 Jun 14 14:03 edges.out.weights
-rw-r--r-- 1 balawend icm-users          0 Jun 14 14:02 output.txt

for n=26:
2^26 * 16 * 2 * 6 = 12884901888
edge size: 12884901888
jest git.

scale 18, with validation, takes 21 second with -N4
scale 18, with validation, takes 10 seconds with -n 64
scale 20, with validation, takes 20 seconds with -n 64
scale 24, with validation, takes ? seconds with -n 64

scale 30 for ef 16 would take 192GB of memory
graph generation:
scale 24: 0.000001s
scale 26: 46s?
scale 30: 600s
scale 33 est: 4800s?

graph construction:
for scale 24 is 9.57s
for scale 22 it's 3s
for scale 23 it's 8s
for scale 26 it's 40s

