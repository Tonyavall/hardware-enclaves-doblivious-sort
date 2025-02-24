#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <stdexcept>
#include <cmath>
#include <random>
#include <algorithm>
#include <sstream>

using namespace std;

struct Element {
    int value;
    int key;
    bool is_dummy;
};

class Server {
public:
    map<pair<int, int>, vector<Element>> storage;
    vector<string> access_log;
    vector<Element> read_bucket(int level, int bucket_index) {
        pair<int, int> key_pair = make_pair(level, bucket_index);
        stringstream ss;
        ss << "Read bucket at level " << level << ", index " << bucket_index << ", [";
        if (storage.find(key_pair) != storage.end()) {
            const auto& bucket = storage[key_pair];
            for (size_t i = 0; i < bucket.size(); i++) {
                ss << "(" << bucket[i].value << ", " << bucket[i].key << (bucket[i].is_dummy ? ", dummy" : "") << ")";
                if (i != bucket.size() - 1)
                    ss << ", ";
            }
        }
        ss << "]";
        access_log.push_back(ss.str());
        if (storage.find(key_pair) != storage.end())
            return storage[key_pair];
        else
            return vector<Element>();
    }
    void write_bucket(int level, int bucket_index, const vector<Element>& bucket) {
        pair<int, int> key_pair = make_pair(level, bucket_index);
        stringstream ss;
        ss << "Write bucket at level " << level << ", index " << bucket_index << ", [";
        for (size_t i = 0; i < bucket.size(); i++) {
            ss << "(" << bucket[i].value << ", " << bucket[i].key << (bucket[i].is_dummy ? ", dummy" : "") << ")";
            if (i != bucket.size() - 1)
                ss << ", ";
        }
        ss << "]";
        access_log.push_back(ss.str());
        storage[key_pair] = bucket;
    }
    vector<string> get_access_log() {
        return access_log;
    }
};

class Client {
public:
    Server* server;
    Client(Server* s) : server(s) {}
    pair<vector<Element>, vector<Element>> merge_split(const vector<Element>& bucket1, const vector<Element>& bucket2, int level, int total_levels, int Z) {
        int bit_index = total_levels - 1 - level;
        vector<Element> combined;
        combined.insert(combined.end(), bucket1.begin(), bucket1.end());
        combined.insert(combined.end(), bucket2.begin(), bucket2.end());
        vector<Element> real_elements;
        for (const auto& elem : combined)
            if (!elem.is_dummy)
                real_elements.push_back(elem);
        vector<Element> out_bucket0, out_bucket1;
        for (const auto& elem : real_elements)
            if (((elem.key >> bit_index) & 1) == 0)
                out_bucket0.push_back(elem);
            else
                out_bucket1.push_back(elem);
        if (out_bucket0.size() > (size_t)Z || out_bucket1.size() > (size_t)Z)
            throw runtime_error("Bucket overflow occurred in merge_split.");
        while (out_bucket0.size() < (size_t)Z)
            out_bucket0.push_back(Element{0, 0, true});
        while (out_bucket1.size() < (size_t)Z)
            out_bucket1.push_back(Element{0, 0, true});
        return {out_bucket0, out_bucket1};
    }
    vector<int> oblivious_sort(const vector<int>& input_array, int bucket_size) {
        int n = input_array.size();
        int Z = bucket_size;
        int B_required = ceil((2.0 * n) / Z);
        int B = 1;
        while (B < B_required)
            B *= 2;
        int L = log2(B);
        if (n > B * (Z / 2))
            throw runtime_error("Bucket size too small for input size.");
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(0, B - 1);
        vector<pair<int, int>> elements;
        for (int x : input_array)
            elements.push_back({x, dis(gen)});
        int group_size = ceil((double)n / B);
        vector<vector<pair<int, int>>> groups(B);
        for (int i = 0; i < B; i++) {
            int start = i * group_size;
            int end = min(start + group_size, n);
            for (int j = start; j < end; j++)
                groups[i].push_back(elements[j]);
        }
        for (int i = 0; i < B; i++) {
            vector<Element> bucket;
            for (auto& p : groups[i])
                bucket.push_back(Element{p.first, p.second, false});
            while (bucket.size() < (size_t)Z)
                bucket.push_back(Element{0, 0, true});
            server->write_bucket(0, i, bucket);
        }
        for (int level = 0; level < L; level++) {
            for (int i = 0; i < B; i += 2) {
                vector<Element> bucket1 = server->read_bucket(level, i);
                vector<Element> bucket2 = server->read_bucket(level, i + 1);
                auto buckets = merge_split(bucket1, bucket2, level, L, Z);
                server->write_bucket(level + 1, i, buckets.first);
                server->write_bucket(level + 1, i + 1, buckets.second);
            }
        }
        vector<pair<int, int>> final_elements;
        for (int i = 0; i < B; i++) {
            vector<Element> bucket = server->read_bucket(L, i);
            vector<pair<int, int>> real_elements;
            for (const auto& elem : bucket)
                if (!elem.is_dummy)
                    real_elements.push_back({elem.value, elem.key});
            shuffle(real_elements.begin(), real_elements.end(), gen);
            final_elements.insert(final_elements.end(), real_elements.begin(), real_elements.end());
        }
        sort(final_elements.begin(), final_elements.end(), [](const pair<int, int>& a, const pair<int, int>& b) {
            return a.first < b.first;
        });
        vector<int> sorted_values;
        for (auto& p : final_elements)
            sorted_values.push_back(p.first);
        return sorted_values;
    }
};

int main() {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1, 100);
    vector<int> input_data;
    for (int i = 0; i < 10; i++)
        input_data.push_back(dis(gen));
    cout << "Input Data: ";
    for (auto x : input_data)
        cout << x << " ";
    cout << "\n";
    Server server;
    Client client(&server);
    vector<int> sorted_data = client.oblivious_sort(input_data, 8);
    cout << "Sorted Data: ";
    for (auto x : sorted_data)
        cout << x << " ";
    cout << "\nAccess Log:\n";
    vector<string> log = server.get_access_log();
    for (auto& entry : log)
        cout << entry << "\n";
    return 0;
}
