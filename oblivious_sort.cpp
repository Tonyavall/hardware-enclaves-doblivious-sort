#include "oblivious_sort.h"
#include <iostream>
#include <algorithm>
#include <random>

// ----- UntrustedMemory Methods -----
std::vector<Element> UntrustedMemory::read_bucket(int level, int bucket_index) {
    std::pair<int, int> key = { level, bucket_index };
    std::ostringstream oss;
    oss << "Read bucket at level " << level << ", index " << bucket_index << ": ";

    // Log the 'sortKey' of real elements or "dummy" if it's a dummy
    if (storage.find(key) != storage.end()) {
        for (const auto& elem : storage[key]) {
            if (elem.is_dummy) {
                oss << "dummy ";
            }
            else {
                oss << elem.sortKey << " "; // Print subscriberCount for debugging
            }
        }
    }
    access_log.push_back(oss.str());

    // Return the bucket (encrypted or not)
    return storage[key];
}

void UntrustedMemory::write_bucket(int level, int bucket_index, const std::vector<Element>& bucket) {
    std::pair<int, int> key = { level, bucket_index };
    storage[key] = bucket;

    std::ostringstream oss;
    oss << "Write bucket at level " << level << ", index " << bucket_index << ": ";
    for (const auto& elem : bucket) {
        if (elem.is_dummy) {
            oss << "dummy ";
        }
        else {
            oss << elem.sortKey << " ";
        }
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
    // We'll produce a copy of bucket, XORing only sortKey and routingKey.
    // rowData is left unencrypted for demo purposes.
    std::vector<Element> encrypted = bucket;
    for (auto& elem : encrypted) {
        if (!elem.is_dummy) {
            elem.sortKey ^= encryption_key;
            elem.routingKey ^= encryption_key;
        }
    }
    return encrypted;
}

std::vector<Element> Enclave::decryptBucket(const std::vector<Element>& bucket) {
    // Reverse the XOR
    std::vector<Element> decrypted = bucket;
    for (auto& elem : decrypted) {
        if (!elem.is_dummy) {
            elem.sortKey ^= encryption_key;
            elem.routingKey ^= encryption_key;
        }
    }
    return decrypted;
}

std::pair<int, int> Enclave::computeBucketParameters(int n, int Z) {
    // Same logic as before
    int B_required = static_cast<int>(std::ceil((2.0 * n) / Z));
    int B = 1;
    while (B < B_required) {
        B *= 2;
    }
    int L = static_cast<int>(std::log2(B));
    if (n > B * (Z / 2)) {
        throw std::invalid_argument("Bucket size too small for input size.");
    }
    return { B, L };
}

void Enclave::initializeBuckets(const std::vector<std::pair<int, std::string>>& inputRows,
    int B, int Z)
{
    int n = inputRows.size();
    std::vector<Element> elements;
    std::uniform_int_distribution<int> routing_dist(0, B - 1);

    // Build a vector of real Elements
    for (auto& row : inputRows) {
        int subscriberCount = row.first;
        std::string fullTuple = row.second;

        // Generate random routingKey for the shuffle
        int rKey = routing_dist(rng);

        // Make an Element
        Element e;
        e.sortKey = subscriberCount; // we sort by this eventually
        e.routingKey = rKey;
        e.rowData = fullTuple;       // entire row data as a string
        e.is_dummy = false;

        elements.push_back(e);
    }

    // Partition the 'elements' vector into B consecutive groups
    int group_size = (n + B - 1) / B; // integer ceiling
    std::vector<std::vector<Element>> groups(B);

    for (int i = 0; i < B; i++) {
        int start = i * group_size;
        int end = std::min(start + group_size, n);
        if (start < n) {
            groups[i] = std::vector<Element>(elements.begin() + start, elements.begin() + end);
        }
        else {
            groups[i] = std::vector<Element>(); // empty group
        }
    }

    // Pad each group up to size Z with dummy elements
    for (int i = 0; i < B; i++) {
        auto& bucket = groups[i];
        while (bucket.size() < static_cast<size_t>(Z)) {
            Element dummy;
            dummy.sortKey = 0;
            dummy.routingKey = 0;
            dummy.rowData = "DUMMY";
            dummy.is_dummy = true;
            bucket.push_back(dummy);
        }
        // Encrypt and store
        untrusted->write_bucket(0, i, encryptBucket(bucket));
    }
}

void Enclave::bitonicMerge(std::vector<Element>& a, int low, int cnt, bool ascending) {
    if (cnt > 1) {
        int k = cnt / 2;
        for (int i = low; i < low + k; i++) {
            // Compare routingKey
            if ((ascending && a[i].routingKey > a[i + k].routingKey) ||
                (!ascending && a[i].routingKey < a[i + k].routingKey))
            {
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
        bitonicSort(a, low, k, true);
        bitonicSort(a, low + k, k, false);
        bitonicMerge(a, low, cnt, ascending);
    }
}

std::pair<std::vector<Element>, std::vector<Element>>
Enclave::merge_split_bitonic(const std::vector<Element>& bucket1,
    const std::vector<Element>& bucket2,
    int level, int total_levels, int Z)
{
    int L = total_levels;
    int bit_index = L - 1 - level;

    // Combine
    std::vector<Element> combined = bucket1;
    combined.insert(combined.end(), bucket2.begin(), bucket2.end());

    // Count how many real elements go to side 0 or side 1
    int count0 = 0, count1 = 0;
    for (auto& e : combined) {
        if (!e.is_dummy) {
            // This bit decides bucket
            int sideBit = (e.routingKey >> bit_index) & 1;
            if (sideBit == 0) count0++; else count1++;
        }
    }
    if (count0 > Z || count1 > Z) {
        throw std::overflow_error("Bucket overflow in mergesplit!");
    }

    // How many dummies for each side?
    int needed_dummies0 = Z - count0;
    int needed_dummies1 = Z - count1;
    int assigned_dummies0 = 0, assigned_dummies1 = 0;

    // Re-assign routingKey for bitonic sorting
    for (auto& e : combined) {
        if (e.is_dummy) {
            // If we still need dummy in side 0 => key=1, else => key=3
            if (assigned_dummies0 < needed_dummies0) {
                e.routingKey = 1;
                assigned_dummies0++;
            }
            else {
                e.routingKey = 3;
                assigned_dummies1++;
            }
        }
        else {
            int bit_val = (e.routingKey >> bit_index) & 1; // 0 or 1
            e.routingKey = (bit_val << 1); // => 0 or 2
        }
    }

    // Bitonic sort on combined by routingKey
    bitonicSort(combined, 0, combined.size(), true);

    // Split
    std::vector<Element> out0(combined.begin(), combined.begin() + Z);
    std::vector<Element> out1(combined.begin() + Z, combined.end());

    return { out0, out1 };
}

void Enclave::performButterflyNetwork(int B, int L, int Z) {
    for (int level = 0; level < L; level++) {
        for (int i = 0; i < B; i += 2) {
            auto bucket1_enc = untrusted->read_bucket(level, i);
            auto bucket2_enc = untrusted->read_bucket(level, i + 1);

            auto bucket1 = decryptBucket(bucket1_enc);
            auto bucket2 = decryptBucket(bucket2_enc);

            // mergesplit
            auto [out_bucket0, out_bucket1] =
                merge_split_bitonic(bucket1, bucket2, level, L, Z);

            // encrypt & write
            untrusted->write_bucket(level + 1, i, encryptBucket(out_bucket0));
            untrusted->write_bucket(level + 1, i + 1, encryptBucket(out_bucket1));
        }
    }
}

std::vector<Element> Enclave::extractFinalElements(int B, int L) {
    std::vector<Element> final_elements;
    for (int i = 0; i < B; i++) {
        auto enc_bucket = untrusted->read_bucket(L, i);
        auto dec_bucket = decryptBucket(enc_bucket);

        std::vector<Element> real_elems;
        for (auto& e : dec_bucket) {
            if (!e.is_dummy) {
                real_elems.push_back(e);
            }
        }
        // Shuffle them locally in the enclave
        std::shuffle(real_elems.begin(), real_elems.end(), rng);
        final_elements.insert(final_elements.end(), real_elems.begin(), real_elems.end());
    }
    return final_elements;
}

std::vector<std::string> Enclave::finalSort(const std::vector<Element>& final_elements) {
    // Sort by sortKey ascending
    std::vector<Element> sorted_elems = final_elements;
    std::sort(sorted_elems.begin(), sorted_elems.end(), [](auto& a, auto& b) {
        return a.sortKey < b.sortKey;
        });

    // Return the entire rowData in sorted order
    std::vector<std::string> result;
    result.reserve(sorted_elems.size());
    for (auto& e : sorted_elems) {
        result.push_back(e.rowData);
    }
    return result;
}

std::vector<std::string> Enclave::oblivious_sort(
    const std::vector<std::pair<int, std::string>>& inputRows,
    int bucket_size)
{
    int n = inputRows.size();
    auto [B, L] = computeBucketParameters(n, bucket_size);

    // Step 1: Initialize
    initializeBuckets(inputRows, B, bucket_size);

    // Step 2: Shuffle in network
    performButterflyNetwork(B, L, bucket_size);

    // Step 3: Extract final real elements
    std::vector<Element> final_elems = extractFinalElements(B, L);

    // Step 4: Local non-oblivious sort by 'sortKey'
    auto sortedRows = finalSort(final_elems);

    return sortedRows;
}
