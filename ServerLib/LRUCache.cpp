#include "pch.hpp"
#include <server/LRUCache.hpp>
#include <server/Database.hpp>
#include <server/Segment.hpp>
#include <server/Cluster.hpp>
#include <server/Blob.hpp>

namespace blobs::server {

FIXME("Maybe avoid all this LRU overhead if no memory limit is set");

size_t LRUCache::maximumMemory = std::numeric_limits<size_t>::max();
size_t LRUCache::evictableMemory = 0;
size_t LRUCache::usedMemory = 0;
LRUCache::LRU LRUCache::lru;

bool LRUCache::IsEnabled() {
  return maximumMemory != std::numeric_limits<size_t>::max();
}


void LRUCache::Touch(Element& element) {
  if (!IsEnabled()) {
    return;
  }

  if (!element.lru.prev && !element.lru.next) {
    // Either not part of the LRU cache, or the only one in the cache.
    // Nothing to do in both cases
    return;
  }

  if (lru.head == &element) {
    // Already the first entry of the LRU cache
    return;
  }

  // Now we know that the element is not the first entry and we have at least one more entry.
  // So cut out the element either from the middle or from the end
  if (element.lru.next) {
    // Not the last entry of the cache
    element.lru.prev->lru.next = element.lru.next;
    element.lru.next->lru.prev = element.lru.prev;
  } else {
    // The last entry of the cache
    lru.tail = element.lru.prev;
    lru.tail->lru.next = nullptr;
  }

  // Finally insert the element at the head
  lru.head->lru.prev = &element;
  element.lru.next = lru.head;
  element.lru.prev = nullptr;
  lru.head = &element;
}

void LRUCache::Enter(Element& element) {
  if (!IsEnabled()) {
    return;
  }

  assert(!element.lru.inCache);

  if (auto head = std::exchange(lru.head, &element)) {
    // There was already a head
    head->lru.prev = &element;
    element.lru.next = head;
  } else {
    // The first element in this cache -> also the tail
    lru.tail = &element;
  }

  evictableMemory += element.EvictSize();
  element.lru.inCache = true;
}

void LRUCache::Remove(Element& element) {
  if (!IsEnabled()) {
    return;
  }

  assert(element.lru.inCache);

  if (lru.head == &element) {
    // Removing the first element -> next one becomes the head
    lru.head = element.lru.next; // my be nullptr if element is the only one
    element.lru.next = nullptr;
    if (lru.head) {
      lru.head->lru.prev = nullptr;
    }
  }

  if (lru.tail == &element) {
    // Removing the last element -> previous one become the tail
    lru.tail = element.lru.prev; // may be nullptr if element is the only one
    element.lru.prev = nullptr;
    if (lru.tail) {
      lru.tail->lru.next = nullptr;
    }
  }
    
  if (element.lru.prev) {
    // Now the only elements left are the ones in the middle (next is also != nullptr)
    assert(element.lru.next);
    
    element.lru.prev->lru.next = element.lru.next;
    element.lru.next->lru.prev = element.lru.prev;
    element.lru.prev = nullptr;
    element.lru.next = nullptr;
  }

  evictableMemory -= element.EvictSize();
  element.lru.inCache = false;
}


void LRUCache::EvictMemory() {
  if (!IsEnabled()) {
    return;
  }

  // Start eviction at the tail and stop once we released enough memory
  // or traversed the whole list once
  while (lru.tail && usedMemory > maximumMemory) {
    // Evict the least recently used element (this will update lru.tail to point to the previous element of the LRU chain)
    Evict(*lru.tail);
  }
}

void LRUCache::Evict(Element& element) {
  // All elements that are inside the LRU chain must be marked as such, have a file location to reload them from and be actually loaded
  assert(element.fileLocation && element.lru.inCache && element.status == MemoryBlock::Status::LOADED);
  
  auto evictSize = element.EvictSize();

  // Evict element and remove it from the LRU cache
  element.Evict();
  Remove(element);

  // Update memory
  evictableMemory -= evictSize;
  usedMemory -= evictSize;
}

LRUCache::LRU::iterator::iterator() : element(nullptr) {}
LRUCache::LRU::iterator::iterator(Element* element) : element(element) {}

bool LRUCache::LRU::iterator::operator==(const iterator& other) const { 
  return element == other.element; 
}

bool LRUCache::LRU::iterator::operator!=(const iterator& other) const { 
  return element != other.element; 
}

auto LRUCache::LRU::iterator::operator++() -> iterator& { 
  element = element->lru.prev; 
  return *this;
};

auto LRUCache::LRU::iterator::operator*() const -> value_type { 
  return element; 
}


LRUCache::LRU::iterator LRUCache::LRU::begin() const {
  return iterator(lru.tail);
}

LRUCache::LRU::iterator LRUCache::LRU::end() const {
  return iterator();
}



}