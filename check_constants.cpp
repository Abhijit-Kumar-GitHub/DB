#include <iostream>
#include "db.hpp"

int main() {
    std::cout << "=== B-Tree Configuration ===" << std::endl;
    std::cout << "PAGE_SIZE: " << PAGE_SIZE << std::endl;
    std::cout << "LEAF_NODE_HEADER_SIZE: " << LEAF_NODE_HEADER_SIZE << std::endl;
    std::cout << "LEAF_NODE_CELL_SIZE: " << LEAF_NODE_CELL_SIZE << std::endl;
    std::cout << "LEAF_NODE_MAX_CELLS: " << LEAF_NODE_MAX_CELLS << std::endl;
    std::cout << "LEAF_NODE_MIN_CELLS: " << LEAF_NODE_MIN_CELLS << std::endl;
    std::cout << std::endl;
    std::cout << "INTERNAL_NODE_HEADER_SIZE: " << INTERNAL_NODE_HEADER_SIZE << std::endl;
    std::cout << "INTERNAL_NODE_CELL_SIZE: " << INTERNAL_NODE_CELL_SIZE << std::endl;
    std::cout << "INTERNAL_NODE_MAX_KEYS: " << INTERNAL_NODE_MAX_KEYS << std::endl;
    std::cout << "INTERNAL_NODE_MIN_KEYS: " << INTERNAL_NODE_MIN_KEYS << std::endl;
    return 0;
}
