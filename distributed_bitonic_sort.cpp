// main_distributed_bitonic_sort.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include <algorithm>
#include <cmath>
#include "nlohmann/json.hpp"
#include "oblivious_sort.h"

using json = nlohmann::json;

// Helper: Compute next power of two.
size_t nextPowerOfTwo(size_t n) {
    size_t power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

// Helper: Merge two sorted vectors (by value) and split into lower and upper halves.
std::pair<std::vector<Element>, std::vector<Element>> distributedMerge(
    const std::vector<Element>& a,
    const std::vector<Element>& b)
{
    std::vector<Element> merged(a.size() + b.size());
    std::merge(a.begin(), a.end(), b.begin(), b.end(), merged.begin(),
        [](const Element& e1, const Element& e2) {
            return e1.value < e2.value;
        });
    size_t total = merged.size();
    size_t half = total / 2;
    std::vector<Element> lower(merged.begin(), merged.begin() + half);
    std::vector<Element> upper(merged.begin() + half, merged.end());
    return { lower, upper };
}

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

    // Partition the data among multiple enclaves.
    const int numEnclaves = 4;  // Must be power of two.
    size_t totalRows = inputValues.size();
    size_t rowsPerEnclave = totalRows / numEnclaves;
    size_t remainder = totalRows % numEnclaves;

    std::vector< std::vector<int> > partitions(numEnclaves);
    size_t index = 0;
    for (int i = 0; i < numEnclaves; i++) {
        size_t count = rowsPerEnclave + (i < remainder ? 1 : 0);
        partitions[i].reserve(count);
        for (size_t j = 0; j < count; j++) {
            partitions[i].push_back(inputValues[index++]);
        }
    }

    // For each partition, convert to a vector of Elements.
    std::vector< std::vector<Element> > enclaveData(numEnclaves);
    UntrustedMemory dummyUntrusted;
    std::vector<Enclave> enclaves;
    for (int i = 0; i < numEnclaves; i++) {
        enclaves.emplace_back(&dummyUntrusted);
        for (int val : partitions[i]) {
            Element e;
            e.value = val;
            // Set key equal to the value for local sort.
            e.key = val;
            e.is_dummy = false;
            enclaveData[i].push_back(e);
        }
        // Pad to next power of two.
        size_t origSize = enclaveData[i].size();
        size_t paddedSize = nextPowerOfTwo(origSize);
        if (paddedSize > origSize) {
            for (size_t j = origSize; j < paddedSize; j++) {
                Element dummy;
                dummy.value = 0;
                dummy.key = 0;
                dummy.is_dummy = true;
                enclaveData[i].push_back(dummy);
            }
        }
    }

    // Perform local bitonic sort in each enclave concurrently.
    std::vector<std::thread> threads;
    for (int i = 0; i < numEnclaves; i++) {
        threads.push_back(std::thread([i, &enclaves, &enclaveData]() {
            enclaves[i].bitonicSort(enclaveData[i], 0, enclaveData[i].size(), true);
            std::cout << "Enclave " << i << " local sort complete. Partition size: " << enclaveData[i].size() << "\n";
            }));
    }
    for (auto& t : threads) {
        t.join();
    }

    // Perform distributed merge rounds.
    int rounds = std::log2(numEnclaves);
    for (int r = 1; r <= rounds; r++) {
        int step = 1 << (r - 1);
        for (int i = 0; i < numEnclaves; i += 2 * step) {
            for (int j = 0; j < step; j++) {
                int idx1 = i + j;
                int idx2 = i + j + step;
                auto mergedPair = distributedMerge(enclaveData[idx1], enclaveData[idx2]);
                enclaveData[idx1] = mergedPair.first;
                enclaveData[idx2] = mergedPair.second;
                std::cout << "Distributed merge round " << r << " pairing enclaves " << idx1 << " and " << idx2 << " complete.\n";
            }
        }
    }

    // Concatenate global sorted results (removing dummies).
    std::vector<Element> globalSorted;
    for (int i = 0; i < numEnclaves; i++) {
        for (const auto& e : enclaveData[i]) {
            if (!e.is_dummy)
                globalSorted.push_back(e);
        }
    }

    bool isSorted = std::is_sorted(globalSorted.begin(), globalSorted.end(),
        [](const Element& a, const Element& b) {
            return a.value < b.value;
        });
    std::cout << "Global sorted order verified? " << (isSorted ? "Yes" : "No") << "\n";
    std::cout << "Total global sorted rows: " << globalSorted.size() << "\n";

    // Write global sorted integers to file.
    std::ofstream ofs("sorted_output_distributed_bitonic.json");
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not open sorted_output_distributed_bitonic.json for writing\n";
        return 1;
    }
    for (const auto& e : globalSorted) {
        ofs << e.value << "\n";
    }
    ofs.close();
    std::cout << "Wrote sorted_output_distributed_bitonic.json\n";

    return 0;
}
