#pragma once

namespace blobs::server {

class MemoryBlock;
class Database;

static_assert(!"Remove the static functions, turn this into an object");

/** Just a collection of static data members and static functions to manage the LRU caching
 */
class LRUCache {
public:
  using Element = MemoryBlock;

  /** Returns true if a maximum memory size has been configured and LRU caching should be used to guarantee that
   */
  static bool IsEnabled();

  /** This function moves the element to the head of the LRU cache (if the element is part of the lru cache)
   *  Otherwise this method does nothing.
   */
  static void Touch(Element& element);

  /** Enters the element into the LRU cache. This will increment only the `evictableSize` by the element's EvictSize().
   *  usedMemory is not touched by this.
   */
  static void Enter(Element& element);

  /** Removes the element from the LRU cache (usually upon deletion or eviction). This will decrement only the `evictableSize` by
   *  the element's EvictSize(). usedMemory is not touched by this.
   */
  static void Remove(Element& element);
  
  
  /** Called by the database whenever an MVCC snapshot is created to mark the blobs, clusters & segments as protected from
   *  cache eviction. These blobs, clusters & segments may have already been changed on disk and evicting them would destroy the MVCC snapshot.
   */
  static void MVCCSnapshotCreated(Database& db);

  /** Called by the database whenever the current MVCC snapshot is dropped.
   */
  static void MVCCSnapshotDropped(Database& db);


  /** Should be called whenever usedMemory > maximumMemory to try and release some memory
   */
  static void EvictMemory();


  /** A utility structure to use when a Touch() should be called after a function/scope is exited.
   */
  struct TouchOnExit {
    TouchOnExit(Element& element) : element(element) {}
    ~TouchOnExit() { Touch(element); }
    Element& element;
  };



  /** The maxmium allowed size of all blobs held in cache. Past this point blobs will be evicted from cache.
   */
  static size_t maximumMemory; 

  /** This data member keeps track of the memory that could be released when unloading all blobs,cluster,segments with a file backend
   *  from this cache. Active MVCC snapshots hinder unloading of blobs, so this is just an upper bound, not an exact value.
   */
  static size_t evictableMemory;


  /** The total memory currently used by blobc,clusters,segments (including in MVCC snapshots and in memory databases)
   */
  static size_t usedMemory;



private:
  /** Internal helper method, which will check whether the element is evictable and if so 
   *  update the evictable/used memory and remove the element from the LRU cache
   */
  static void TryEvict(Element& element);


  static struct LRU {
    Element* head = nullptr;
    Element* tail = nullptr;

    /** STL style LRU cache iterator (always in reverse direction tail->head)
     */
    class iterator {
    public:
      using value_type = Element*;
      iterator() : element(nullptr) {}
      iterator(Element* element) : element(element) {}
      iterator(const iterator& other) = default;
      iterator& operator=(const iterator& other) = default;

      bool operator==(const iterator& other) const { return element == other.element; }
      bool operator!=(const iterator& other) const { return element != other.element; }

      iterator& operator++() { element = element->lru.prev; };
      value_type operator*() const { return element; }

    private:
      Element* element;
    };

    /** iterator to the tail of the LRU cache (the first one to evict)
     */
    iterator begin() const;
    iterator end() const;
  } lru;

  /** This structure is used to keep track of all currently not evictable elements, due to an active MVCC snapshot
   */
  struct Unevictable {
    Database* database;
    std::unordered_set<Element*> elements;
  };
};


}
