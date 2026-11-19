// Micro-benchmark harness for IndexedMinHeap and HashTable.
//
// Exits with status 2 and a clear message while the data structures are unimplemented
// so that nobody mistakes a trivially fast NotImplemented path for a result.
#include "aegisproxy/datastructures/hash_table.hpp"
#include "aegisproxy/datastructures/indexed_min_heap.hpp"

#include <chrono>
#include <cstdio>
#include <random>

int main() {
  constexpr int kOps = 200000;
  aegis::ds::IndexedMinHeap<int, int> heap;
  aegis::ds::HashTable<int, int> table;

  // Guard: if the core algorithms are still stubs, refuse to report anything.
  if (!heap.insert(0, 0) || !table.insert(0, 0)) {
    std::fprintf(stderr,
                 "bench_datastructures: IndexedMinHeap/HashTable are not implemented yet; "
                 "no benchmark results produced.\n");
    return 2;
  }

  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, 1 << 20);

  auto t0 = std::chrono::steady_clock::now();
  for (int i = 1; i < kOps; ++i)
    (void)heap.insert(i, dist(rng));
  for (int i = 0; i < kOps; ++i)
    (void)heap.update_key(i, dist(rng));
  while (!heap.empty())
    (void)heap.extract_min();
  auto t1 = std::chrono::steady_clock::now();

  for (int i = 1; i < kOps; ++i)
    (void)table.insert(i, i);
  for (int i = 0; i < kOps; ++i)
    (void)table.find(i);
  for (int i = 0; i < kOps; ++i)
    (void)table.erase(i);
  auto t2 = std::chrono::steady_clock::now();

  const auto heap_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const auto table_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
  std::printf("IndexedMinHeap: %d insert + %d update_key + %d extract_min in %.1f ms\n", kOps, kOps,
              kOps, heap_ms);
  std::printf("HashTable:      %d insert + %d find + %d erase in %.1f ms\n", kOps, kOps, kOps,
              table_ms);
  return 0;
}
