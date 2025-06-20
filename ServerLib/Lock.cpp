#include "pch.hpp"
#include "include/server/Lock.hpp"

namespace blobs {
namespace server {

Lock::Lock(const BlobLocation& location) : location(location) {}


Lock::Lock(const BlobLocation& location, client_id client, bool writeLock) : location(location) {
  if (writeLock) {
    write = client;
  } else {
    read.push_back(client);
  }
}

bool Lock::operator<(const Lock& other) const {
  return location < other.location;
}

bool Lock::operator<(const BlobLocation& location) const {
  return this->location < location;
}


bool Lock::CanAcquire(client_id client, bool writeLock) const {
  if (!writeLock) {
    // A read lock can be acquired if no write lock is held or the write lock is being held by this client
    return !write || *write == client;

  } else {
    // A write lock can only be acquired if this client is the only client holding the read lock (or no client)
    if (write) {
      // If this is already write locked, then acquiring can only work if this client is already holding the write lock
      return *write == client;
    }

    // Either there are no read locks or this client holds the only read lock
    return read.empty() || (read.size() == 1 && read[0] == client);
  }
}

void Lock::Acquire(client_id client, bool writeLock) {
  if (writeLock) {
    // Set write lock and potentially upgrade read lock
    write = client;
    read.clear(); // no read locks can exists while a write lock exists
  } else if (write == client) {
    // Nothing to do... we don't downgrade write locks into read locks
  } else {
    // Acquire a read lock
    auto pos = std::find(read.begin(), read.end(), client);
    if (pos == read.end()) {
      // The client didn't hold this read lock before
      read.push_back(client);
    }
  }
}

bool Lock::Release(client_id client) {
  if (write) {
    // Fast path as only one write lock can exist at any time
    if (*write == client) {
      write.reset();
      return true;
    }
    assert(false); // This should never happen as the client should always know, which locks he holds
    return false; 
  } else {
    // Remove the client lock from the read lock holders
    auto pos = std::remove(read.begin(), read.end(), client);
    read.erase(pos, read.end());
    return read.empty();
  }
}


bool Lock::OwnsWriteLock(client_id client) const {
  return write == client;
}

bool operator<(const BlobLocation& location, const Lock& lock) {
  return location < lock.location;
}



SCENARIO("Locking Invariants") {
  client_id clientA = 1;
  client_id clientB = 2;
  client_id clientC = 3;
  client_id clientD = 4;

  client_id clients[] = { clientA, clientB, clientC, clientD };

  GIVEN("An empty lock") {
    Lock lock({});

    THEN("Each client CanAcquire() a read lock") {
      for (auto client : clients) {
        CAPTURE(client);
        REQUIRE(lock.CanAcquire(client, false));
      }
    }

    THEN("Each client CanAcquire() a write lock") {
      for (auto client : clients) {
        CAPTURE(client);
        REQUIRE(lock.CanAcquire(client, true));
      }
    }
  }


  GIVEN("A read lock held by clientA") {
    Lock lock({}, clientA, false);

    THEN("All clients can still acquire a read lock") {
      for (auto client : clients) {
        CAPTURE(client);
        REQUIRE(lock.CanAcquire(client, false));
      }
    }

    THEN("clientA can acquire a write lock") {
      REQUIRE(lock.CanAcquire(clientA, true));
    }

    THEN("All other clients cannot acquire a write lock") {
      for (auto client : clients) {
        if (client != clientA) {
          CAPTURE(client);
          REQUIRE(!lock.CanAcquire(client, true));
        }
      }
    }

    
    WHEN("clientA releases the lock") {
      lock.Release(clientA);

      THEN("All clients can acquire a read and write lock") {
        for (auto client : clients) {
          CAPTURE(client);
          REQUIRE(lock.CanAcquire(client, true));
          REQUIRE(lock.CanAcquire(client, false));
        }
      }
    }
  }


  GIVEN("A read lock held by clientA and clientB") {
    Lock lock({}, clientA, false);
    lock.Acquire(clientB, false);


    THEN("All clients can still acquire a read lock") {
      for (auto client : clients) {
        CAPTURE(client);
        REQUIRE(lock.CanAcquire(client, false));
      }
    }

    THEN("No client can acquire a write lock") {
      for (auto client : clients) {
        CAPTURE(client);
        REQUIRE(!lock.CanAcquire(client, true));
      }
    }

    WHEN("clientA releases the lock") {
      lock.Release(clientA);

      THEN("All clients can still acquire a read lock") {
        for (auto client : clients) {
          CAPTURE(client);
          REQUIRE(lock.CanAcquire(client, false));
        }
      }

      THEN("Only clientB can acquire a write lock") {
        REQUIRE(lock.CanAcquire(clientB, true));
      }

      THEN("No other client can acquire a write lock") {
        for (auto client : clients) {
          if (client != clientB) {
            CAPTURE(client);
            REQUIRE(!lock.CanAcquire(client, true));
          }
        }
      }
    }
  }


  GIVEN("A write lock held by clientA") {
    Lock lock({}, clientA, true);

    THEN("Only clientA can acquire a read or write lock") {
      REQUIRE(lock.CanAcquire(clientA, false));
      REQUIRE(lock.CanAcquire(clientA, true));
    }

    THEN("No other clients can acquire a read or write lock") {
      for (auto client : clients) {
        if (client != clientA) {
          CAPTURE(client);
          REQUIRE(!lock.CanAcquire(client, false));
          REQUIRE(!lock.CanAcquire(client, true));
        }
      }
    }

    WHEN("Re-Acquiring the write lock for clientA") {
      lock.Acquire(clientA, true);

      THEN("Only clientA can acquire a read or write lock") {
        REQUIRE(lock.CanAcquire(clientA, false));
        REQUIRE(lock.CanAcquire(clientA, true));
      }

      THEN("No other clients can acquire a read or write lock") {
        for (auto client : clients) {
          if (client != clientA) {
            CAPTURE(client);
            REQUIRE(!lock.CanAcquire(client, false));
            REQUIRE(!lock.CanAcquire(client, true));
          }
        }
      }
    }

    WHEN("Acquiring the read lock for clientA") {
      lock.Acquire(clientA, false);

      THEN("Only clientA can acquire a read or write lock") {
        REQUIRE(lock.CanAcquire(clientA, false));
        REQUIRE(lock.CanAcquire(clientA, true));
      }

      THEN("No other clients can acquire a read or write lock") {
        for (auto client : clients) {
          if (client != clientA) {
            CAPTURE(client);
            REQUIRE(!lock.CanAcquire(client, false));
            REQUIRE(!lock.CanAcquire(client, true));
          }
        }
      }
    }
  }
}



}}