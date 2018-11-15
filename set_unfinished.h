/**
 * This is the interface I want your Set to follow.
 * Copy this, rename the class, and fill in the implementation.
 */

#pragma once
#include <cassert>
#include <pthread.h>
#include <immintrin.h>
#include <iostream>

uint32_t murmur(int k) {
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


class SetUnfinished {
public:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int size;
    volatile char padding1[PADDING_BYTES];

    SetUnfinished(const int _numThreads, const int _size);
    ~SetUnfinished();
    bool insertIfAbsent(const int tid, const int & key); // try to insert key; return true if successful (if it doesn't already exist), false otherwise
    bool erase(const int tid, const int & key); // try to erase key; return true if successful, false otherwise
    long getSumOfKeys(); // should return the sum of all keys in the set
    void printDebuggingDetails(); // print any debugging details you want at the end of a trial in this function
};

SetUnfinished::SetUnfinished(const int _numThreads, const int _size)
: numThreads(_numThreads), size(_size) {
    
}

SetUnfinished::~SetUnfinished() {
    // destructor
}

bool SetUnfinished::insertIfAbsent(const int tid, const int & key) {
    return false;
}

bool SetUnfinished::erase(const int tid, const int & key) {
    return false;
}

long SetUnfinished::getSumOfKeys() {
    return 0;
}

void SetUnfinished::printDebuggingDetails() {
    
}



class Hlock {
public:
   int padding = 16;
   static const int EMPTY = 0;
   static const int TOMBSTONE = -1;
   volatile char padding0[PADDING_BYTES];
   const int numThreads;
   volatile char padding1[PADDING_BYTES];
   volatile uint64_t size;
   volatile char padding2[PADDING_BYTES];
   TryLock lock;
   volatile char padding3[PADDING_BYTES];
   int * data;
   volatile char padding4[PADDING_BYTES];
   volatile int64_t  *approx_counter_shards;
   volatile char padding5[PADDING_BYTES];
   volatile uint64_t approx_addition = 0;
   volatile char padding6[PADDING_BYTES];
   Sharded succeed_transactions;
   volatile char padding7[PADDING_BYTES];
   Sharded failed_transactions;
   volatile char padding8[PADDING_BYTES];
   Sharded lock_failed_transactions;
   volatile char padding9[PADDING_BYTES];
   Sharded expansion_transaction;
   volatile char padding10[PADDING_BYTES];
   Sharded expansion_regular;
   volatile char padding11[PADDING_BYTES];
   
   Hlock(const int _numThreads, const int _size);
   ~Hlock();
   int insertIfAbsent(const int tid, const int & key); // try to insert key; return true if successful (if it doesn't already exist), false otherwise
   bool erase(const int tid, const int & key); // try to erase key; return true if successful, false otherwise
   long getSumOfKeys(); // should return the sum of all keys in the set
   void printDebuggingDetails(); // print any debugging details you want at the end of a trial in this function
   int insertHTM(const int tid, const int & key); //  insert
   bool eraseHTM(const int tid, const int & key);
   void expand();
   int64_t inc(int tid);
   int64_t read();
};

Hlock::Hlock(const int _numThreads, const int _size)
   : numThreads(_numThreads)
   , size(2 * _size) {
   succeed_transactions.init(numThreads);
   failed_transactions.init(numThreads);
   lock_failed_transactions.init(numThreads);
   expansion_transaction.init(numThreads);
   expansion_regular.init(numThreads);
   data = new int[size];
   approx_counter_shards = new int64_t[_numThreads *padding];
   lock.release();

#pragma omp parallel for
   for (int i = 0; i < size; ++i) {
      data[i] = EMPTY;
   }
#pragma omp parallel for
   for (int i = 0; i < _numThreads * padding; i += padding){ 
      approx_counter_shards[i*padding] = 0; 
   }

}

Hlock::~Hlock() {
   delete[] data;// destructor
}

int Hlock::insertIfAbsent(const int tid, const int & key) {
   assert(EMPTY != key && TOMBSTONE != key);\
   


      int retriesLeft = 5;
      unsigned status = _XABORT_EXPLICIT;
      bool result = 0;
   retry:
      status = _xbegin();
      if (status == _XBEGIN_STARTED)
      {
          int64_t current_counts = read();
         if (current_counts > (size/2))
            {
              while (lock.tryAcquire() == false) { /* wait */ }
            expand();
            lock.release();
            expansion_transaction.inc(tid);
            return 2;
            }
         if ((lock.isHeld() == true)) { 
             lock_failed_transactions.inc(tid);
             _xabort(_XABORT_CODE(7)); 
         }
          result = insertHTM(tid, key);
         _xend();
         succeed_transactions.inc(tid);
         return result;
      }

      else {
          failed_transactions.inc(tid);
         while (lock.isHeld() == true) { /* wait */ }
         if (--retriesLeft > 0) { goto retry; }
         while (lock.tryAcquire() == false) { /* wait */ }
         int64_t current_counts = read();
         if (current_counts > (size/2))
         {
            expand();
            lock.release();
            expansion_regular.inc(tid);
            return 2;
         }
             result = insertHTM(tid, key);
            lock.release();
            return result;

      }
   return 0;
 
}



int Hlock::insertHTM(const int tid, const int & key) {

   unsigned int const hash = murmur(key);
   for (unsigned int i = 0; i < size; ++i) {

      unsigned int const index = (hash + i) % size;
       int found = data[index];

      if (found == key) {
         return 0;
      }

      else if (found == EMPTY) {
         data[index] = key;
         inc(tid);
         return 1;
      }
   }
  
   return 0;
}



bool Hlock::erase(const int tid, const int & key) {
   assert(EMPTY != key && TOMBSTONE != key);
      int retriesLeft = 5;
      bool result = false;
      unsigned status = _XABORT_EXPLICIT;
   retry:
      status = _xbegin();
      if (status == _XBEGIN_STARTED)
      {
         if ((lock.isHeld() == true)) { _xabort(1); }
          result = eraseHTM(tid, key);
         _xend();
         return result;
      }
      else {
         while (lock.isHeld() == true) { /* wait */ }
         if (--retriesLeft > 0) { goto retry; }
         while (lock.tryAcquire() == false) { /* wait */ }
         result = eraseHTM(tid, key);
         lock.release();
         return result;
      }
   return false;
}

bool Hlock::eraseHTM(const int tid, const int & key) {
   unsigned int const hash = murmur(key);

   for (unsigned int i = 0; i < size; ++i) {
      unsigned int const index = (hash + i) % size;
       int found = data[index];
      if (found == key){
         data[index] = TOMBSTONE;
         return true;
      }
      else if (found == EMPTY){
         return false; // did not find key
      }
   }
   return false;
}


// Check Sum of keys////////////////////////////////////////////////////////////
long Hlock::getSumOfKeys() {
   long sum = 0;
#pragma omp parallel for reduction(+:sum)
   for (int i = 0; i < size; ++i) {
      int v = data[i];
      if (v != TOMBSTONE && v != EMPTY) sum += v;
   }
   return sum;
}
////////////////////////////////////////////////////////////////////////////////

// Debug print//////////////////////////////////////////////////////////////////
void Hlock::printDebuggingDetails() {
   cout << "succeed_transactions: " <<succeed_transactions.read() << endl;
   cout << "failed_transactions: " <<failed_transactions.read() << endl;
   cout << "lock_failed_transactions: " <<lock_failed_transactions.read() << endl;
   cout << "expansion_transaction: " <<expansion_transaction.read() << endl;
   cout << "expansion_regular: " <<expansion_regular.read() << endl;

}
////////////////////////////////////////////////////////////////////////////////

//Expansion of hash table///////////////////////////////////////////////////////
void Hlock::expand() {

   uint64_t old_size = size;
   
   size = size * 2;
   
   int* new_data = new int[size]; //new size

   for (int i = 0; i < size; ++i) {
      new_data[i] = EMPTY;
   }

   //save data here with rehashed values
   for (int i = 0; i < old_size; ++i) {
      if (EMPTY != data[i] && TOMBSTONE != data[i])
      {
         unsigned int const hash = murmur(data[i]);
         int x = 0;
         do {
            unsigned int const index = (hash + x) % size;
            if (new_data[index] == EMPTY) {
               new_data[index] = data[i];
               break;
            }
            else { x++; }
         } while (x < size);
         assert(x < size); //-DNDEBUG remove asserts from complile
      }
   }
   //cout << "Expansion Succed" << endl;
   delete[] data;
   data= new_data;
}
////////////////////////////////////////////////////////////////////////////////

// Approximate counter implementation for resizing Hash table///////////////////
int64_t Hlock::inc(int tid)
{
   approx_counter_shards[tid * padding]++;
   if (approx_counter_shards[tid * padding] >= 5000)
   {
      int64_t w = approx_counter_shards[tid *padding];
      approx_counter_shards[tid * padding] = 0;
      __sync_add_and_fetch(&approx_addition, w);
   }
   return approx_addition;
}

int64_t Hlock::read()
{
   return approx_addition;
}
////////////////////////////////////////////////////////////////////////////////