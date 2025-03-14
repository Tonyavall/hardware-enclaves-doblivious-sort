// main_oblivious_bucket_sort.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include "nlohmann/json.hpp"
#include "oblivious_sort.h"

using json = nlohmann::json;

int main() {
    // Open ints.json
    std::ifstream ifs("ints.json");
    if (!ifs.is_open()) {
        std::cerr << "Error: Could not open ints.json\n";
        return 1;
    }
    
    // Parse JSON into a vector of integers.
    json j;
    try {
        ifs >> j;
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
        return 1;
    }
    std::vector<int> inputValues = j.get<std::vector<int>>();
    std::cout << "Loaded " << inputValues.size() << " integers from ints.json.\n";
    
    // Create an UntrustedMemory and Enclave.
    UntrustedMemory untrusted;
    Enclave enclave(&untrusted);
    
    // Choose a bucket size (experiment with this value, e.g. 32 or 64).
    int bucket_size = 32;
    std::cout << "Starting oblivious bucket sort with bucket size " << bucket_size << "...\n";
    
    std::vector<int> sortedOblivious = enclave.oblivious_sort(inputValues, bucket_size);
    
    std::ofstream ofs("sorted_output_oblivious.json");
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not open sorted_output_oblivious.json for writing\n";
        return 1;
    }
    for (int val : sortedOblivious) {
        ofs << val << "\n";
    }
    ofs.close();
    std::cout << "Wrote sorted_output_oblivious.json\n";
    
    return 0;
}
