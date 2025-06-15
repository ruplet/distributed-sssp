#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>

bool allocate_and_touch(size_t gb) {
    size_t bytes = gb * size_t(1024) * 1024 * 1024;
    size_t page_size = sysconf(_SC_PAGESIZE);

    std::cout << "System page size: " << page_size << " bytes\n";
    std::cout << "Allocating " << gb << " GB (" << bytes << " bytes)\n";

    // Allocate anonymous memory
    void* addr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return false;
    }

    std::cout << "Mapped address: " << addr << "\n";

    // Try to lock memory to prevent swapping (optional)
    if (mlock(addr, bytes) != 0) {
        perror("mlock");
        std::cerr << "Warning: mlock failed, memory might be swapped\n";
    }

    // Write value 42 to the first byte of every page
    char* p = static_cast<char*>(addr);
    for (size_t i = 0; i < bytes; i += page_size) {
        p[i] = 42;
    }

    // Access last page and print value
    size_t last_offset = bytes - page_size;
    char last_value = p[last_offset];
    std::cout << "Value at start of last page: " << static_cast<int>(last_value) << "\n";

    // Clean up
    munlock(addr, bytes);
    munmap(addr, bytes);

    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <alloc_size in GiB>" << std::endl;
        return 1;
    }
    size_t gb = std::stoull(argv[1]);
    
    if (!allocate_and_touch(gb)) {
        std::cerr << "Memory allocation or page touch failed\n";
        return 1;
    }
    return 0;
}
