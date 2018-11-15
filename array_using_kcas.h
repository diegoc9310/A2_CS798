/**
 * 
 * This is a silly array based data structure that offers only one operation:
 * Choose K random locations and increment them atomically (using KCAS).
 * 
 * It serves as the main work horse in our benchmark, and also serves as an
 * example of how you can use KCAS in a data structure
 * (via the KCAS provider interface I have defined).
 * 
 */

#pragma once

#include <algorithm>

template<class KCASProviderType>
class ArrayUsingKCAS {
public:
    volatile char padding0[PADDING_BYTES];
    KCASProviderType provider;
    casword_t * data;
    const int size;
    const int K;
    volatile char padding1[PADDING_BYTES];

    ArrayUsingKCAS(const int _size, const int _K) : size(_size), K(_K) {
        const int dummyTid = 0;
        data = new casword_t[_size];
        for (int i=0;i<_size;++i) {
            provider.writeInitVal(dummyTid, &data[i], 0);
        }
    }
    ~ArrayUsingKCAS() {
        delete[] data;
    }
    bool atomicIncrementRandomK(const int tid, PaddedRandom & rng) {
        /**
         * 
         * Choose K indices to perform KCAS on:
         * Pick the first index randomly,
         * then just take consecutive indices starting from there.
         * 
         */
        int ix[K];
        ix[0] = rng.nextNatural() % size;
        for (int i=1;i<K;++i) {
            ix[i] = (ix[i-1] + 1) % size;
        }
        
        // Create a new KCAS descriptor and populate it with rows containing: addr, exp, new
        auto ptr = provider.getDescriptor(tid);
        for (int i=0;i<K;++i) {
            casword_t * addr = &data[ix[i]];
            casword_t oldval = provider.readVal(tid, &data[ix[i]]);
            casword_t newval = oldval+1;
            ptr->addValAddr(addr, oldval, newval);
        }
        
        // Perform the actual kcas
        bool result = provider.kcas(tid, ptr);
        
        return result;
    }
    long long getTotal(const int tidForReading) {
        long long result = 0;
        for (int i=0;i<size;++i) {
            result += provider.readVal(tidForReading, &data[i]);
        }
        return result;
    }
};
