/*
 *
 * Author: Aasheesh Kolli
 *
 */
#include <iostream>

#include "persist_buffer.hh"

// Constructor for the persist buffer entry
PersistBufferEntry::PersistBufferEntry() {
    persistReq = nullptr;
    persistPkt = nullptr;
    addr = 0;
    hasDependents = false;
    isPulledToMSHR = false;
    isBarrier = false;
    id = -1;
}

PersistBufferEntry::~PersistBufferEntry() {
}

void
PersistBufferEntry::setPersistReq(RequestPtr _persistReq) {
    persistReq = _persistReq;
}

RequestPtr
PersistBufferEntry::getPersistReq() {
    return persistReq;
}

void
PersistBufferEntry::setPersistPkt(PacketPtr _persistPkt) {
    persistPkt = _persistPkt;
}

PacketPtr
PersistBufferEntry::getPersistPkt() {
    return persistPkt;
}

void
PersistBufferEntry::addDependencies(std::vector<PersistDependency> _dependencies) {
  std::vector<PersistDependency>::iterator it;
  // Iterate over all the _dependencies
  for(it = _dependencies.begin(); it != _dependencies.end(); it++) {
      if (dependencies.find((*it).getPersistBufferId()) == dependencies.end()) {
          // If the dependency is to a buffer (from another core) we haven't seen before
          // then add it to out map
          dependencies[(*it).getPersistBufferId()] = (*it).getPersistId();
      } else {
          // If the dependency we are checking is younger than what we already have, update it
          if (dependencies[(*it).getPersistBufferId()] < (*it).getPersistId()) {
              dependencies[(*it).getPersistBufferId()] = (*it).getPersistId();
          }
      }
  }
}

void
PersistBufferEntry::resolveDependency(void* _persistBufferId, int _persistId) {
  print();
  // Iterate over all of our dependencies and if our dependency to the particular buffer
  // is older than what we are resolving for, then remove our dependecy to that buffer
  if (dependencies.find(_persistBufferId) != dependencies.end()) {
      if (dependencies[_persistBufferId] <= _persistId) {
        dependencies.erase(_persistBufferId);
      }
  }
}

bool
PersistBufferEntry::isAllDependenciesResolved() {
  return dependencies.size() == 0;
}

void
PersistBufferEntry::resetEntry() {
  persistReq = nullptr;
  persistPkt = nullptr;
  addr = 0;
  hasDependents = false;
  isPulledToMSHR = false;
  id = -1;
  isBarrier = false;

  dependencies.clear();
  loads.clear();
  stores.clear();
}

void
PersistBufferEntry::print() {
  std::cout<<" persistID: "<<id;
  std::cout<<" Addr: "<<addr;
  if (isBarrier) std::cout<<" isBarrier: "<<"yes";
  else std::cout<<" isBarrier: "<<"no";
  if (isPulledToMSHR) std::cout<<" isPulledToMSHR: "<<"yes";
  else std::cout<<" isPulledToMSHR: "<<"no";
  if (hasDependents) std::cout<<" hasDependents: "<<"yes";
  else std::cout<<" hasDependents: "<<"no";
  std::cout<<" pkt: "<<persistPkt;
  std::cout<<" req: "<<persistReq;
  std::cout<<" dependencies: "<<dependencies.size();
  std::cout<<" loads: "<<loads.size();
  std::cout<<" stores: "<<stores.size();
  std::cout<<std::endl;
  if (dependencies.size()) {
    std::cout<<"Dependencies:"<<std::endl;
    std::map<void*,int>::iterator it;
    for (it = dependencies.begin(); it != dependencies.end(); it++) {
      std::cout<<it->first<<" "<<it->second<<std::endl;
    }
  }
}

PersistBuffer::PersistBuffer(int _size, int _maxBarriers) {
  bufferEntries = new PersistBufferEntry[_size];
  head = 0;
  tail = 0;
  mshrHead = 0;
  numPersists = 0;
  blkSize = 64;
  size = _size;
  maxBarriers = _maxBarriers;
}

PersistBuffer::~PersistBuffer() {
  delete bufferEntries;
}

int
PersistBuffer::getCurrentSize() {
  if(tail >= head) {
    return (tail - head);
  } else {
    return (size - head + tail);
  }
}

