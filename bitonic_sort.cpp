// main_bitonic_sort.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <thread>
#include "nlohmann/json.hpp"
#include "oblivious_sort.h"

using json = nlohmann::json;

// Helper: Compute next power of two.
size_t nextPowerOfTwo(size_t n) {
    if (n == 0) return 1;
    size_t power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

int main() {
    // Open ints.json
    std::ifstream ifs("ints.json");
    if (!ifs.is_open()) {
        std::cerr << "Error: Could not open ints.json\n";
        return 1;
    }
    
    // Parse JSON into vector of integers.
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
    
    // Convert integers to Elements.
    std::vector<Element> elements;
    elements.reserve(inputValues.size());
    for (int val : inputValues) {
        Element e;
        e.value = val;
        // For bitonic sort, we want key to reflect the value.
        e.key = val;
        e.is_dummy = false;
        elements.push_back(e);
    }
    
    // Pad the vector to next power of two.
    size_t origSize = elements.size();
    size_t paddedSize = nextPowerOfTwo(origSize);
    if (paddedSize > origSize) {
        for (size_t i = origSize; i < paddedSize; i++) {
            Element dummy;
            dummy.value = 0;
            dummy.key = 0;
            dummy.is_dummy = true;
            elements.push_back(dummy);
        }
    }
    std::cout << "Padded vector size for bitonic sort: " << elements.size() << "\n";
    
    // Create an Enclave.
    UntrustedMemory dummyUntrusted;
    Enclave enclave(&dummyUntrusted);
    
    // Run bitonic sort in a separate thread.
    std::thread sortThread([&enclave, &elements]() {
        enclave.bitonicSort(elements, 0, elements.size(), true);
    });
    sortThread.join();
    
    // Remove dummy elements.
    std::vector<Element> finalElements;
    for (const auto &e : elements) {
        if (!e.is_dummy)
            finalElements.push_back(e);
    }
    
    // Verify sorted order by value.
    bool sorted = std::is_sorted(finalElements.begin(), finalElements.end(), 
        [](const Element& a, const Element& b) {
            return a.value < b.value;
        });
    std::cout << "Final elements sorted by value? " << (sorted ? "Yes" : "No") << "\n";
    
    // Write sorted integers to file.
    std::ofstream ofs("sorted_output_bitonic.json");
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not open sorted_output_bitonic.json for writing\n";
        return 1;
    }
    for (const auto &e : finalElements) {
        ofs << e.value << "\n";
    }
    ofs.close();
    std::cout << "Wrote sorted_output_bitonic.json\n";
    
    return 0;
}
