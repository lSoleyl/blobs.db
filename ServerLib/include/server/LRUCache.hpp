#pragma once

namespace blobs::server {

struct MemoryBlock;
class Database;

TODO("Should I turn this into an object? It would add overhead to every call... is it worth it?");

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

  /** Enters the element into the LRU cache. This will increment only the `evictableMemory` by the element's EvictSize().
   *  usedMemory is not touched by this.
   */
  static void Enter(Element& element);

  /** Removes the element from the LRU cache (usually upon deletion or eviction). This will decrement only the `evictableMemory` by
   *  the element's EvictSize(). usedMemory is not touched by this.
   */
  static void Remove(Element& element);


  /** Should be called whenever usedMemory > maximumMemory to try and release some memory
   */
  static void EvictMemory();


  /** A utility structure to use when a Touch() should be called after a function/scope is exited.
   */
  struct TouchOnExit {
    TouchOnExit(Element& element) : element(element) {}
    ~TouchOnExit() { Touch(element); }
  private:
    Element& element;
  };


  /** A utility structure to use when the used memory of a structure managed by the LRU cache changes to update the total usedMemory
   *  and evictable memory accordingly. Construct this structure before the update and drop it after the update has been performed.
   *  Evictable memory is only updated if the element is part of the LRU cache. Otherwise only usedMemory is updated.
   * 
   *  The structure is defined as template to allow static dispatch of EvictSize() method.
   */
  template<typename ElementType>
  struct UpdateMemoryOnExit {
    UpdateMemoryOnExit(ElementType& element) : element(element), delta(0 - element.EvictSize()) {} // unsigned underflow/overflow is well defined
    ~UpdateMemoryOnExit() {
      delta += element.EvictSize();
      
      // Total used memory is always updated
      usedMemory += delta;

      if (element.lru.inCache) {
        // Only elements in cache contribute to the evictable memory
        evictableMemory += delta;
      }
    }

  private:
    ElementType& element;
    size_t delta;
  };



  /** The maxmium allowed size of all blobs held in cache. Past this point blobs, clusters, segments will be evicted from cache.
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
  /** Internal helper method, update the evictable/used memory and remove the element from the LRU cache
   */
  static void Evict(Element& element);


  static struct LRU {
    Element* head = nullptr;
    Element* tail = nullptr;

    /** STL style LRU cache iterator (always in reverse direction tail->head)
     */
    class iterator {
    public:
      using value_type = Element*;
      iterator();
      iterator(Element* element);
      iterator(const iterator& other) = default;
      iterator& operator=(const iterator& other) = default;

      bool operator==(const iterator& other) const;
      bool operator!=(const iterator& other) const;

      iterator& operator++();
      value_type operator*() const;

    private:
      Element* element;
    };

    /** iterator to the tail of the LRU cache (the first one to evict)
     */
    iterator begin() const;
    iterator end() const;
  } lru;
};


}
