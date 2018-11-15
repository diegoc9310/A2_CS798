#pragma once

#include <cassert>
#include <pthread.h>
using namespace std;

uint32_t murmur3_32(int k) {
    uint32_t h = 0x1a8b714c; // seed
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    h ^= k;
    h = (h << 13) | (h >> 19);
    h = (h * 5) + 0xe6546b64;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

class SetHashTableLockfree {
private:
    static const int EMPTY = 0;
    static const int TOMBSTONE = -1;
    volatile char padding0[PADDING_BYTES];
    int * data;
    volatile char padding1[PADDING_BYTES];
    const int numThreads;
    const int capacity;
    volatile char padding2[PADDING_BYTES];
    Sharded  failed_inserts;
    volatile char padding3[PADDING_BYTES];
    Sharded successful_inserts;
    volatile char padding4[PADDING_BYTES];
    Sharded someone_else_inserts;
    volatile char padding5[PADDING_BYTES];
    Sharded  failed_erase;
    volatile char padding6[PADDING_BYTES];
    Sharded  successful_erase;
    volatile char padding7[PADDING_BYTES];
public:
    SetHashTableLockfree(const int _numThreads, const int _size);
    ~SetHashTableLockfree();
    bool insertIfAbsent(const int tid, const int & key); // try to insert key; return true if successful (if it doesn't already exist), false otherwise
    bool erase(const int tid, const int & key); // try to erase key; return true if successful, false otherwise
    long getSumOfKeys(); // should return the sum of all keys in the set
    void printDebuggingDetails(); // print any debugging details you want at the end of a trial in this function
};

SetHashTableLockfree::SetHashTableLockfree(const int _numThreads, const int _size)
        : numThreads(_numThreads)
        , capacity(2*_size) {
    data = new int[capacity];
    failed_inserts.init(numThreads);
    successful_inserts.init(numThreads);
    someone_else_inserts.init(numThreads);
    failed_erase.init(numThreads);
    successful_erase.init(numThreads);
    #pragma omp parallel for
    for (int i=0;i<capacity;++i) {
        data[i] = EMPTY;
    }
}

SetHashTableLockfree::~SetHashTableLockfree() {
    delete[] data;
}

bool SetHashTableLockfree::insertIfAbsent(const int tid, const int & key) {
    assert(EMPTY != key && TOMBSTONE != key);
    unsigned int const hash = murmur3_32(key);
    for (unsigned int i = 0 ; i < capacity ; ++i) {
        unsigned int const index = (hash + i) % capacity;
        unsigned int found = data[index];
        if (found == key) {
           failed_inserts.inc(tid);
            return false;
        } else if (found == EMPTY) {
            auto result = __sync_val_compare_and_swap(&data[index], found, key);
            if (result == found) {
               successful_inserts.inc(tid);
                return true;
            } else if (result == key) { // key was inserted by someone else
               someone_else_inserts.inc(tid);
               return false;
            }
        }
    }
    return false;
}

bool SetHashTableLockfree::erase(const int tid, const int & key) {
    assert(EMPTY != key && TOMBSTONE != key);
    unsigned int const hash = murmur3_32(key);
    for (unsigned int i = 0 ; i < capacity ; ++i) {
        unsigned int const index = (hash + i) % capacity;
        unsigned int found = data[index];
        if (found == key) {
            auto result = __sync_val_compare_and_swap(&data[index], key, TOMBSTONE);
            if (result == key) { successful_erase.inc(tid); return true; }
            assert(data[index] == TOMBSTONE); // someone else deleted key (by CASing to TOMBSTONE)
            failed_erase.inc(tid);
            return false; 
        } else if (found == EMPTY) {
            failed_erase.inc(tid);
            return false; // did not find key
        }
    }
    return false;
}

long SetHashTableLockfree::getSumOfKeys() {
    long sum = 0;
    #pragma omp parallel for reduction(+:sum)
    for (int i=0;i<capacity;++i) {
        int v = data[i];
        if (v != TOMBSTONE && v != EMPTY) sum += v;
    }
    return sum;
}

void SetHashTableLockfree::printDebuggingDetails() {

      cout << "failed_inserts      : "<<failed_inserts.read()        << endl;
      cout << "successful_inserts  : "<<successful_inserts.read()   << endl;
      cout << "someone_else_inserts: "<<someone_else_inserts.read()  << endl;
      cout << "failed_erase        : "<<failed_erase.read()          << endl;
      cout << "successful_erase    : "<<successful_erase.read()     << endl;
    
}

