#include <iostream>      // For standard input/output operations (std::cout, std::cerr)
#include <fstream>       // For file stream operations (std::ifstream, std::ofstream)
#include <vector>        // For dynamic arrays (std::vector)
#include <string>        // For string manipulation (std::string, std::to_string)
#include <cmath>         // For mathematical functions like pow (used for 2^scale, though bit shift is preferred)
#include <filesystem>    // For path manipulation and directory operations (std::filesystem::path, exists, create_directories, remove_all) (C++17)
#include <sys/stat.h>    // For stat() and S_ISFIFO macro to check file type (Unix/Linux specific)
#include <fcntl.h>       // For open(), mkfifo() for named pipes (Unix/Linux specific)
#include <unistd.h>      // For unlink() to delete files/FIFOs (Unix/Linux specific)
#include <memory>        // For smart pointers like std::unique_ptr to manage file streams
#include <optional>      // For std::optional to represent potentially absent values (e.g., end of file) (C++17)
#include <cstring>       // For memcpy to copy raw bytes
#include <utility>       // For std::pair and std::move

/**
 * @brief Reads a 6-byte unsigned integer from a binary input stream in little-endian format.
 * Applies a bitmask based on the given scale.
 *
 * @param f The input file stream.
 * @param scale The scale value used to create the bitmask.
 * @return An optional containing the read and masked 64-bit unsigned integer,
 * or std::nullopt if fewer than 6 bytes could be read (e.g., end of file).
 */
std::optional<uint64_t> read_6byte_uint(std::ifstream& f, int scale) {
    char bytes[6];
    f.read(bytes, 6); // Attempt to read 6 bytes
    if (f.gcount() < 6) { // Check if 6 bytes were actually read
        return std::nullopt; // Indicate end of file or read error
    }

    uint64_t val = 0;
    // Reconstruct the 64-bit unsigned integer from little-endian bytes
    for (int i = 0; i < 6; ++i) {
        val |= (static_cast<uint64_t>(static_cast<unsigned char>(bytes[i])) << (i * 8));
    }

    // Apply the bitmask: (1ULL << scale) - 1
    // 1ULL ensures the shift is performed on an unsigned long long to avoid overflow for large scales.
    uint64_t mask = (1ULL << scale) - 1;
    return val & mask;
}

/**
 * @brief Determines the "owner" process for a given vertex based on a distribution strategy.
 * This logic directly mirrors the Python script's `get_owner_process` function.
 *
 * @param vertex The vertex ID.
 * @param num_vertices The total number of vertices.
 * @param num_procs The total number of processes.
 * @return The index of the owner process for the given vertex.
 */
int get_owner_process(uint64_t vertex, uint64_t num_vertices, int num_procs) {
    uint64_t base = num_vertices / num_procs;
    uint64_t extras = num_vertices % num_procs;

    // Vertices [0, extras * (base + 1)) are assigned one extra load
    uint64_t threshold = extras * (base + 1);
    if (vertex < threshold) {
        return static_cast<int>(vertex / (base + 1));
    } else {
        // Remaining vertices are distributed among the rest of the processes with base_load
        return static_cast<int>(extras + (vertex - threshold) / base);
    }
}

/**
 * @brief Prepares the output files or named pipes (FIFOs) for writing.
 * This function handles creating the necessary files/FIFOs and opening them.
 *
 * @param dir_path The directory where output files/FIFOs should be created.
 * @param n The number of output files/FIFOs to create.
 * @param reuse_files If true, existing files are "touched" (created if not exist, otherwise left),
 * if false, FIFOs are created.
 * @return A pair containing:
 * - A vector of `std::filesystem::path` objects for the created files/FIFOs.
 * - A vector of `std::unique_ptr<std::ofstream>` objects, managing the open output streams.
 */
