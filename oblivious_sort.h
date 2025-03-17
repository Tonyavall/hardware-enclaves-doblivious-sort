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

// Represents a data element. For real elements, is_dummy is false.
struct Element {
    std::string value;  // Changed from int to std::string.
    int key;
    bool is_dummy;
};

// UntrustedMemory simulates untrusted storage (outside the enclave) that holds encrypted buckets.
class UntrustedMemory {
public:
    // Storage: keys are (level, bucket_index) and values are encrypted buckets.
    std::map<std::pair<int, int>, std::vector<Element>> storage;
    std::vector<std::string> access_log;

    // Read an encrypted bucket from untrusted memory.
    std::vector<Element> read_bucket(int level, int bucket_index);

    // Write an encrypted bucket to untrusted memory.
    void write_bucket(int level, int bucket_index, const std::vector<Element>& bucket);

    // Retrieve the access log.
    std::vector<std::string> get_access_log();
};

// Enclave represents the trusted SGX enclave. It decrypts data from untrusted memory,
// performs the oblivious sort operations, and reencrypts data when writing back.
class Enclave {
public:
    UntrustedMemory* untrusted;
    std::mt19937 rng; // Random number generator.

    // A fixed key for our simulated encryption.
    static constexpr int encryption_key = 0xdeadbeef;

    // Constructor: initializes the enclave with a pointer to untrusted memory.
    Enclave(UntrustedMemory* u);

    // Simulated encryption: XOR each field with encryption_key.
    static std::vector<Element> encryptBucket(const std::vector<Element>& bucket);

    // Simulated decryption.
    static std::vector<Element> decryptBucket(const std::vector<Element>& bucket);

    // Computes the bucket parameters (B: number of buckets, L: number of levels)
    // given the input size n and bucket capacity Z.
    std::pair<int, int> computeBucketParameters(int n, int Z);

    // Step 1: Initializes buckets by assigning random keys, partitioning the input, and padding with dummies.
    void initializeBuckets(const std::vector<std::string>& input_array, int B, int Z);

    // Step 2: Processes the butterfly network by performing MergeSplit on each bucket pair.
    void performButterflyNetwork(int B, int L, int Z);

    // Step 3: Extracts final elements from the last level and performs an oblivious permutation on each bucket.
    std::vector<Element> extractFinalElements(int B, int L);

    // Step 4: Performs a final non-oblivious sort on the extracted elements.
    std::vector<std::string> finalSort(const std::vector<Element>& final_elements);

    // The main oblivious sort function.
    std::vector<std::string> oblivious_sort(const std::vector<std::string>& input_array, int bucket_size);

    // Bitonic sort based functions for constant storage MergeSplit.
    void bitonicMerge(std::vector<Element>& a, int low, int cnt, bool ascending);
    void bitonicSort(std::vector<Element>& a, int low, int cnt, bool ascending);

    // Modified MergeSplit function that uses bitonic sort to implement the bucket split
    // with only O(1) enclave storage.
    std::pair<std::vector<Element>, std::vector<Element>> merge_split_bitonic(
        const std::vector<Element>& bucket1,
        const std::vector<Element>& bucket2,
        int level, int total_levels, int Z);

    // NEW: Oblivious permutation for a bucket using constant local storage.
    // It assigns a random label to each element and then obliviously sorts the bucket.
    void obliviousPermuteBucket(std::vector<Element>& bucket);
};

#endif // OBLIVIOUS_SORT_H