#include <iostream>
#include <vector>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <alloc_size in GiB>" << std::endl;
        return 1;
    }
    size_t sz = std::stoull(argv[1]);

    size_t n_items = sz * 1024 * 1024 * 1024 / sizeof(size_t);
    std::vector<size_t> vec(n_items, 7);

    std::cout << "Sucessfully allocated and initialized " << sz << "GiB of memory. Last byte: " << vec[n_items - 1] << std::endl;
    return 0;
}