std::pair<std::vector<std::filesystem::path>, std::vector<std::unique_ptr<std::ofstream>>>
prepare_outputs(const std::filesystem::path& dir_path, int n, bool reuse_files) {
    std::vector<std::filesystem::path> paths;
    std::vector<std::unique_ptr<std::ofstream>> outputs;

    for (int i = 0; i < n; ++i) {
        std::filesystem::path path = dir_path / (std::to_string(i) + ".in");
        paths.push_back(path);

        if (!std::filesystem::exists(path)) {
            if (reuse_files) {
                // If reusing files, simply create an empty file if it doesn't exist.
                // This is equivalent to Python's path.touch()
                std::ofstream{path}.close(); // Create and immediately close to "touch"
            } else {
                // Create a FIFO (named pipe) with read/write permissions for owner/group/others (0666)
                if (mkfifo(path.c_str(), 0666) == -1) {
                    // EEXIST means the FIFO already exists, which is acceptable
                    if (errno != EEXIST) {
                        std::cerr << "Error: Failed to create FIFO " << path << ": " << strerror(errno) << std::endl;
                        exit(1);
                    }
                }
            }
        }

        // If not reusing files, verify that the created path is indeed a FIFO.
        if (!reuse_files) {
            struct stat st; // Structure to hold file status information
            if (stat(path.c_str(), &st) == -1) { // Get file status
                std::cerr << "Error: Failed to stat " << path << ": " << strerror(errno) << std::endl;
                exit(1);
            }
            if (!S_ISFIFO(st.st_mode)) { // Check if the file mode indicates a FIFO
                std::cerr << "Error: " << path << " is not a FIFO" << std::endl;
                exit(1);
            }
        }

        // Open the file/FIFO for writing. std::ios_base::out is the default.
        // For FIFOs, this open call will block until a reader opens the other end.
        outputs.push_back(std::make_unique<std::ofstream>(path));
        if (!outputs.back()->is_open()) {
            std::cerr << "Error: Could not open output file/FIFO " << path << std::endl;
            exit(1);
        }
    }
    // --- FIX APPLIED HERE ---
    // Explicitly move outputs into the pair to guarantee move semantics.
    return {paths, std::move(outputs)};
}

/**
 * @brief Removes named pipes (FIFOs) created by the script.
 * It safely checks if a path exists and is a FIFO before attempting to unlink it.
 *
 * @param paths A vector of `std::filesystem::path` objects representing the FIFOs to remove.
 */
