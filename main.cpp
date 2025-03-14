#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include "nlohmann/json.hpp"
#include "oblivious_sort.h"

using json = nlohmann::json;

int main() {
    // 1. Open data.json
    std::ifstream ifs("data.json");
    if (!ifs.is_open()) {
        std::cerr << "Error: Could not open data.json\n";
        return 1;
    }

    // 2. Parse JSON
    json j;
    try {
        ifs >> j;
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return 1;
    }

    // 3. Build a vector of (subscriberCount, entireRowAsString)
    //    Adjust the field name if your JSON uses "subscribercount" or "subscriberCount".
    std::vector<std::pair<int, std::string>> inputRows;
    inputRows.reserve(j.size());

    for (auto& record : j) {
        int subscriberCount = 0;

        // Check the field name carefully: "subscriberCount" (capital C) vs "subscribercount" or something else
        if (record.contains("subscriberCount") && !record["subscriberCount"].is_null()) {
            if (record["subscriberCount"].is_string()) {
                // It's stored as a string, so parse it manually
                std::string scStr = record["subscriberCount"].get<std::string>();
                try {
                    subscriberCount = std::stoi(scStr);
                }
                catch (...) {
                    // Handle conversion error: maybe the field is empty or not a valid integer?
                    // We'll default to 0 or skip this record if it fails
                    subscriberCount = 0;
                }
            }
            else if (record["subscriberCount"].is_number()) {
                // It's already a number type, so just get<int>()
                subscriberCount = record["subscriberCount"].get<int>();
            }
        }

        // std::cout << "DEBUG: subscriberCount = " << subscriberCount << std::endl;
        // Convert the entire JSON object to a string. 
        // This might be big, but it keeps the entire row for output.
        std::string rowString = record.dump();

        inputRows.push_back({ subscriberCount, rowString });
    }

    std::cout << "Loaded " << inputRows.size() << " rows from data.json.\n";

    // 4. Set up untrusted memory + enclave
    UntrustedMemory untrusted;
    Enclave enclave(&untrusted);

    // 5. Perform oblivious sort by subscriberCount
    //    We'll pick a bucket_size. For large data, pick a bigger bucket_size to reduce overflow risk.
    int bucket_size = 16;
    std::cout << "Starting oblivious sort... (B=" << bucket_size << ")\n";
    std::vector<std::string> sortedRows;

    try {
        sortedRows = enclave.oblivious_sort(inputRows, bucket_size);
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "Sort complete. Number of sorted rows: " << sortedRows.size() << "\n";

    // 6. Write the sorted rows to an output file (e.g. sorted_output.json).
    //    Each line is the original JSON object, but in sorted order by subscriberCount.
    std::ofstream ofs("sorted_output.json");
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not open sorted_output.json for writing\n";
        return 1;
    }

    for (auto& line : sortedRows) {
        ofs << line << "\n";
    }

    ofs.close();
    std::cout << "Wrote sorted rows to sorted_output.json\n";

    // 7. (Optional) Print part of the untrusted memory log
    auto logvec = untrusted.get_access_log();
    std::cout << "\nAccess Log (first 10 entries):\n";

    if (logvec.size() <= 10) return 0;

    for (size_t i = 0; i < 10; i++) {
        std::cout << logvec[i] << "\n";
    }

    return 0;
}
