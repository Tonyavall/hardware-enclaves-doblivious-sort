#ifndef OBLIVIOUS_SORT_H
#define OBLIVIOUS_SORT_H

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <cmath>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <utility>

// Represents one row to be sorted + its random routing info + a dummy flag.
struct Element {
    // The integer key by which we actually want to sort (e.g. subscriberCount).
    int sortKey;

    // The random key used for oblivious routing (bucket assignment, mergesplit).
    // This is what was previously 'key' in your code.
    int routingKey;

    // The entire record as a string. Could hold the JSON object or CSV line, etc.
    // This way, after we sort by sortKey, we still keep all fields for output.
    std::string rowData;

    // Indicates whether this is a dummy element or a real one.
    bool is_dummy;
};

// ---------------- UntrustedMemory ----------------
// Simulates untrusted storage that holds encrypted buckets.
class UntrustedMemory {
public:
    // The storage is addressed by (level, bucketIndex) pairs.
    std::map<std::pair<int, int>, std::vector<Element>> storage;
    std::vector<std::string> access_log;

    // Read an encrypted bucket from untrusted memory.
    std::vector<Element> read_bucket(int level, int bucket_index);

    // Write an encrypted bucket to untrusted memory.
    void write_bucket(int level, int bucket_index, const std::vector<Element>& bucket);

    // Retrieve the entire access log.
    std::vector<std::string> get_access_log();
};

// ---------------- Enclave ----------------
// Represents the trusted SGX enclave that does all the oblivious operations.
class Enclave {
public:
    UntrustedMemory* untrusted;
    std::mt19937 rng; // Random number generator

    // A fixed key for our simulated encryption.
    static constexpr int encryption_key = 0xdeadbeef;

    // Constructor.
    Enclave(UntrustedMemory* u);

    // Simulated encryption: XOR sortKey and routingKey with encryption_key.
    // We do not encrypt rowData in this toy example, but you could do so if needed.
    static std::vector<Element> encryptBucket(const std::vector<Element>& bucket);

    // Simulated decryption.
    static std::vector<Element> decryptBucket(const std::vector<Element>& bucket);

    // Compute bucket parameters: B (num buckets) and L (num levels) from n and Z.
    std::pair<int, int> computeBucketParameters(int n, int Z);

    // Step 1: Initialize buckets with random routing keys, store rowData, pad with dummies.
    // 'inputRows': each row is a (subscriberCount, entireRowAsJSON).
    void initializeBuckets(const std::vector<std::pair<int, std::string>>& inputRows,
        int B, int Z);

    // Step 2: The mergesplit butterfly or bitonic approach to shuffle them obliviously.
    void performButterflyNetwork(int B, int L, int Z);

    // Step 3: Extract final elements from the last level, do an in-enclave shuffle.
    std::vector<Element> extractFinalElements(int B, int L);

    // Step 4: Final sort by 'sortKey' in normal data-dependent manner, returning sorted rows.
    // We output the entire rowData, but the ordering is by sortKey ascending.
    std::vector<std::string> finalSort(const std::vector<Element>& final_elements);

    // The top-level oblivious sort function.
    //   'inputRows': vector of (subscriberCount, entireRowString).
    //   'bucket_size': capacity Z for each bucket.
    std::vector<std::string> oblivious_sort(
        const std::vector<std::pair<int, std::string>>& inputRows,
        int bucket_size
    );

    // ---------------- Bitonic mergesplit Helpers ----------------
    void bitonicMerge(std::vector<Element>& a, int low, int cnt, bool ascending);
    void bitonicSort(std::vector<Element>& a, int low, int cnt, bool ascending);

    // The mergesplit that uses bitonic sort, as in your original code.
    std::pair<std::vector<Element>, std::vector<Element>> merge_split_bitonic(
        const std::vector<Element>& bucket1,
        const std::vector<Element>& bucket2,
        int level, int total_levels, int Z
    );
};

#endif // OBLIVIOUS_SORT_H