void remove_fifos(const std::vector<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        try {
            struct stat st;
            // Check if path exists and is a FIFO before calling unlink
            if (std::filesystem::exists(path) && stat(path.c_str(), &st) != -1 && S_ISFIFO(st.st_mode)) {
                if (unlink(path.c_str()) == -1) {
                    std::cerr << "Warning: could not remove FIFO " << path << ": " << strerror(errno) << std::endl;
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Warning: could not remove FIFO " << path << ": " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Warning: could not remove FIFO " << path << ": " << e.what() << std::endl;
        }
    }
}

/**
 * @brief Main logic for processing graph edge and weight data.
 * Reads from binary edge and weight files, distributes processed lines to output files/FIFOs.
 * This function encapsulates the core logic of the Python script's `main` function.
 *
 * @param edges_path The path to the binary edges file.
 * @param weights_path The path to the binary weights file.
 * @param scale The scale parameter for graph properties.
 * @param num_procs The number of processes for distribution.
 * @param outputs A vector of unique pointers to output streams (files/FIFOs).
 */
void process_graph_data(
    const std::filesystem::path& edges_path,
    const std::filesystem::path& weights_path,
    int scale,
    int num_procs,
    std::vector<std::unique_ptr<std::ofstream>>& outputs
) {
    // Calculate total number of vertices (2^scale)
    uint64_t num_vertices = 1ULL << scale; // Using 1ULL for proper type promotion in bit shift

    // Write initial header line to each output file/FIFO
    for (int i = 0; i < num_procs; ++i) {
        uint64_t first_resp, last_resp;
        uint64_t base_load = num_vertices / num_procs;
        uint64_t extra = num_vertices % num_procs;

        if (i < extra) {
            first_resp = i * (base_load + 1);
            last_resp = first_resp + (base_load + 1) - 1;
        } else {
            first_resp = extra * (base_load + 1) + (i - extra) * base_load;
            last_resp = first_resp + base_load - 1;
        }
        *outputs[i] << num_vertices << " " << first_resp << " " << last_resp << "\n";
    }

    // Open binary input files
    std::ifstream edges_file(edges_path, std::ios::binary);
    std::ifstream weights_file(weights_path, std::ios::binary);

    // Error handling for opening input files
    if (!edges_file.is_open()) {
        std::cerr << "Error: Could not open edges file: " << edges_path << std::endl;
        exit(1);
    }
    if (!weights_file.is_open()) {
        std::cerr << "Error: Could not open weights file: " << weights_path << std::endl;
        exit(1);
    }

    // Main loop for reading edge and weight data
    while (true) {
        // Read start vertex (6-byte unsigned int)
        std::optional<uint64_t> start_opt = read_6byte_uint(edges_file, scale);
        if (!start_opt) { // Check if read was successful (not EOF)
            break;
        }
        uint64_t start = *start_opt;

        // Read end vertex (6-byte unsigned int)
        std::optional<uint64_t> end_opt = read_6byte_uint(edges_file, scale);
        if (!end_opt) { // Check if read was successful (not EOF)
            std::cerr << "Warning: Unexpected EOF in edges file after reading start vertex." << std::endl;
            break;
        }
        uint64_t end = *end_opt;

        char w_bytes[4];
        weights_file.read(w_bytes, 4); // Read 4 bytes for the float weight
        if (weights_file.gcount() < 4) { // Check if 4 bytes were actually read
            std::cerr << "Warning: Unexpected EOF in weights file." << std::endl;
            break;
        }

        float w;
        // Directly copy bytes to float. This assumes the float representation
        // on the system matches the one in the binary file (typically IEEE 754).
        std::memcpy(&w, w_bytes, 4);

        // Convert float weight to integer (0-255 range)
        int weight_int = static_cast<int>(w * 256) % 256;

        // Determine owner processes for start and end vertices
        int owner_start = get_owner_process(start, num_vertices, num_procs);
        int owner_end = get_owner_process(end, num_vertices, num_procs);

        // Construct the output line
        std::string line = std::to_string(start) + " " + std::to_string(end) + " " + std::to_string(weight_int) + "\n";

        // Write the line to the output stream of the start vertex's owner
        *outputs[owner_start] << line;

        // If the owner processes are different, also write to the end vertex's owner
        if (owner_start != owner_end) {
            *outputs[owner_end] << line;
        }
    }

    // Explicitly close output streams. While unique_ptr will close them on destruction,
    // this mirrors the Python script's explicit closing. Input files will close automatically.
    for (auto& f_ptr : outputs) {
        if (f_ptr && f_ptr->is_open()) {
            f_ptr->close();
        }
    }
}

/**
 * @brief Main entry point of the C++ program.
 * Parses command-line arguments, sets up directories and files,
 * calls the core processing logic, and performs cleanup.
 */
int main(int argc, char* argv[]) {
    // Validate command-line arguments
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <edges_folder> <scale> <num_procs> <tests_dir> <reuse_files>" << std::endl;
        std::cerr << "  <reuse_files> must be 'reuse' or 'noreuse'" << std::endl;
        return 1; // Indicate error
    }

    // Parse command-line arguments
    std::filesystem::path edges_folder = argv[1];
    std::filesystem::path edges_file = edges_folder / "edges.out";
    std::filesystem::path weights_file = edges_folder / "edges.out.weights";
    int scale = std::stoi(argv[2]);        // Convert string to integer
    int num_procs = std::stoi(argv[3]);    // Convert string to integer
    std::filesystem::path tests_dir = argv[4];
    std::string reuse_files_raw = argv[5];

    bool reuse_files;
    // Determine the value of reuse_files based on the argument string
    if (reuse_files_raw == "reuse") {
        reuse_files = true;
    } else if (reuse_files_raw == "noreuse") {
        reuse_files = false;
    } else {
        std::cerr << "<reuse_files> must be 'reuse' or 'noreuse', got: " << reuse_files_raw << std::endl;
        return 1; // Indicate invalid argument
    }

    // Construct the output directory path
    std::filesystem::path out_dir = tests_dir / ("graph500-scale-" + std::to_string(scale) + "_" +
                                                  std::to_string(static_cast<uint64_t>(1ULL << scale)) + "_" +
                                                  std::to_string(num_procs));
    
    // Create the output directory and any necessary parent directories (equivalent to mkdir -p)
    try {
        std::filesystem::create_directories(out_dir);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Could not create directory " << out_dir << ": " << e.what() << std::endl;
        return 1; // Indicate error
    }

    // Vectors to hold paths and unique pointers to output file streams
    // Using C++17 structured bindings to directly capture the elements of the returned pair.
    // This correctly handles the move semantics for std::unique_ptr.
    auto [fifo_paths, outputs] = prepare_outputs(out_dir, num_procs, reuse_files);

    // Process the graph data
    process_graph_data(edges_file, weights_file, scale, num_procs, outputs);

    // Cleanup: remove FIFOs and the output directory if not reusing files
    if (!reuse_files) {
        remove_fifos(fifo_paths); // Remove created FIFOs
        try {
            // Remove the output directory and its contents (equivalent to shutil.rmtree)
            std::filesystem::remove_all(out_dir);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Warning: could not remove directory " << out_dir << ": " << e.what() << std::endl;
        }
    }

    return 0; // Indicate successful execution
}
