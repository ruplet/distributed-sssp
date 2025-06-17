# Distributed delta-stepping algorithm for single-source shortest path problem

Code presented here implements the techniques from the paper "Scalable Single Source Shortest Path Algorithms for Massively Parallel Systems". We provide comprehensive testing machinery for correctness at edge cases,
stress testing the limits of your hardware and random average-case tests for performing benchmarks and measuring weak-scaling for code with and without the different optimizations.


## Data generation: Graph500 distribution
We faciliate experimenting on graphs generated in the same way as in the Graph500 benchmark.
We use the reference implementation: https://github.com/graph500/graph500
I was unable to compile this neither on my local machine nor on the `students` server of MIMUW.
Fortunately, it works on Okeanos.

A lot of work is required to create sensible tests from the data generated there.
I provide a C++ program to split the graph data generated there into input files in our format.
Additionaly, using a reference Dijkstra implementation using the NetworkX library in Python,
we can compute the true optimal distances for smaller instances.
Still, the generated graphs are:
- disconnected
- have multi-edges
- have self-loops
which rather needs to be just handled by the distributed sssp program.

The code for this section is in the directory `generate-graph500-rmat1-rmat2`.

## Data generation: Huge cycles
As an edge-case, to test the behavior of the program on instances with huge number of vertices
and huge weights, but not memory-heavy (so, sparse), we provide in directory `generate-huge-cycle`
a program to generate a valid input for the problem consisting of a huge cycle-graph,
with small, random weights except one - which is `int64_t::max() - sum of other edges`.
This tests the behaviour of the program with total weight sum approaching the numerical limit.

## Data generation: Random connected graphs
As we originally only wanted to consider connected graphs, and most easy ways to create
a random graph generate a disconnected one, we start by generating (again using `NetworkX`)
a random `n`-ary tree, to which we then add random edges between any two existing nodes.
These are the "nicest" tests we present here, as they contain no self-loops, no multi-edges,
are connected and have a very well-defined number of vertices and edges.


## Hardware

At ICM, Okeanos has 1084 computational nodes.
It is Cray XC40 with Cray Aries network with dragonfly topology.
Each node has 24 cores (2x 12-core Intel Xeon E5-2690 v3 Haswell) and 128GB of RAM
hyperthreading x2 possible
lustre file system
suse linux enterprise server os
slurm queue system


# Correctness
We thoroughly verified the correctness of the code.
Unoptimized program (plain delta-stepping) and code with different optimizations enabled was tested against:

RandomTree:
Arity Height NProc EF
2   10  5 2-16
2 15 768 2 x10 seeds
Arity 3, Height 6 (1093 nodes), 5 procs, x10 seeds
Arity 2, Height 7 (255 nodes), ef 2, procs = 1 to 42
Arity 3, H 9, E10, Nproc 768 x10 seeds

BigCycle: 


