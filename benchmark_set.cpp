/**
 * A simple insert & delete benchmark for data structures that implement a set.
 * See set_unfinished.h for details on the set interface we have assumed.
 */
using namespace std;
#include <thread>
#include <cstdlib>
#include <atomic>
#include <string>
#include <cstring>
#include <iostream>

#include "globals.h"
#include "util.h"
#include "set_unfinished.h"
#include "set_hashtable_lockfree.h"



template <class DataStructureType>
struct globals_t {
    PaddedRandom rngs[MAX_THREADS];
    volatile char padding0[PADDING_BYTES];
    ElapsedTimer timer;
    volatile char padding1[PADDING_BYTES];
    long elapsedMillis;
    volatile char padding2[PADDING_BYTES];
    volatile bool done;
    volatile char padding3[PADDING_BYTES];
    volatile bool start;        // used for a custom barrier implementation (should threads start yet?)
    volatile char padding4[PADDING_BYTES];
    atomic_int running;         // used for a custom barrier implementation (how many threads are waiting?)
    volatile char padding5[PADDING_BYTES];
    DataStructureType * ds;
    debugCounter numTotalOps;   // already has padding built in at the beginning and end
    debugCounter keyChecksum;
    int millisToRun;
    int totalThreads;
    int keyRangeSize;
    volatile char padding7[PADDING_BYTES];
    
    globals_t(int _millisToRun, int _totalThreads, int _keyRangeSize, DataStructureType * _ds) {
        for (int i=0;i<MAX_THREADS;++i) {
            rngs[i].setSeed(i+1); // +1 because we don't want thread 0 to get a seed of 0, since seeds of 0 usually mean all random numbers are zero...
        }
        elapsedMillis = 0;
        done = false;
        start = false;
        running = 0;
        ds = _ds;
        millisToRun = _millisToRun;
        totalThreads = _totalThreads;
        keyRangeSize = _keyRangeSize;
    }
    ~globals_t() {
        delete ds;
    }
} __attribute__((aligned(PADDING_BYTES)));

