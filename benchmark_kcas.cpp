/**
 * An array-based benchmark for k-CAS
 */

#include <thread>
#include <cstdlib>
#include <atomic>
#include <string>
#include <iostream>

#include "globals.h"
#include "util.h"
#include "kcas_reuse_impl.h"
#include "kcas_unfinished.h"
#include "array_using_kcas.h"

using namespace std;

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
    debugCounter numSuccessfulOps;    // already has padding built in at the beginning and end
    debugCounter numTotalOps;      // already has padding built in at the beginning and end
    int millisToRun;
    int totalThreads;
    int K;
    volatile char padding7[PADDING_BYTES];
    
    globals_t(int _millisToRun, int _totalThreads, int _K, DataStructureType * _ds) {
        for (int i=0;i<MAX_THREADS;++i) {
            rngs[i].setSeed(i+1); // +1 because we don't want thread 0 to get a seed of 0, since seeds of 0 usually mean all random numbers are zero...
        }
        elapsedMillis = 0;
        done = false;
        start = false;
        running = 0;
        millisToRun = _millisToRun;
        totalThreads = _totalThreads;
        K = _K;
        ds = _ds;
    }
    ~globals_t() {
        delete ds;
    }
} __attribute__((aligned(PADDING_BYTES)));

template <class KCASProvider>
void runExperiment(int arraySize, int millisToRun, int totalThreads, int K) {
    // create globals struct that all threads will access (with padding to prevent false sharing on control logic meta data)
    auto sharedArray = new ArrayUsingKCAS<KCASProvider>(arraySize, K);
    auto g = new globals_t<ArrayUsingKCAS<KCASProvider>>(millisToRun, totalThreads, K, sharedArray);
    
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
                    bool result = g->ds->atomicIncrementRandomK(tid, g->rngs[tid]);

                    // Count successful and total kcas operations
                    g->numTotalOps.inc(tid);
                    if (result) g->numSuccessfulOps.inc(tid);
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
    
    auto successfulOps = g->numSuccessfulOps.getTotal();
    auto numTotalOps = g->numTotalOps.getTotal();
    
    auto sumOfEntries = g->ds->getTotal(0 /* dummy thread ID */);
    cout<<"TOTAL="<<sumOfEntries<<endl;

    cout<<"Validation: # successful KCAS = "<<successfulOps<<" and K = "<<g->K<<" so array sum should be "<<(successfulOps*g->K)<<".";
    cout<<((successfulOps*g->K == sumOfEntries) ? " OK." : " FAILED.")<<endl;
    cout<<endl;

    cout<<"completed ops        : "<<numTotalOps<<endl;
    cout<<"throughput           : "<<(long long) (numTotalOps * 1000. / g->elapsedMillis)<<endl;
    cout<<"elapsed milliseconds : "<<g->elapsedMillis<<endl;
    cout<<endl;
    
    if (successfulOps*g->K != sumOfEntries) {
        cout<<"ERROR: validation failed!"<<endl;
        exit(-1);
    }
    
    delete g;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        cout<<"USAGE: "<<argv[0]<<" [options]"<<endl;
        cout<<"Options:"<<endl;
        cout<<"    -a [string]  algorithm name in { lockfree, unfinished }"<<endl;
        cout<<"    -t [int]     milliseconds to run"<<endl;
        cout<<"    -s [int]     size of array that KCAS will be performed on"<<endl;
        cout<<"    -n [int]     number of threads that will perform KCAS"<<endl;
        cout<<"    -k [int]     the K in KCAS (how many slots to operate on)"<<endl;
        cout<<endl;
        cout<<"Example: "<<argv[0]<<" -a lockfree -t 1000 -s 1000000 -n 8 -k 4"<<endl;
        return 1;
    }
    
    int millisToRun = -1;
    int arraySize = 0;
    int totalThreads = 0;
    int K = 0;
    char * alg = NULL;
    
    // read command line args
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-s") == 0) {
            arraySize = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            totalThreads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            millisToRun = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            alg = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0) {
            K = atoi(argv[++i]);
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
    PRINT(KCAS_MAXK);
    PRINT(K);
    PRINT(millisToRun);
    PRINT(arraySize);
    PRINT(totalThreads);
    cout<<endl;
    
    // check for too large thread count
    if (totalThreads >= MAX_THREADS) {
        std::cout<<"ERROR: totalThreads="<<totalThreads<<" >= MAX_THREADS="<<MAX_THREADS<<std::endl;
        return 1;
    }
    
    // check for size too small
    if (arraySize < KCAS_MAXK) {
        std::cout<<"ERROR: arraySize="<<arraySize<<" < KCAS_MAXK="<<KCAS_MAXK<<std::endl;
        return 1;
    }
    
    // check for missing alg name
    if (alg == NULL) {
        cout<<"Must specify algorithm name"<<endl;
        return 1;
    }
    
    // check for K too small or too large
    if (K < 2 || K > KCAS_MAXK) {
        cout<<"K must be between 2 and KCAS_MAXK (which is currently "<<KCAS_MAXK<<")."<<endl;
        cout<<"If you want to perform larger KCAS operations, increase KCAS_MAXK."<<endl;
        return 1;
    }
    
    // run experiment for the selected KCAS implementation
    if (!strcmp(alg, "lockfree")) {
        runExperiment<KCASLockFree<KCAS_MAXK>>(arraySize, millisToRun, totalThreads, K);
    } else if (!strcmp(alg, "unfinished")) {
        runExperiment<KCASUnfinished<KCAS_MAXK>>(arraySize, millisToRun, totalThreads, K);
    } else {
        cout<<"Bad algorithm name: "<<alg<<endl;
        return 1;
    }
    
    return 0;
}
