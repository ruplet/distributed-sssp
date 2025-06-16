#include <fstream>
#include <sstream>
#include <string>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./append_edges N\n";
        return 1;
    }

    int n = std::stoi(argv[1]);

    for (int i = 0; i < n; ++i) {
        std::string filename = std::to_string(i) + ".in";

        // Open to read first line
        std::ifstream in(filename);
        if (!in.is_open()) {
            std::cerr << "Could not open " << filename << " for reading.\n";
            continue;
        }

        std::string header;
        std::getline(in, header);
        in.close(); // Only needed the first line

        std::istringstream iss(header);
        int total_vertices, my_first, my_last;
        if (!(iss >> total_vertices >> my_first >> my_last)) {
            std::cerr << "Malformed header in " << filename << "\n";
            continue;
        }

        // Open to append
        std::ofstream out(filename, std::ios::app);
        if (!out.is_open()) {
            std::cerr << "Could not open " << filename << " for appending.\n";
            continue;
        }

        // Append edges to connect my_first to my_last
        for (int u = my_first; u < my_last; ++u) {
            out << u << " " << (u + 1) << " 100\n";
        }

        if (my_last < total_vertices - 1) {
            out << my_last << " " << (my_last + 1) << " 100\n";
        } else {
            out << my_last << " 0 100\n";
        }

        out.close();
    }

    return 0;
}
