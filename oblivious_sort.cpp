#include "oblivious_sort.h"
#include <iostream>
#include <algorithm>
#include <random>

// ----- UntrustedMemory Methods -----
std::vector<Element> UntrustedMemory::read_bucket(int level, int bucket_index) {
    std::pair<int, int> key = { level, bucket_index };
    std::ostringstream oss;
    oss << "Read bucket at level " << level << ", index " << bucket_index << ": ";
    if (storage.find(key) != storage.end()) {
        for (const auto& elem : storage[key]) {
            if (elem.is_dummy)
                oss << "dummy ";
            else
                oss << elem.value << " ";
        }
    }
    access_log.push_back(oss.str());
    return storage[key];
}

void UntrustedMemory::write_bucket(int level, int bucket_index, const std::vector<Element>& bucket) {
    std::pair<int, int> key = { level, bucket_index };
    storage[key] = bucket;
    std::ostringstream oss;
    oss << "Write bucket at level " << level << ", index " << bucket_index << ": ";
    for (const auto& elem : bucket) {
        if (elem.is_dummy)
            oss << "dummy ";
        else
            oss << elem.value << " ";
    }
    access_log.push_back(oss.str());
}

std::vector<std::string> UntrustedMemory::get_access_log() {
    return access_log;
}

// ----- Enclave Methods -----
Enclave::Enclave(UntrustedMemory* u) : untrusted(u) {
    std::random_device rd;
    rng.seed(rd());
}

std::vector<Element> Enclave::encryptBucket(const std::vector<Element>& bucket) {
    std::vector<Element> encrypted = bucket;
    for (auto& elem : encrypted) {
        if (!elem.is_dummy) {
            elem.value ^= encryption_key;
            elem.key ^= encryption_key;
        }
    }
    return encrypted;
}

std::vector<Element> Enclave::decryptBucket(const std::vector<Element>& bucket) {
    std::vector<Element> decrypted = bucket;
    for (auto& elem : decrypted) {
        if (!elem.is_dummy) {
            elem.value ^= encryption_key;
            elem.key ^= encryption_key;
        }
    }
    return decrypted;
}

std::pair<int, int> Enclave::computeBucketParameters(int n, int Z) {
    int B_required = static_cast<int>(std::ceil((2.0 * n) / Z));
    int B = 1;
    while (B < B_required)
        B *= 2;
    int L = static_cast<int>(std::log2(B));
    if (n > B * (Z / 2))
        throw std::invalid_argument("Bucket size too small for input size.");
    return { B, L };
}

void Enclave::initializeBuckets(const std::vector<int>& input_array, int B, int Z) {
    int n = input_array.size();
    std::vector<Element> elements;
    std::uniform_int_distribution<int> key_dist(0, B - 1);
    for (int x : input_array) {
        int random_key = key_dist(rng);
        elements.push_back(Element{ x, random_key, false });
    }
    int group_size = (n + B - 1) / B;
    std::vector<std::vector<Element>> groups(B);
    for (int i = 0; i < B; i++) {
        int start = i * group_size;
        int end = std::min(start + group_size, n);
        if (start < n)
            groups[i] = std::vector<Element>(elements.begin() + start, elements.begin() + end);
        else
            groups[i] = std::vector<Element>(); // Empty group.
    }
    for (int i = 0; i < B; i++) {
        std::vector<Element> bucket = groups[i];
        while (bucket.size() < static_cast<size_t>(Z))
            bucket.push_back(Element{ 0, 0, true });
        untrusted->write_bucket(0, i, encryptBucket(bucket));
    }
}
void Enclave::bitonicMerge(std::vector<Element>& a, int low, int cnt, bool ascending) {
    if (cnt > 1) {
        int k = cnt / 2;
        for (int i = low; i < low + k; i++) {
            if ((ascending && a[i].key > a[i + k].key) ||
                (!ascending && a[i].key < a[i + k].key)) {
                std::swap(a[i], a[i + k]);
            }
        }
        bitonicMerge(a, low, k, ascending);
        bitonicMerge(a, low + k, k, ascending);
    }
}

void Enclave::bitonicSort(std::vector<Element>& a, int low, int cnt, bool ascending) {
    if (cnt > 1) {
        int k = cnt / 2;
        // First half sorted in ascending order, second half in descending order.
        bitonicSort(a, low, k, true);
        bitonicSort(a, low + k, k, false);
        // Merge whole sequence in desired order.
        bitonicMerge(a, low, cnt, ascending);
    }
}