bool
PersistBuffer::isFull() {
  return ((getCurrentSize() == (size-1)) || getNumActiveBarriers() == maxBarriers);
}

bool
PersistBuffer::isEmpty() {
  return (getCurrentSize() == 0);
}

void
PersistBuffer::addNewEntry(PacketPtr _persistPkt, std::vector<PersistDependency> _dependencies) {
  assert(!isFull());
  if (tryCoalescing(_persistPkt, _dependencies))
    return;
  
  bufferEntries[tail].setPersistPkt(_persistPkt);
  bufferEntries[tail].setPersistReq(_persistPkt->req);
  bufferEntries[tail].setAddr(_persistPkt->getAddr());
  bufferEntries[tail].resetHasDependents();
  bufferEntries[tail].setPersistId(numPersists);
  if (!_persistPkt->isMemBar()) {
      bufferEntries[tail].addDependencies(_dependencies);
  } else {
      bufferEntries[tail].addDependencies(nextBarrierDependencies);
      nextBarrierDependencies.clear();
      bufferEntries[tail].setIsBarrier();
  }

  if (tail == size -1) {
      tail =0;
  } else {
      tail++;
  }

  numPersists++;
}

bool
PersistBuffer::tryCoalescing(PacketPtr _persistPkt, std::vector<PersistDependency> _dependencies) {
  if (isEmpty()) return false;

  if (_dependencies.size()) return false;

  if (bufferEntries[tail-1].getAddr() == blockAlign(_persistPkt->getAddr())) {
      if (!bufferEntries[tail-1].getHasDependents() && bufferEntries[tail-1].isAllDependenciesResolved() && !bufferEntries[tail-1].getIsPulledToMSHR()) {
          bufferEntries[tail-1].setPersistPkt(_persistPkt);
          bufferEntries[tail-1].setPersistReq(_persistPkt->req);
          return true;
      } else {
          return false;
      }
  }
  return false;
}

void
PersistBuffer::removeHead(PacketPtr _persistPkt) {
  assert(!isEmpty());
  print();
  assert(bufferEntries[head].getPersistReq() == _persistPkt->req);

  bufferEntries[head].resetEntry();
  if (head == size -1) {
      head = 0;
  } else {
      head++;
  }
}

int
PersistBuffer::findReq(PacketPtr _pkt) {
  Addr addr = blockAlign(_pkt->getAddr());
  if (isEmpty()) return -1;

  // Since our buffer buffer is circular, based on the values of head
  // and tail, the active entries vary and we ahve to check for differnt
  // cases
  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            if (_pkt->isRead()) {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            } else {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
                if (bufferEntries[i].isLoadHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            }
        }else if(bufferEntries[i].getAddr() == addr) {
            if (!bufferEntries[i].getHasDependents()) {
                if (_pkt->needsExclusive()) {
                  bufferEntries[i].setHasDependents();
                }
                return bufferEntries[i].getPersistId();
            } else {
                return -1;
            }

        }
    }

    for(int i = size-1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            if (_pkt->isRead()) {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            } else {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
                if (bufferEntries[i].isLoadHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            }
        }else if(bufferEntries[i].getAddr() == addr) {
            if (!bufferEntries[i].getHasDependents()) {
                if (_pkt->needsExclusive()) {
                  bufferEntries[i].setHasDependents();
                }
                return bufferEntries[i].getPersistId();
            } else {
                return -1;
            }
        }
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            if (_pkt->isRead()) {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            } else {
                if (bufferEntries[i].isStoreHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
                if (bufferEntries[i].isLoadHit(addr)) {
                    return bufferEntries[i].getPersistId();
                }
            }
        }else if(bufferEntries[i].getAddr() == addr) {
            if (!bufferEntries[i].getHasDependents()) {
                if (_pkt->needsExclusive()) {
                  bufferEntries[i].setHasDependents();
                }
                return bufferEntries[i].getPersistId();
            } else {
                return -1;
            }
        }
    }

  }
  return -1;
}

void
PersistBuffer::resolveDependencies(PacketPtr _pkt) {

  if(isEmpty()) return;

  void* persistBufferId = _pkt->persistBufferId;
  int persistId = _pkt->persistId;

  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
            bufferEntries[i].resolveDependency(persistBufferId, persistId);
    }

    for(int i = size-1; i>=head; i--) {
            bufferEntries[i].resolveDependency(persistBufferId, persistId);
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
            bufferEntries[i].resolveDependency(persistBufferId, persistId);
    }
  }
  return;
}

