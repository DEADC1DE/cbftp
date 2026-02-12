#include "datablockpool.h"

#include <cstdlib>

namespace Core {

#define BLOCKSIZE 16384

thread_local DataBlockPool::ThreadLocalCache DataBlockPool::tlcache;

DataBlockPool::DataBlockPool() : totalblocks(0) {
  std::lock_guard<std::mutex> lock(blocklock);
  allocateNewBlocks();
}

DataBlockPool::~DataBlockPool()
{
  std::lock_guard<std::mutex> lock(blocklock);
  for (char* block : blocks) {
    free(block);
  }
}

char* DataBlockPool::getBlock() {
  if (!tlcache.blocks.empty()) {
    char* block = tlcache.blocks.back();
    tlcache.blocks.pop_back();
    return block;
  }
  refillThreadLocalCache();
  if (!tlcache.blocks.empty()) {
    char* block = tlcache.blocks.back();
    tlcache.blocks.pop_back();
    return block;
  }
  // Fallback: direct allocation under lock
  std::lock_guard<std::mutex> lock(blocklock);
  if (availableblocks.empty()) {
    allocateNewBlocks();
  }
  char* block = availableblocks.back();
  availableblocks.pop_back();
  return block;
}

const int DataBlockPool::blockSize() const {
  return BLOCKSIZE;
}

void DataBlockPool::returnBlock(char* block) {
  if (static_cast<int>(tlcache.blocks.size()) < TL_CACHE_SIZE * 2) {
    tlcache.blocks.push_back(block);
    return;
  }
  std::lock_guard<std::mutex> lock(blocklock);
  availableblocks.push_back(block);
}

void DataBlockPool::refillThreadLocalCache() {
  std::lock_guard<std::mutex> lock(blocklock);
  for (int i = 0; i < TL_CACHE_SIZE; ++i) {
    if (availableblocks.empty()) {
      allocateNewBlocks();
    }
    tlcache.blocks.push_back(availableblocks.back());
    availableblocks.pop_back();
  }
}

void DataBlockPool::allocateNewBlocks() {
  for (int i = 0; i < 10; i++) {
    char* block = (char*) malloc(BLOCKSIZE);
    blocks.push_back(block);
    availableblocks.push_back(block);
    totalblocks++;
  }
}

} // namespace Core
