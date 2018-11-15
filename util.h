#ifndef UTIL_H
#define UTIL_H

#include <chrono>

class ElapsedTimer {
private:
    char padding0[PADDING_BYTES];
    bool calledStart = false;
    char padding1[PADDING_BYTES];
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    char padding2[PADDING_BYTES];
public:
    void startTimer() {
        calledStart = true;
        start = std::chrono::high_resolution_clock::now();
    }
    int64_t getElapsedMillis() {
        if (!calledStart) {
            printf("ERROR: called getElapsedMillis without calling startTimer\n");
            exit(1);
        }
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }
};

class PaddedRandom {
private:
    volatile char padding[PADDING_BYTES-sizeof(unsigned int)];
    unsigned int seed;
public:
    PaddedRandom(void) {
        this->seed = 0;
    }
    PaddedRandom(int seed) {
        this->seed = seed;
    }
    
    void setSeed(int seed) {
        this->seed = seed;
    }
    
    /** returns pseudorandom x satisfying 0 <= x < n. **/
    unsigned int nextNatural() {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        return seed;
    }
};

class debugCounter {
private:
    struct PaddedVLL {
        volatile char padding[PADDING_BYTES-sizeof(long long)];
        volatile long long v;
    };
    PaddedVLL data[MAX_THREADS+1];
public:
    void add(const int tid, const long long val) {
        data[tid].v += val;
    }
    void inc(const int tid) {
        add(tid, 1);
    }
    long long get(const int tid) {
        return data[tid].v;
    }
    long long getTotal() {
        long long result = 0;
        for (int tid=0;tid<MAX_THREADS;++tid) {
            result += get(tid);
        }
        return result;
    }
    void clear() {
        for (int tid=0;tid<MAX_THREADS;++tid) {
            data[tid].v = 0;
        }
    }
    debugCounter() {
        clear();
    }
} __attribute__((aligned(PADDING_BYTES)));

struct TryLock {
    int volatile state;
    TryLock() {
        state = 0;
    }
    bool tryAcquire() {
        if (state) return false;
        return __sync_bool_compare_and_swap(&state, 0, 1);
    }
    void release() {
        state = 0;
    }
    bool isHeld() {
        return state;
    }
};
class Sharded {
private:
   pthread_spinlock_t *lock;
   int padding = 16;
   int volatile *v; // no padding needed here since this only points to a memory location
   int number;
public:
   Sharded(){}
  void init(int _numThreads)
   {
      number = _numThreads;
      v = new int[_numThreads*padding];
      lock = new pthread_spinlock_t[_numThreads*padding];
      for (int i = 0; i < _numThreads; i ++) { v[i*padding] = 0; }
      for (int i = 0; i < _numThreads; i ++) {pthread_spin_init(&lock[i*padding], 0);}
      }

      int64_t inc(int tid) // <-- should be void
      {
         pthread_spin_lock(&lock[tid* padding]);
         v[tid * padding]++;
         pthread_spin_unlock(&lock[tid* padding]);

         return 0; // <-- no return needed
      }
      int64_t read()
      {
         int cont = 0;
         for (int x = 0; x < number; x++) { pthread_spin_lock(&lock[x*padding]); }

         for (int x = 0; x < number; x++) { cont += v[x*padding]; }

         for (int x = 0; x < number; x++) { pthread_spin_unlock(&lock[x*padding]); }

         return cont;
      }
   };
#endif /* UTIL_H */