template <class DataStructureType>
void runExperiment(int keyRangeSize, int millisToRun, int totalThreads) {
    // create globals struct that all threads will access (with padding to prevent false sharing on control logic meta data)
    auto dataStructure = new DataStructureType(totalThreads, keyRangeSize);
    auto g = new globals_t<DataStructureType>(millisToRun, totalThreads, keyRangeSize, dataStructure);
    
    /**
     * 
     * RUN EXPERIMENT
     * 
     */
    
    // create and start threads
    thread * threads[MAX_THREADS]; // just allocate an array for max threads to avoid changing data layout (which can affect results) when varying thread count. the small amount of wasted space is not a big deal.
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid] = new thread([&, tid]() { /* access all variables by reference, except tid, which we copy (since we don't want our tid to be a reference to the changing loop variable) */
                const int OPS_BETWEEN_TIME_CHECKS = 500; // only check the current time (to see if we should stop) once every X operations, to amortize the overhead of time checking

                // BARRIER WAIT
                g->running.fetch_add(1);
                while (!g->start) { TRACE TPRINT("waiting to start"<<endl); } // wait to start
                
                for (int cnt=0; !g->done; ++cnt) {
                    if ((cnt % OPS_BETWEEN_TIME_CHECKS) == 0                    // once every X operations
                        && g->timer.getElapsedMillis() >= g->millisToRun) {   // check how much time has passed
                            g->done = true; // set global "done" bit flag, so all threads know to stop on the next operation (first guy to stop dictates when everyone else stops --- at most one more operation is performed per thread!)
                            __sync_synchronize(); // flush the write to g->done so other threads see it immediately (mostly paranoia, since volatile writes should be flushed, and also our next step will be a fetch&add which is an implied flush on intel/amd)
                    }

                    VERBOSE if (cnt&&((cnt % 1000000) == 0)) TPRINT("op# "<<cnt<<endl);
                    
                    // flip a coin to decide: insert or erase?
                    // generate a random double in [0, 1]
                    double operationType = g->rngs[tid].nextNatural() / (double) numeric_limits<unsigned int>::max();
                    
                    // generate random key
                    int key = 1 + (g->rngs[tid].nextNatural() % g->keyRangeSize);
                    
                    // insert or delete this key
                    if (operationType < 0.5) {
                        auto result = g->ds->insertIfAbsent(tid, key);
                        if (result==1) g->keyChecksum.add(tid, key);
                        else if (result==2) cout << "Expansion at m/s: " << g->timer.getElapsedMillis() << endl;
                    } else {
                        auto result = g->ds->erase(tid, key);
                        if (result) g->keyChecksum.add(tid, -key);
                    }
                    
                    g->numTotalOps.inc(tid);
                }
                
                g->running.fetch_add(-1);
                //TPRINT("terminated"<<endl);
        });
    }
    
    while (g->running < g->totalThreads) {
        TRACE cout<<"main thread: waiting for threads to START running="<<g->running<<endl;
    } // wait for all threads to be ready
    
    cout<<"main thread: starting timer..."<<endl;
    g->timer.startTimer();
    __sync_synchronize(); // prevent compiler from reordering "start = true;" before the timer start; this is mostly paranoia, since start is volatile, and nothing should be reordered around volatile reads/writes
    
    g->start = true; // release all threads from the barrier, so they can work
    
    while (g->running > 0) { /* wait for all threads to stop working */ }
    
    // measure and print elapsed time
    g->elapsedMillis = g->timer.getElapsedMillis();
    cout<<(g->elapsedMillis/1000.)<<"s"<<endl;
    
    // join all threads
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid]->join();
        delete threads[tid];
    }
    
    /**
     * 
     * PRODUCE OUTPUT
     * 
     * 
     */
    
    g->ds->printDebuggingDetails();
    
    auto numTotalOps = g->numTotalOps.getTotal();
    auto dsSumOfKeys = g->ds->getSumOfKeys();
    auto threadsSumOfKeys = g->keyChecksum.getTotal();
    cout<<"Validation: sum of keys according to the data structure = "<<dsSumOfKeys<<" and sum of keys according to the threads = "<<threadsSumOfKeys<<".";
    cout<<((threadsSumOfKeys == dsSumOfKeys) ? " OK." : " FAILED.")<<endl;
    cout<<endl;

    cout<<"completed ops        : "<<numTotalOps<<endl;
    cout<<"throughput           : "<<(long long) (numTotalOps * 1000. / g->elapsedMillis)<<endl;
    cout<<"elapsed milliseconds : "<<g->elapsedMillis<<endl;
    cout<<"END OF TEST"<<endl;
    cout<<endl;
    
    if (threadsSumOfKeys != dsSumOfKeys) {
        cout<<"ERROR: validation failed!"<<endl;
        exit(-1);
    }
    
    delete g;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        cout<<"USAGE: "<<argv[0]<<" [options]"<<endl;
        cout<<"Options:"<<endl;
        cout<<"    -a [string]  algorithm name in { unfinished }"<<endl;
        cout<<"    -t [int]     milliseconds to run"<<endl;
        cout<<"    -s [int]     size of the key range that random keys will be drawn from (i.e., range [1, s])"<<endl;
        cout<<"    -n [int]     number of threads that will perform inserts and deletes"<<endl;
        cout<<endl;
        cout<<"Example: "<<argv[0]<<" -a unfinished -t 5000 -s 1000000 -n 8"<<endl;
        return 1;
    }
    
    int millisToRun = -1;
    int keyRangeSize = 0;
    int totalThreads = 0;
    char * alg = NULL;
    
    // read command line args
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-s") == 0) {
            keyRangeSize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            totalThreads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            millisToRun = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            alg = argv[++i];
        } else {
            cout<<"bad arguments"<<endl;
            exit(1);
        }
    }
    
    // print command and args for debugging
    std::cout<<"Cmd:";
    for (int i=0;i<argc;++i) {
        std::cout<<" "<<argv[i];
    }
    std::cout<<std::endl;
    
    // print configuration for debugging
    PRINT(MAX_THREADS);
    PRINT(millisToRun);
    PRINT(keyRangeSize);
    PRINT(totalThreads);
    cout<<endl;
    
    // check for too large thread count
    if (totalThreads >= MAX_THREADS) {
        std::cout<<"ERROR: totalThreads="<<totalThreads<<" >= MAX_THREADS="<<MAX_THREADS<<std::endl;
        return 1;
    }
    
    // check for missing alg name
    if (alg == NULL) {
        cout<<"Must specify algorithm name"<<endl;
        return 1;
    }
    
    // run experiment for the selected algorithm
    if (!strcmp(alg, "unfinished")) {
        runExperiment<SetUnfinished>(keyRangeSize, millisToRun, totalThreads);
    } else if (!strcmp(alg, "hashtable")) {
        runExperiment<SetHashTableLockfree>(keyRangeSize, millisToRun, totalThreads);
    } else if (!strcmp(alg, "htmhash")) {
        runExperiment<Hlock>(keyRangeSize, millisToRun, totalThreads);
    }else {
        cout<<"Bad algorithm name: "<<alg<<endl;
        return 1;
    }
    
    return 0;
}
