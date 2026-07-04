# blobs.db standalone

When the client application is linked against `blobs.sa.db.lib` it will use the `blobs.sa.db.dll` at runtime, which bundles the server and client into a single library.
A client application linking against this standalone version will start an internal server thread when calling `blobs::Initialize()`. In standalone mode no network communication is happening and no server port is opened. This also means that regular clients cannot connect to the internally running server of a standalone client instance. The internal server thread and each client [session](sessions.md) thread communicate over a simple synchronized queue. 

The idea is that the client application is able to open [databases](databses.md) without having to start a dedicated [blobs_server](server.md) process similar to how SQLite clients operate. 

The integrated server supports the same features as the full `blobs_server.exe` with the only exception being that only one process can connect to that server. This client process can however use multiple [sessions](sessions.md) to connect to the internal server thus having multiple clients accessing the same internal server (which can of course also lead to locking conflicts between them).

Just like the `blobs_server.exe` the integrated server will open all database files in exclusive write mode to prevent multiple servers from accidentially opening the same database. This also means that multiple clients with integrated servers can run on the same machine without affecting each other as long as they operate on different databses.

## Sample client code
```cpp
#include <blobs/Blobs.hpp>

int main(int argc, char** argv) {
    // Initialize blobs.db (client&server) with default configuration:
    // - no logging
    // - database root = .\databases
    blobs::Initialize();

    // Do things...
    auto db = blobs::Database::Open("localhost", "test.db");
    db->WriteString(0, 0, 0, "Hello blobs!");
    blobs::Transaction::Commit();
    db->Close();

    // Cleanup and shutdown (the threads)
    blobs::Shutdown();
    return result;
}
```

## Configuration
The server options can be configured by passing a `blobs::Configuration` object into `blobs::Initialize()`.

```cpp
#include <blobs/Blobs.hpp>

int main(int argc, char** argv) {
    auto& config = blobs::Configuration::Get()
        .LogLevel(blobs::LogLevel::DEBUG_LEVEL) // <- let the server log everything
        .LogFile(".\\client.log")     // <- log into file instead of std::cout
        .DbRootDir("C:\\databases")   // <- change database root directory
        //.NoDbRootDir()              // <- to disable database root directory entirely
        //.DatabaseCloseDelay(3s)     // <- optionally delay closing databases by 3 seconds
    ;

// Initialize blobs.db (client&server) with the specified configuration
    blobs::Initialize(config);

    //...
}
```