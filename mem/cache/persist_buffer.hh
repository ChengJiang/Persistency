/*
 *
 * Author: Aasheesh Kolli
 * This file defines the interface for the persist buffer and its entries
 *
 */

#include <map>
#include <vector>

#include "mem/packet.hh"
#include "mem/request.hh"

//#include "mem/persist_dependency.hh"

#define BUFFER_SIZE 1024

// A persist buffer entry class. Stores each individual persist req.
class PersistBufferEntry {
    private:
        RequestPtr persistReq; // The req ptr of the persist
        PacketPtr persistPkt; // the pkt ptr of the persist
        Addr addr; // The addr of the persist
        std::map<void*, int> dependencies; // All the dependencies of the persist
        bool hasDependents; // Does this persist have any dependents in other buffers?
        bool isPulledToMSHR; // Has this persist been pulled to the mshr
        int id; // The id of the persist
        bool isBarrier; // Is this entry storing a barrier?
        std::map<Addr, bool> loads; // The load bloom filter
        std::map<Addr, bool> stores; // The store bloom filter
    public:
        PersistBufferEntry();
        ~PersistBufferEntry();
        // Accessor functions for the persist req
        void setPersistReq(RequestPtr _persistReq);
        RequestPtr getPersistReq();
        // Accessor functions for the persist pkt
        void setPersistPkt(PacketPtr _persistPkt);
        PacketPtr getPersistPkt();
        // Accessor functions for the persist id
        void setPersistId(int _id) {id = _id;}
        int getPersistId() {return id;}
        // Accessor function to add dependencies
        void addDependencies(std::vector<PersistDependency> _dependencies);
        // Accessor function for hasDependents
        void setHasDependents() {hasDependents = true;}
        void resetHasDependents() {hasDependents = false;}
        bool getHasDependents(){return hasDependents;}
        // Accessor function for addr
        void setAddr(Addr _addr) {addr = _addr;}
        Addr getAddr() {return addr;}
        // Reset the entry for reuse
        void resetEntry();
        // Accessor functions for pulledToMSHR
        void setIsPulledToMSHR() {isPulledToMSHR = true;}
        bool getIsPulledToMSHR() {return isPulledToMSHR;}
        // Resolves the dependencies for the persist buffer entry
        void resolveDependency(void* persistBufferId, int _persistId);
        // Have all the dependencies of the entry been resolved?
        bool isAllDependenciesResolved();
        // Accessor functions for isBarrier
        void setIsBarrier() {isBarrier = true;}
        void resetIsBarrier() {isBarrier = false;}
        bool getIsBarrier() {return isBarrier;}
        // Check if an addr hits in the load bloom filter
        bool isLoadHit(Addr _addr) {return loads.find(_addr) != loads.end();}
        // Check if an addr hits in the store bloom filter
        bool isStoreHit(Addr _addr) {return stores.find(_addr) != stores.end();}
        // Add an addr to the load bloom filter
        void addLoad(Addr _addr) {loads[_addr] = true;}
        // Add an addr to the store bloom filter
        void addStore(Addr _addr) {stores[_addr] = true;}
        // Print the status of the entry
        void print();
};

// Persist buffer class, stores all the persists, resolves their dependencies, and
// pushes them towards NVM. Implemented as a circular buffer
class PersistBuffer {
    private:
        PersistBufferEntry* bufferEntries; // An array of persist buffer entries
        int head; // Head entry
        int tail; // Tail Entry
        int mshrHead; // Head MSHR entry. All the entries between head and mshrHead
                      // pushed to the MSHR
        int numPersists; // Increasing counter for num of persists+barriers seen, used
                         // to set id
        std::vector<PersistDependency> nextBarrierDependencies; // Stores all the deps
                                                                // to be assigned to the
                                                                // next barrier
        int blkSize; // cache block size
        int size; // The size of the persist buffer (in terms of number of entries)
        int maxBarriers; // max num of barriers allowed in the buffer
    public:
        PersistBuffer(int _size, int _maxBarriers);
        ~PersistBuffer();

        // Get the current size (num of valid entries) of the persist buffer
        int getCurrentSize();

        // Is the buffer full?
        bool isFull();

        // Is the buffer empty?
        bool isEmpty();

        // Add a new entry to the buffer, must check if the buffer is full before calling
        // this function
        void addNewEntry(PacketPtr _pkt, std::vector<PersistDependency> _dependencies);

        // Try coalescing the new req with the existing entries in the buffer
        bool tryCoalescing(PacketPtr _pkt, std::vector<PersistDependency> _dependencies);

        // Remove the entry at the head (because it has been serviced)
        void removeHead(PacketPtr _pkt);

        // Snoop the persist buffer to find a hit, could be a persist or could hit in the
        // bloom filter associated with a barrier
        int findReq(PacketPtr _pkt);

        // Returns the pkt ptr of the next persist that can be pushed to the MSHR
        PacketPtr getMSHRHeadPersistPkt(std::map<void*, int> latestDependencies);

        // Return the persist id of the head entry.
        int getHeadPersistId();

        // Given the persist id of a pkt we observe, resolve dependencies for all the
        // entries in the buffer.
        void resolveDependencies(PacketPtr _pkt);

        // Find the youngest barrier in the buffer and return its index
        int findYoungestBarrier();

        // Add a store/load to the bloom filter of the youngest barrier
        void addToBloomFilter(PacketPtr _pkt);

        // Block align an address
        Addr blockAlign(Addr addr) {return (addr & ~(Addr(blkSize-1)));}

        // Returns true if the persist id belongs to a barrier in the buffer
        bool isPersistIdBarrier(int persistId);

        // Print the contents of the buffer
        void print();

        // Find the number of active barriers in the buffer
        int getNumActiveBarriers();
 };

