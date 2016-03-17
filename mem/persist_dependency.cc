#include "persist_dependency.hh"

PersistDependency::PersistDependency() {
          persistBufferId = nullptr;
          persistId = 0;
}

PersistDependency::PersistDependency(void* _persistBufferId, int _persistId) {
         persistBufferId = _persistBufferId;
         persistId = _persistId;
}

PersistDependency::~PersistDependency() {
}

void 
PersistDependency::setPersistBufferId(void* _persistBufferId) {
            persistBufferId = _persistBufferId;
}

void
PersistDependency::setPersistId(int _persistId) {
            persistId = _persistId;
}

void*
PersistDependency::getPersistBufferId() {
            return persistBufferId;
}

int
PersistDependency::getPersistId() {
            return persistId;
}
