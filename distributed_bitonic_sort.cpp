// main.cpp
#include "oblivious_sort.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include <algorithm>
#include <cmath>
#include <cctype>

using namespace std;

// Helper function: Trim whitespace from both ends of a string.
string trim(const string& s) {
    auto start = s.begin();
    while (start != s.end() && isspace(*start)) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (distance(start, end) > 0 && isspace(*end));
    return string(start, end + 1);
}

// Helper function: Parse a file formatted like:
// [yWAPLdVoB,7ac2ZS4,VVYh, ...]
// for some reason it was throwing a fit bc it's not like "7ac2ZS4", it's 7ac2ZS4
vector<string> parseStringsFile(const string& filename) {
    ifstream ifs(filename);
    if (!ifs.is_open()) {
        throw runtime_error("Could not open " + filename);
    }
    stringstream buffer;
    buffer << ifs.rdbuf();
    string content = buffer.str();

    // Remove surrounding brackets if present.
    if (!content.empty() && content.front() == '[')
        content.erase(content.begin());
    if (!content.empty() && content.back() == ']')
        content.pop_back();

    vector<string> result;
    stringstream ss(content);
    string item;

    // Split by commas.
    while (getline(ss, item, ',')) {
        result.push_back(trim(item));
    }
    return result;
}

// Helper: Compute next power of two.
size_t nextPowerOfTwo(size_t n) {
    size_t power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

// Helper: Merge two sorted vectors (lexicographically, by Element::value)
// and split into lower and upper halves.
pair<vector<Element>, vector<Element>> distributedMerge(
    const vector<Element>& a,
    const vector<Element>& b)
{
    vector<Element> merged(a.size() + b.size());
    merge(a.begin(), a.end(), b.begin(), b.end(), merged.begin(),
        [](const Element& e1, const Element& e2) {
            return e1.value < e2.value;
        });
    size_t total = merged.size();
    size_t half = total / 2;
    vector<Element> lower(merged.begin(), merged.begin() + half);
    vector<Element> upper(merged.begin() + half, merged.end());
    return { lower, upper };
}

int main() {
    try {
        // 1. Read and parse strings17.json using our custom parser.
        vector<string> rawValues = parseStringsFile("strings17.json");
        cout << "Loaded " << rawValues.size() << " strings from strings22.json." << endl;

        // 2. Partition the data among 4 enclaves.
        const int numEnclaves = 4;  // Must be a power of two.
        size_t totalRows = rawValues.size();
        size_t rowsPerEnclave = totalRows / numEnclaves;
        size_t remainder = totalRows % numEnclaves;
        vector<vector<string>> partitions(numEnclaves);
        size_t index = 0;
        for (int i = 0; i < numEnclaves; i++) {
            size_t count = rowsPerEnclave + (i < remainder ? 1 : 0);
            partitions[i].reserve(count);
            for (size_t j = 0; j < count; j++) {
                partitions[i].push_back(rawValues[index++]);
            }
        }

        // 3. For each partition, convert to a vector of Elements.
        // Assume that the modified Element holds a string in 'value'.
        vector<vector<Element>> enclaveData(numEnclaves);
        UntrustedMemory dummyUntrusted;
        vector<Enclave> enclaves;
        for (int i = 0; i < numEnclaves; i++) {
            enclaves.emplace_back(&dummyUntrusted);
            for (const auto& s : partitions[i]) {
                Element e;
                e.value = s;    // The string value.
                e.key = 0;      // We'll compute composite keys later.
                e.is_dummy = false;
                enclaveData[i].push_back(e);
            }
            // Pad each partition to the next power of two.
            size_t origSize = enclaveData[i].size();
            size_t paddedSize = nextPowerOfTwo(origSize);
            if (paddedSize > origSize) {
                for (size_t j = origSize; j < paddedSize; j++) {
                    Element dummy;
                    dummy.value = "";
                    dummy.key = 0;
                    dummy.is_dummy = true;
                    enclaveData[i].push_back(dummy);
                }
            }
        }

        // 4. Perform local bitonic sort in each enclave concurrently.
        vector<thread> threads;
        for (int i = 0; i < numEnclaves; i++) {
            threads.push_back(thread([i, &enclaves, &enclaveData]() {
                enclaves[i].bitonicSort(enclaveData[i], 0, enclaveData[i].size(), true);
                cout << "Enclave " << i << " local sort complete. Partition size: " << enclaveData[i].size() << "\n";
                }));
        }
        for (auto& t : threads) {
            t.join();
        }

        // 5. Perform distributed merge rounds.
        int rounds = log2(numEnclaves);
        for (int r = 1; r <= rounds; r++) {
            int step = 1 << (r - 1);
            for (int i = 0; i < numEnclaves; i += 2 * step) {
                for (int j = 0; j < step; j++) {
                    int idx1 = i + j;
                    int idx2 = i + j + step;
                    auto mergedPair = distributedMerge(enclaveData[idx1], enclaveData[idx2]);
                    enclaveData[idx1] = mergedPair.first;
                    enclaveData[idx2] = mergedPair.second;
                    cout << "Distributed merge round " << r << " pairing enclaves "
                        << idx1 << " and " << idx2 << " complete.\n";
                }
            }
        }

        // 6. Concatenate global sorted results (remove dummy elements).
        vector<Element> globalSorted;
        for (int i = 0; i < numEnclaves; i++) {
            for (const auto& e : enclaveData[i]) {
                if (!e.is_dummy)
                    globalSorted.push_back(e);
            }
        }

        bool isSorted = is_sorted(globalSorted.begin(), globalSorted.end(),
            [](const Element& a, const Element& b) {
                return a.value < b.value;
            });
        cout << "Global sorted order verified? " << (isSorted ? "Yes" : "No") << "\n";
        cout << "Total global sorted rows: " << globalSorted.size() << "\n";

        // 7. Write global sorted strings to file.
        ofstream ofs("sorted_output_distributed_bitonic_strings.json");
        if (!ofs.is_open()) {
            cerr << "Error: Could not open sorted_output_distributed_bitonic_strings.json for writing\n";
            return 1;
        }
        for (const auto& e : globalSorted) {
            ofs << e.value << "\n";
        }
        ofs.close();
        cout << "Wrote sorted_output_distributed_bitonic_strings.json\n";
    }
    catch (const exception& ex) {
        cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
