#pragma once

#define casword_t uintptr_t
#define descriptor_t typename KCASUnfinished<MAX_K>::kcas_desc_t

template <int MAX_K>
class KCASUnfinished {
public:
    /**
     * KCAS descriptor type
     * (just used to provide well-formed variable length args to kcas())
     */
    class kcas_desc_t {
    public:
        struct kcas_entry_t { // really just part of kcas_desc_t, not a standalone descriptor
            casword_t volatile * addr;
            casword_t oldval;
            casword_t newval;
        };
        volatile char padding[PADDING_BYTES]; // add padding to prevent false sharing
        casword_t numEntries;
        kcas_entry_t entries[MAX_K];

        void addValAddr(casword_t * addr, casword_t oldval, casword_t newval) {
            entries[numEntries].addr = addr;
            entries[numEntries].oldval = oldval;
            entries[numEntries].newval = newval;
            ++numEntries;
            assert(numEntries <= MAX_K);
            //std::cout<<"addr="<<(size_t) addr<<" oldval="<<oldval<<" newval="<<newval<<" numEntries="<<std::endl;
        }

        void addPtrAddr(casword_t * addr, casword_t oldval, casword_t newval) {
            entries[numEntries].addr = addr;
            entries[numEntries].oldval = oldval;
            entries[numEntries].newval = newval;
            ++numEntries;
            assert(numEntries <= MAX_K);
            //std::cout<<"addr="<<(size_t) addr<<" oldval="<<oldval<<" newval="<<newval<<" numEntries="<<std::endl;
        }
    };

private:
    kcas_desc_t perThreadDescriptors[MAX_THREADS+1] __attribute__ ((aligned(64))); // allocate one extra cell to pad the rightmost array endpoint

public:
    KCASUnfinished();
    casword_t readPtr(const int tid, casword_t volatile * addr);
    casword_t readVal(const int tid, casword_t volatile * addr);
    void writeInitPtr(const int tid, casword_t volatile * addr, casword_t const newval);
    void writeInitVal(const int tid, casword_t volatile * addr, casword_t const newval);
    bool kcas(const int tid, kcas_desc_t * ptr);
    kcas_desc_t * getDescriptor(const int tid);
private:
    // your private functions here
};

template <int MAX_K>
KCASUnfinished<MAX_K>::KCASUnfinished() {
    memset(perThreadDescriptors, 0, sizeof(perThreadDescriptors));
}

// TODO: maybe replace crappy bubble sort with something fast for large MAX_K (maybe even use insertion sort for small MAX_K)
template <int MAX_K>
static void kcasdesc_sort(descriptor_t * ptr) {
    descriptor_t::kcas_entry_t temp;
    bool changed = false;
    for (int i = 0; i < ptr->numEntries; i++) {
        for (int j = 0; j < ptr->numEntries - i - 1; j++) {
            if (ptr->entries[j].addr > ptr->entries[j + 1].addr) {
                temp = ptr->entries[j];
                ptr->entries[j] = ptr->entries[j + 1];
                ptr->entries[j + 1] = temp;
                changed = true;
            }
        }
        if (!changed) break;
    }
}

template <int MAX_K>
bool KCASUnfinished<MAX_K>::kcas(const int tid, descriptor_t * ptr) {
    // sort entries in the kcas descriptor to guarantee progress
    kcasdesc_sort<MAX_K>(ptr);

    // incomplete implementation
    
    return 0;
}

template <int MAX_K>
casword_t KCASUnfinished<MAX_K>::readPtr(const int tid, casword_t volatile * addr) {
    return *addr;
}

template <int MAX_K>
casword_t KCASUnfinished<MAX_K>::readVal(const int tid, casword_t volatile * addr) {
    return *addr;
}

template <int MAX_K>
void KCASUnfinished<MAX_K>::writeInitPtr(const int tid, casword_t volatile * addr, casword_t const newval) {
    *addr = newval;
}

template <int MAX_K>
void KCASUnfinished<MAX_K>::writeInitVal(const int tid, casword_t volatile * addr, casword_t const newval) {
    *addr = newval;
}

template <int MAX_K>
descriptor_t * KCASUnfinished<MAX_K>::getDescriptor(const int tid) {
    perThreadDescriptors[tid].numEntries = 0;
    return &perThreadDescriptors[tid];
}
