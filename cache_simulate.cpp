#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <bitset>

using namespace std;

// Constants for cache configuration
const int L1_INSTRUCTION_CACHE_ASSOC = 2;
const int L1_INSTRUCTION_CACHE_SETS = 16 * 1024;
const int L1_INSTRUCTION_CACHE_LINESIZE = 64; 

const int L1_DATA_CACHE_ASSOC = 4;
const int L1_DATA_CACHE_SETS = 16 * 1024;
const int L1_DATA_CACHE_LINESIZE = 64;

const int L2_CACHE_SIZE = 512 * 1024;

// Cache entry structure
struct CacheEntry {
    bool valid;
    bool dirty;
    bool firstWrite;
    bitset<L1_DATA_CACHE_ASSOC> lru;
};

// L1 Instruction Cache
unordered_map<unsigned long long, CacheEntry> l1InstructionCache;

// L1 Data Cache
unordered_map<unsigned long long, CacheEntry> l1DataCache;

// L2 Cache
unordered_map<unsigned long long, CacheEntry> l2Cache;

// Statistics counters
int numCacheReads = 0;
int numCacheWrites = 0;
int numCacheHits = 0;
int numCacheMisses = 0;

// Function to handle L1 cache access
void accessCache(unsigned long long address, bool isDataCache, bool isWrite) {
    auto& cache = isDataCache ? l1DataCache : l1InstructionCache;
    auto iter = cache.find(address);

    // Cache hit
    if (iter != cache.end() && iter->second.valid) {
        numCacheHits++;
        iter->second.lru.reset();
        iter->second.lru.set(0, true);

        // Update dirty bit and write policy
        if (isDataCache && isWrite) {
            if (!iter->second.firstWrite)
                iter->second.dirty = true;
            else
                iter->second.firstWrite = false;
        }
        return;
    }

    // Cache miss
    numCacheMisses++;

    // Check L2 cache
    auto l2Iter = l2Cache.find(address);
    if (l2Iter != l2Cache.end() && l2Iter->second.valid) {
        // Read from L2
        cout << "Read from L2 0x" << hex << uppercase << setw(8) << setfill('0') << address << endl;

        // Insert/Update cache entry in L1
        cache[address] = {true, false, false, bitset<L1_DATA_CACHE_ASSOC>()};

        // Set LRU bits
        for (auto& entry : cache) {
            if (entry.second.valid) {
                entry.second.lru >>= 1;
                entry.second.lru.set(L1_DATA_CACHE_ASSOC - 1, false);
            }
        }

        // Set LRU bits for the new entry
        cache[address].lru.set(L1_DATA_CACHE_ASSOC - 1, true);
    } else {
        // Read for Ownership from L2
        cout << "Read for Ownership from L2 0x" << hex << uppercase << setw(8) << setfill('0') << address << endl;

        // Insert/Update cache entry in L1
        cache[address] = {true, true, true, bitset<L1_DATA_CACHE_ASSOC>()};

        // Set LRU bits
        for (auto& entry : cache) {
            if (entry.second.valid) {
                entry.second.lru >>= 1;
                entry.second.lru.set(L1_DATA_CACHE_ASSOC - 1, false);
            }
        }

        // Set LRU bits for the new entry
        cache[address].lru.set(L1_DATA_CACHE_ASSOC - 1, true);
    }
}

// Function to handle L2 cache eviction
void evictL2Cache(unsigned long long address) {
    auto l2Iter = l2Cache.find(address);
    if (l2Iter != l2Cache.end() && l2Iter->second.valid && l2Iter->second.dirty) {
        // Write back to L2
        cout << "Write to L2 0x" << hex << uppercase << setw(8) << setfill('0') << address << endl;
    }
}

// Function to parse and process the trace file
void processTraceFile(const string& traceFile, bool verbose) {
    ifstream file(traceFile);
    string line;
    while (getline(file, line)) {
        istringstream iss(line);
        int operation;
        unsigned long long address;
        if (iss >> operation >> hex >> address) {
            switch (operation) {
                case 0: // Read data request to L1 data cache
                    numCacheReads++;
                    accessCache(address, true, false);
                    break;
                case 1: // Write data request to L1 data cache
                    numCacheWrites++;
                    accessCache(address, true, true);
                    break;
                case 2: // Instruction fetch (read request to L1 instruction cache)
                    numCacheReads++;
                    accessCache(address, false, false);
                    break;
                case 3: // Evict command from L2
                    evictL2Cache(address);
                    break;
                case 8: // Clear cache and reset statistics
                    l1InstructionCache.clear();
                    l1DataCache.clear();
                    l2Cache.clear();
                    numCacheReads = 0;
                    numCacheWrites = 0;
                    numCacheHits = 0;
                    numCacheMisses = 0;
                    break;
                case 9: // Print contents and state of the cache
                    if (verbose) {
                        cout << "L1 Instruction Cache:\n";
                        for (const auto& entry : l1InstructionCache) {
                            if (entry.second.valid) {
                                cout << "Address: 0x" << hex << uppercase << setw(8) << setfill('0') << entry.first;
                                cout << ", Valid: 1, Dirty: 0\n";
                            }
                        }

                        cout << "L1 Data Cache:\n";
                        for (const auto& entry : l1DataCache) {
                            if (entry.second.valid) {
                                cout << "Address: 0x" << hex << uppercase << setw(8) << setfill('0') << entry.first;
                                cout << ", Valid: 1, Dirty: " << entry.second.dirty << ", LRU: ";
                                cout << entry.second.lru.to_string().substr(L1_DATA_CACHE_ASSOC - 1) << endl;
                            }
                        }

                        cout << "L2 Cache:\n";
                        for (const auto& entry : l2Cache) {
                            if (entry.second.valid) {
                                cout << "Address: 0x" << hex << uppercase << setw(8) << setfill('0') << entry.first;
                                cout << ", Valid: 1, Dirty: " << entry.second.dirty << endl;
                            }
                        }
                    }
                    break;
                default:
                    cerr << "Invalid operation: " << operation << endl;
                    break;
            }
        }
    }
}

int main(int argc, char** argv) {
    // Parse command line arguments
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <mode> <trace_file>" << endl;
        return 1;
    }

    bool verbose = stoi(argv[1]) != 0;
    string traceFile = argv[2];

    // Process the trace file
    processTraceFile(traceFile, verbose);

    // Calculate cache hit ratio
    double cacheHitRatio = numCacheHits / static_cast<double>(numCacheReads + numCacheWrites);

    // Print cache usage statistics
    cout << "Number of cache reads: " << numCacheReads << endl;
    cout << "Number of cache writes: " << numCacheWrites << endl;
    cout << "Number of cache hits: " << numCacheHits << endl;
    cout << "Number of cache misses: " << numCacheMisses << endl;
    cout << "Cache hit ratio: " << fixed << setprecision(2) << cacheHitRatio * 100 << "%" << endl;

    return 0;
}

