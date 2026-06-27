# blobs.db

A low overhead ACID database for storing blobs, which supports file databases and pure in-memory databases and can be run in standlone mode or in client/server configurations. 


## Quick start
Prerequisites: Visual Studio with a v140 compatible toolset version installed and a C++ 17 compatible compiler (VS2015-VS2026).

1. Download the latest release from the releases page
2. Add the `include` directory to your C++ include directories.
3. Select the correct `.lib` file as linker input
   * use `blobs.db.lib` when implementing a regular client/server application
   * use `blobs.sa.db.lib` when implementing a standalone client application
4. Write a client application:

```cpp
#include <blobs/Blobs.hpp>

#include <iostream>

using namespace blobs;

int main(int argc, char** argv) {
  // blobs::Initialize() must be called before performing any database operation
  Initialize();

  // Open or create the database as ".\test.db" relative to the server's database root dir
  // The database root dir is by default ".\databases" (relative to the server's working directory)
  auto db = Database::Open("localhost", "test.db");

  // Read the default, blob at segment 0, cluster 0, blob 0, which always exists in a new database.
  auto content = db->ReadString(0, 0, 0); // returns a std::string
  std::cout << "Old blob content: " << content << std::endl;

  // Initialize or update the content
  if (content.empty()) {
    content = "Hello blobs!";
  } else {
    content += " Hello to you too!";
  }

  std::cout << "New blob content: " << content << std::endl;

  // Write back the blob in to the database
  db->WriteString(0, 0, 0, content);

  // Commit transaction to the server
  Transaction::Commit();

  // Close the database when it is not needed anymore. 
  // This will also delete the Database object, so Close() must always be the 
  // last method call on this pointer
  db->Close();

  // blobs::Shutdown() must be called before terminating the process to shutdown the network thread and cleanup resources
  Shutdown();
  return 0;
}
``` 

5. If compiled as client/server application start the `blobs_server.exe`
6. Build the client application (make sure to place the `blobs.db.dll` or `blobs.sa.db.dll` next to the built .exe file or have it in the `PATH`)
7. Run the client application multiple times to see it update the database

## Design philosophy... or why one more database?
I needed a database, which can efficiently store and retrieve lots of blobs and:
 * is ACID with [SERIALIZABLE isolation level](https://en.wikipedia.org/wiki/Isolation_(database_systems)#Serializable)
 * under NO circumstances leaves a corrupted database file (using copy-on-write commits)
 * supports [MVCC](doc/mvcc.md) transactions
 * supports multiple [sessions](doc/sessions.md) (connections) per client
 * can open multiple [databases](doc/databases.md) per session
 * stores databases in a single file (easy to manage/backup)
 * supports in memory databases without a file backend (for temporary data)
 * supports a [standalone](doc/standalone.md) version (similar to SQLite)
 * is compact and easy to integrate into projects with a C++ 17 compiler:
   * one .lib/.dll for the client
   * one self contained [blobs_server.exe](doc/server.md)

One thing that results form these requirements is that the database requires no transaction log and the database file is always in a consistent state and a simple shadow volume copy can be used to perform a consistent backup of the database.

One core idea was to create a cooperative protocol between client and server where the client will hold all the transaction state locally and cache reads and remember writes and only during commit everything will be sent to the server. This means that the server (the single bottleneck) is not burdened with keeping track of every client's transaction state. This also makes transaction aborts very cheap as the client just has to discard all locally saved write operations. This additionally lends itself into the idea of [sticky locks](doc/locks.md), which are locks that can be preserved across transactions and reduce client/server communication basically to zero (for single client use-cases) because the client already knows the current state of the blobs it has already read in the previous transaction.

## Current limitations
blobs.db is still in a very early stage of development and before considering to integrate it into a live system be aware of the following limitations at the moment (most of them should be fixed in the future):

 * Only available for x64
 * Not available on Linux systems
 * No graceful recovery on connection errors
 * No authentication of client/server
 * No transport encryption between client and server
 * Blobs are limited to ~4GB in size (see `blobs/Config.hpp`)
 * Server logic is single threaded and blocks on database file IO
 * Server cache and client cache are only cleared when closing a database. No LRU mechanism to evict rarely used blobs from memory is implemented.
 * No support for reading multiple blobs in a single request
 * No support for lock timeouts

## Planned features
  * Client-Cache memory management (remove rarely accessed blobs)
  * Server database memory management (unload rarely accessed blobs,clusters,segemnts if memory is restricted)
  * Multi blob read
  * Add `CreateClusterAt()`/`CreateSegmentAt()` with a given cluster/segment id
  * Tool to perform a live copy of the database based on MVCC
  * Add cmake support
  * Support linux
  * Add API for clients to specify a client identifier (to make server logs/errors more understandable)
  * Add support for lock timeouts
  * Transport encryption (TLS)
  
## Open ideas
  * One MVCC snapshot per client instead of a shared snapshot?
  * Database structure with variable hierarchy depth and textual identifiers