PacketPtr
PersistBuffer::getMSHRHeadPersistPkt(std::map<void*, int> latestDependencies) {
  if(isEmpty()) {
    return nullptr;
  }

  if (mshrHead == tail) return nullptr; // Nothing to send

  if (tail > head) {
    assert((mshrHead >= head) && (mshrHead < tail));
  } else {
    assert(mshrHead <  tail || mshrHead>= head);
  }

  std::map<void*, int>::iterator it;
  for (it = latestDependencies.begin(); it != latestDependencies.end(); it++) {
      bufferEntries[mshrHead].resolveDependency(it->first, it->second);
  }

  if (!bufferEntries[mshrHead].isAllDependenciesResolved()) {
    return nullptr;
  }


  if (bufferEntries[mshrHead].getIsPulledToMSHR()) {
    return nullptr;
  }

  bufferEntries[mshrHead].setIsPulledToMSHR();
  PacketPtr pkt = bufferEntries[mshrHead].getPersistPkt();

  if (mshrHead == size - 1) {
    mshrHead = 0;
  } else {
    mshrHead++;
  }
  return pkt;
}

int
PersistBuffer::getHeadPersistId() {
  if(isEmpty()) {
    return -1;
  }

  int temp = bufferEntries[head].getPersistId();
  return temp;
}

int
PersistBuffer::findYoungestBarrier() {
  if (isEmpty()) return -1;

  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            return i;
        }
    }

    for(int i = size-1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            return i;
        }
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            return i;
        }
    }

  }
  return -1;
}

int
PersistBuffer::getNumActiveBarriers() {

  int numBarriers = 0;
  if (isEmpty()) return numBarriers;

  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            numBarriers++;
        }
    }

    for(int i = size-1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            numBarriers++;
        }
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
        if (bufferEntries[i].getIsBarrier()) {
            numBarriers++;
        }
    }

  }
  return numBarriers;
}

void
PersistBuffer::print() {
  std::cout<<"**** Persist buffer start ****"<<std::endl;
  if (!isEmpty()) std::cout<<"Non-Zero number of entries, head: "<<head<<" tail: "<<tail<<std::endl;

  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
        std::cout<<"PB Indx: "<<i;
        bufferEntries[i].print();
    }

    for(int i = size-1; i>=head; i--) {
        std::cout<<"PB Indx: "<<i;
        bufferEntries[i].print();
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
        std::cout<<"PB Indx: "<<i;
        bufferEntries[i].print();
    }

  }


  std::cout<<"**** Persist buffer end ****"<<std::endl;
}

bool
PersistBuffer::isPersistIdBarrier(int persistId) {
  if (isEmpty()) return false;

  if (tail < head) {
    for(int i = tail-1; i>=0; i--) {
        if(bufferEntries[i].getPersistId() == persistId && bufferEntries[i].getIsBarrier()) return true;
    }

    for(int i = size-1; i>=head; i--) {
        if(bufferEntries[i].getPersistId() == persistId && bufferEntries[i].getIsBarrier()) return true;
    }
  } else {
    for(int i = tail -1; i>=head; i--) {
        if(bufferEntries[i].getPersistId() == persistId && bufferEntries[i].getIsBarrier()) return true;
    }

  }

  return false;
}

void
PersistBuffer::addToBloomFilter(PacketPtr _pkt) {
  Addr addr = blockAlign(_pkt->getAddr());
  int barrierIndx = findYoungestBarrier();
  if (barrierIndx == -1) return;

  assert(bufferEntries[barrierIndx].getIsBarrier());

  if (_pkt->isRead()) {
      bufferEntries[barrierIndx].addLoad(addr);
                while(_pkt->req->getNumPersistDependencies()) {
                  nextBarrierDependencies.push_back(_pkt->req->getPersistDependency());
                }
  } else {
      if (addr > 0x20000000) {
        // Persist store, will get its own entry,
        // dont have to add to bloom filter
        return;
      }
      bufferEntries[barrierIndx].addStore(addr);
      while(_pkt->req->getNumPersistDependencies()) {
            nextBarrierDependencies.push_back(_pkt->req->getPersistDependency());
      }
  }

}