// Modified merge_split function that uses bitonic sort.
// This function replaces the original merge_split to achieve constant (O(1)) enclave storage.
std::pair<std::vector<Element>, std::vector<Element>> Enclave::merge_split_bitonic(
    const std::vector<Element>& bucket1,
    const std::vector<Element>& bucket2,
    int level, int total_levels, int Z) {

    int L = total_levels;
    int bit_index = L - 1 - level;

    // Combine the two buckets into one vector (size 2Z).
    std::vector<Element> combined = bucket1;
    combined.insert(combined.end(), bucket2.begin(), bucket2.end());

    // Count the number of real elements assigned to each target bucket.
    int count0 = 0, count1 = 0;
    for (const auto& elem : combined) {
        if (!elem.is_dummy) {
            if (((elem.key >> bit_index) & 1) == 0)
                count0++;
            else
                count1++;
        }
    }
    if (count0 > Z || count1 > Z)
        throw std::overflow_error("Bucket overflow occurred in merge_split.");

    // Determine how many dummy elements need to be assigned to each bucket.
    int needed_dummies0 = Z - count0;
    int needed_dummies1 = Z - count1;
    int assigned_dummies0 = 0, assigned_dummies1 = 0;

    // For each element in 'combined', assign a composite key that will be used for bitonic sort.
    // We define the composite key as:
    //   For a real element: ( ( (elem.key >> bit_index) & 1 ) << 1 ) | 0.
    //       That is, 0 if the bit is 0 (target bucket 0) and 2 if the bit is 1 (target bucket 1).
    //   For a dummy element:
    //       If we still need dummies in bucket 0, assign composite key 1.
    //       Otherwise, assign composite key 3.
    for (auto& elem : combined) {
        if (elem.is_dummy) {
            if (assigned_dummies0 < needed_dummies0) {
                elem.key = 1; // Tagged for bucket 0 dummy.
                assigned_dummies0++;
            }
            else {
                elem.key = 3; // Tagged for bucket 1 dummy.
                assigned_dummies1++;
            }
        }
        else {
            int bit_val = (elem.key >> bit_index) & 1;
            elem.key = (bit_val << 1); // 0 for bucket 0, 2 for bucket 1.
        }
    }

    // Perform bitonic sort on the combined vector using the composite keys.
    bitonicSort(combined, 0, combined.size(), true);

    // After sorting, the first Z elements belong to output bucket 0, and the next Z to output bucket 1.
    std::vector<Element> out_bucket0(combined.begin(), combined.begin() + Z);
    std::vector<Element> out_bucket1(combined.begin() + Z, combined.end());

    return { out_bucket0, out_bucket1 };
}


void Enclave::performButterflyNetwork(int B, int L, int Z) {
    for (int level = 0; level < L; level++) {
        for (int i = 0; i < B; i += 2) {
            std::vector<Element> bucket1_enc = untrusted->read_bucket(level, i);
            std::vector<Element> bucket2_enc = untrusted->read_bucket(level, i + 1);
            std::vector<Element> bucket1 = decryptBucket(bucket1_enc);
            std::vector<Element> bucket2 = decryptBucket(bucket2_enc);
            auto [out_bucket0, out_bucket1] = merge_split_bitonic(bucket1, bucket2, level, L, Z);
            untrusted->write_bucket(level + 1, i, encryptBucket(out_bucket0));
            untrusted->write_bucket(level + 1, i + 1, encryptBucket(out_bucket1));
        }
    }
}

std::vector<Element> Enclave::extractFinalElements(int B, int L) {
    std::vector<Element> final_elements;
    for (int i = 0; i < B; i++) {
        std::vector<Element> bucket_enc = untrusted->read_bucket(L, i);
        std::vector<Element> bucket = decryptBucket(bucket_enc);
        std::vector<Element> real_elements;
        for (const auto& elem : bucket)
            if (!elem.is_dummy)
                real_elements.push_back(elem);
        std::shuffle(real_elements.begin(), real_elements.end(), rng);
        final_elements.insert(final_elements.end(), real_elements.begin(), real_elements.end());
    }
    return final_elements;
}

std::vector<int> Enclave::finalSort(const std::vector<Element>& final_elements) {
    std::vector<Element> sorted_elements = final_elements;
    std::sort(sorted_elements.begin(), sorted_elements.end(),
        [](const Element& a, const Element& b) {
            return a.value < b.value;
        });
    std::vector<int> sorted_values;
    for (const auto& elem : sorted_elements)
        sorted_values.push_back(elem.value);
    return sorted_values;
}

std::vector<int> Enclave::oblivious_sort(const std::vector<int>& input_array, int bucket_size) {
    int n = input_array.size();
    int Z = bucket_size;
    auto [B, L] = computeBucketParameters(n, Z);
    initializeBuckets(input_array, B, Z);
    performButterflyNetwork(B, L, Z);
    std::vector<Element> final_elements = extractFinalElements(B, L);
    return finalSort(final_elements);
}
