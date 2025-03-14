// main.cpp (Base-case: sort integers from ints.json)
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include "nlohmann/json.hpp"
#include "oblivious_sort.h"

using json = nlohmann::json;

int main() {
    // 1. Open ints.json (which should contain a JSON array of integers)
    std::ifstream ifs("ints.json");
    if (!ifs.is_open()) {
        std::cerr << "Error: Could not open ints.json\n";
        return 1;
    }

    // 2. Parse JSON into a vector of integers.
    json j;
    try {
        ifs >> j;
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return 1;
    }
    std::vector<int> inputValues = j.get<std::vector<int>>();
    std::cout << "Loaded " << inputValues.size() << " integers from ints.json.\n";

    // 3. Baseline non-oblivious sort using std::sort.
    std::vector<int> sortedStd = inputValues;

    std::sort(sortedStd.begin(), sortedStd.end());

    std::ofstream ofs_std("sorted_output_std.json");
    if (!ofs_std.is_open()) {
        std::cerr << "Error: Could not open sorted_output_std.json for writing\n";
        return 1;
    }
    for (auto val : sortedStd) {
        ofs_std << val << "\n";
    }
    ofs_std.close();
    std::cout << "Wrote sorted_output_std.json\n";

    return 0;
}
