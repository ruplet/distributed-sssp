#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <cstdint>
#include <cstdlib>  // for std::atoll
#include <fstream>  // For file output
#include <limits>
#include <string>   // For std::to_string
#include <sys/stat.h>
#include <sys/types.h>

int main(int argc, char** argv) {
    // Modified to accept 4 arguments: <number_of_vertices> <num_processes>
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <number_of_vertices> <num_processes>\n";
        return 1;
    }

    int64_t n = std::atoll(argv[1]);
    int64_t TOTAL_MAX = std::numeric_limits<int64_t>::max(); //std::atoll(argv[2]);
    int num_processes = std::atoi(argv[2]); // New argument for number of processes

    if (n < 2) {
        std::cerr << "Number of vertices must be at least 2.\n";
        return 1;
    }
    if (TOTAL_MAX <= 0) {
        std::cerr << "TOTAL_MAX must be positive.\n";
        return 1;
    }
    if (num_processes <= 0) {
        std::cerr << "Number of processes must be at least 1.\n";
        return 1;
    }
    // It's usually fine for num_processes > n for certain distribution types,
    // but for block distribution, it might lead to empty blocks.
    // For this ring graph context, it's simpler to assume num_processes <= n.
    if (num_processes > n) {
        std::cerr << "Warning: Number of processes (" << num_processes 
                  << ") is greater than the number of vertices (" << n << "). "
                  << "Some process files will be empty.\n";
    }

    const int batch_size = 10000;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, 10);

    // Step 1: Generate random weights in batches [1..10]
    // These weights represent the edge (i, (i+1)%n)
    std::vector<int64_t> weights(n);
    int64_t generated = 0;
    while (generated < n) {
        int64_t batch_end = std::min(generated + batch_size, n);
        for (int64_t i = generated; i < batch_end; ++i) {
            weights[i] = dist(gen);
        }
        generated = batch_end;
    }

    // Step 2: Compute prefix sums (clockwise distances)
    std::vector<int64_t> prefix(n + 1, 0);
    // int64_t tipping_point = 0;
    for (int64_t i = 0; i < n; ++i) {
        prefix[i + 1] = prefix[i] + weights[i];
    }

    int64_t current_total = prefix[n];

    // Step 3: Find tipping point vertex using binary search
    int64_t left = 0, right = n;
    int64_t tipping_point = n;  // default to n if none found
    while (left <= right) {
        int64_t mid = left + (right - left) / 2;
        int64_t cw_dist = prefix[mid];
        int64_t ccw_dist = current_total - cw_dist;
        if (cw_dist >= ccw_dist) {
            tipping_point = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    // Step 4: Adjust edge weight to reach TOTAL_MAX
    int64_t diff = TOTAL_MAX - current_total;

    // Edge to adjust: edge from (tipping_point -1) -> tipping_point (mod n)
    // This edge's weight is stored at weights[(tipping_point - 1 + n) % n]
    int64_t adjust_index = (tipping_point - 1 + n) % n;

    int64_t new_weight = weights[adjust_index] + diff;

    if (new_weight < 0) {
        std::cerr << "Error: adjusted weight is negative! Cannot fix total to TOTAL_MAX.\n";
        return 1;
    }

    weights[adjust_index] = new_weight;
    current_total += diff;

    // Output for verification
    std::cout << "Graph size (n): " << n << "\n";
    std::cout << "TOTAL_MAX: " << TOTAL_MAX << "\n";
    std::cout << "Original total sum of edges (before adjustment): " << (current_total - diff) << "\n";
    std::cout << "Tipping point vertex: " << tipping_point << "\n";
    // The adjusted edge is (adjust_index -> (adjust_index + 1) % n)
    std::cout << "Adjusted edge (source vertex index): " << adjust_index << "\n";
    std::cout << "New weight of adjusted edge: " << weights[adjust_index] << "\n";
    std::cout << "New total sum of edges: " << current_total << "\n\n";

    // --- Outputting data to files for BLOCK distribution ---
    std::cout << "Distributing edges to " << num_processes << " files based on BLOCK distribution...\n";

    // Calculate block sizes for uneven distribution
    int64_t base_block_size = n / num_processes;
    int64_t extra_vertices = n % num_processes; // These processes get one extra vertex
    int64_t current_vertex_start = 0;

    std::string dirname = "bigcycle_" + std::to_string(n) + "_" + std::to_string(num_processes);
    struct stat st = {0};
    if (stat(dirname.c_str(), &st) == -1) {
        if (mkdir(dirname.c_str(), 0755) != 0) {
            std::cerr << "Error: Failed to create directory '" << dirname << "'.\n";
            return 1;
        }
    }

    for (int p_id = 0; p_id < num_processes; ++p_id) {
        std::string filename = std::string("bigcycle_") + std::to_string(n) + "_" + std::to_string(num_processes) + "/" + std::to_string(p_id) + ".in";
        std::ofstream outfile(filename);

        std::ofstream resultfile(std::string("bigcycle_") + std::to_string(n) + "_" + std::to_string(num_processes) + "/" + std::to_string(p_id) + ".out");

        if (!outfile.is_open()) {
            std::cerr << "Error: Could not open file " << filename << " for writing.\n";
            return 1; // Exit if file cannot be opened
        }
        if (!resultfile.is_open()) {
            std::cerr << "Error: Could not open file " << std::to_string(p_id) + ".out" << " for writing.\n";
            return 1; // Exit if file cannot be opened
        }

        // Determine the number of vertices for this process's block
        int64_t block_length = base_block_size;
        if (p_id < extra_vertices) {
            block_length++;
        }

        // Determine the start and end vertex indices for this process
        int64_t start_v = current_vertex_start;
        int64_t end_v = current_vertex_start + block_length - 1; // Inclusive

        // outfile << "# Edges for Process " << p_id << " (responsible for vertices " 
        //         << start_v << " to " << end_v << ")\n";
        // outfile << "# Format: source_vertex destination_vertex weight\n";


        // Edge from (v-1+n)%n to v. Its weight is weights[(v-1+n)%n].
        outfile << n << " " << start_v << " " << end_v << "\n";
        int64_t v_prev = (start_v - 1 + n) % n;
        outfile << v_prev << " " << start_v << " " << weights[v_prev] << "\n";
        // Iterate through all vertices assigned to this process's block
        for (int64_t v = start_v; v <= end_v; ++v) {
            if (v >= n) { // Handle cases where num_processes > n leading to v out of bounds
                continue;
            }

            // Edge from v to (v+1)%n. Its weight is weights[v].
            int64_t v_next = (v + 1) % n;
            outfile << v << " " << v_next << " " << weights[v] << "\n";

            if (v <= adjust_index) {
                // go to left, output corresponding sum from prefix sums
                resultfile << prefix[v] << "\n";
                // go to right, output total sum of prefix array (not including added edge) - left side
            } else {
                resultfile << prefix[n] - prefix[v] << "\n";
            }
        }
        
        outfile.close();
        std::cout << "Generated " << filename << " for vertices " << start_v << " to " << end_v << ".\n";
        
        // Update the starting vertex for the next process's block
        current_vertex_start += block_length;
    }

    std::cout << "\nFile generation complete.\n";

    return 0;
}