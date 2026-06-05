# Sessions

All client communication and with the database [servers](server.md) and all [database](databases.md) state and transaction state for these connections are associated with a session.

In the trivial case each client has exactly one session (the global session). But blobs.db supports multiple sessions per client, which can act like multiple idependent sub-clients inside a client process and establish their own network connections to the database server. This allows for opening the same database in two independent Transactions (which can of course have [locking](locks.md) conflicts with one another).

The global session always exists. Each next session is created via `Session::Create()`.

## Sample Code
```cpp
{
    // Create a new session
    auto session = blobs::Session::Create();

    // Open database in that session (session is always the first parameter)
    auto db = blobs::Database::Open(session, "localhost", "test.db");
    db->WriteString(0, 0, 0, "Hello sessions!");

    // Commit transaction (in this session!)
    blobs::Transaction::Commit(session);
    db->Close();
} // <- as the session handle falls out of scope, the session will be terminated
```


## The session handle
`Session::Create()` and `Session::GetGlobalSession()` return special Handle objects that can be cheaply copied and moved around and acts like a `std::shared_ptr` to the atomically reference counted session object. Once the last session handle is destroyed, the session itself is destroyed, which means that the network thread associated with this session is terminated.

Be aware that the `blobs::Database` object also holds a handle to the session it was opened in. So any still open database will also keep its session alive. To full terminate a session, all databases opened in that session must be closed and all session handles destroyed.

The session object itself, which is wrapped inside the session handle, provides no methods to be used by the client. It is only used internally by the library to keep track of the session's state and synchronize access.

## The global session
For ease of use, the call to `blobs::Initialize()` implicitly initializes a global session, which the client can access via `Session::GetGlobalSession()`. This global session is also the default session, which is used for all functions that take an optional session parameter (eg. `Database::Open()`, `Transaction::Commit()`).

The global session handle is released in `blobs::Shutdown()`, which also shuts down the session itself. If the program still holds a handle to the global session during `blobs::Shutdown()`, then a `blobs::Exception` is thrown. `blobs::Shutdown()` does

## Sessions and Multithreading
A single session may be used by any number of threads. All access to [databases](databses.md) and [transactions](transactions.md) is synchronized through the session object's mutex, which only allows one thread at a time to call a method on `blobs::Database` or a function on `blobs::Transaction`.

The session handle itself may be used by multiple threads at once. Beware that the operations, which modify a session handle (`Reset()`,`operator=`,`~Handle()`) are not synchronized and MUST NOT be called from multiple threads on the SAME session handle without manual synchronization.

A session handle may be safely copied into as many threads as needed and each copy can safely be modified within that thread.