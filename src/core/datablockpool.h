#pragma once

#include <list>
#include <mutex>
#include <vector>

namespace Core {

class DataBlockPool {
public:
  DataBlockPool();
  ~DataBlockPool();
  char* getBlock();
  const int blockSize() const;
  void returnBlock(char* block);
private:
  void allocateNewBlocks();
  std::list<char*> blocks;
  std::list<char*> availableblocks;
  int totalblocks;
  std::mutex blocklock;

  // Thread-local cache support for reduced lock contention
  static constexpr int TL_CACHE_SIZE = 8;
  struct ThreadLocalCache {
    std::vector<char*> blocks;
    ThreadLocalCache() { blocks.reserve(TL_CACHE_SIZE); }
  };
  static thread_local ThreadLocalCache tlcache;
  void refillThreadLocalCache();
};

} // namespace Core
