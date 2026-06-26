## Databases

blobs.db supports opening multiple databases per [session](sessions.md). The same database can be opened across multiple sessions, but only once per single session.

A blobs.db database is stored as a single file and it is accessed either through the [blobs_server.exe](server.md) or the [blobs.db standalone library](standalone.md).



### Opening a database
Opening a database consists of simply calling `Database::Open()` with a connection string or a separate hostname, database path and optional port number. The connection string is just the single connection parameters combined into a string of the form `<host>[:<port>]/<dbName>` (eg. `localhost/test.db`).

`Database::Open()` returns a `Database` pointer, which must not (and in fact cannot) be manually `delete`'d be the caller. The only way to delete a `Database` object is to call `Close()` on it, which will properly notify the [server](server.md) about the closed database and then delete the `Database` object itself. Do not use it after calling `Close()` on it. All opened databases must be closed before calling `blobs::Shutdown()`.

The `openMode` parameter to open may be used to specify when to create a new database file and when to report an error. The default `openMode` creates a database if it doesn't exist and otherwise opens it.

`Database::OpenMVCC()` is a special case of `Open()`, which directly sets the database into [MVCC](mvcc.md) transaction mode. This is usually equivalent to calling `Open()` followed by `SetMVCC(true)` UNLESS the database is opened while already inside a running transaction, because `SetMVCC()` is only applied at the start of the next transaction. In such a case only  `OpenMVCC()` can open the database AND immediately set it into MVCC mode.

### The logical database structure
The blobs.db database is structured in a 3 level hierarchy of segments, clusters and blobs. The 3 level structuring is a bit arbitrary and a trade-off as the copy-on-write approach of the database benefits from a tree structure (balancing depth and elements per node) as less of the data needs to actually be copied, whereas usability suffers as each database access would need to name more navigation parameters or a just a variable length path. The naming of **segment** and **cluster** is just as arbitrary and DOES NOT hint at any underlying difference between these two. A **segment** simply holds zero or more **clusters** and a **cluster** holds zero or more **blobs**.

For now I decided against the variable length path as it introduces added parsing overhead, would make the network messages more complicated and would complicate the file structure of the database. A path would also invite the use of textual identifiers for each level (similar to a filesystem), which would add even more overhead network messages and the file storage of the database.

So when reading or writing a blob, you always have to specify the 3 parameters `segment`, `cluster` and `blob`. These parameters are just numerical identifiers a segment or cluster is not represented by an actual object that you could query on the client.

A newly created blobs.db database contains exactly one empty blob at segment 0, cluster 0, blob 0. New segments can be created by calling `CreateSegment()` on the database. \
A newly created segment always contains one empty blob at cluster 0, blob 0. New clusters can be created by calling `CreateCluster(segment)` on the database. \
A newly created cluster always contains one empty blob 0.

How you structure your blobs into this 3 level hierarchy is your own choice, but a balanced distribution of blobs will result in more efficient transaction commits as the server will need to copy less data. You can for simplicity simply ignore segments and clusters and store all your blobs into segment 0, cluster 0. Another point to consider is that two clients CANNOT create/delete blobs in the same cluster without blocking each other (due to the required [write locks](locks.md)). Two clients can do this however in separate clusters without blocking each other.

### Writing blobs
The `Database` class provides a basic `WriteBlob(segment, cluster, blob, blobData, blobSize)` implementation and a few convenience overloads that are implemented on top of it.

One such overload is `WriteString(segment, cluster, blob, string)`, which accepts binary string data as `std::string`, `std::wstring`, `std::string_view` or `std::wstring_view` non binary data as `char*`, `wchar_t*`. When the string is passed as zero terminated `char*`/`wchar_t*` then the null termination is NOT written into the blob. If you want to explicitly write the null termination into the blob, use a `std::string` or `std::string_view`.

Example:
```cpp
auto db = blobs::Database::Open("localhost/test.db");
db->WriteString(0, 0, 0, "Hello Blobs!");
blobs::Transaction::Commit();
db->Close();
```

Another fairly basic overload is `WriteVector(segment, cluster, blob, vector)` which will write the memory of any `std::vector<T>` into the blob. This method performs no serialization, it just copies the memory into the blob.

Upon calling any write operation, the client will automatically acquire a write lock for the blob if not already done (this operation may block the current thread). The blob data will be copied into the client's [transaction](transactions.md) cache inside `WriteBlob()`. Overwriting the memory after the call to `WriteBlob()` will not alter the blob upon [transaction](transactions.md) commit. Since the blob is only transferred to the server during `Transaction::Commit()`, calling `WriteBlob()` for the same blob with different data inside the same transaction will just update the blob in the client's transaction cache without any server communication as all locks are already acquired.


Blobs are currently limited to slightly less than 4GB (see `blobs::constants::MaxBlobSize`). This limit can be easily increased when compiling blobs.db from source by simply using 64 bit values for `message_size` and `blob_size`. blobs.db has not yet been optimized for huge blobs and the server has to wait for the blobs to be fully transmitted before being able to process them. Huge blobs can also result in swapping if the commit data does not fit into the server's memory.

### Reading blobs
Similar to writing, `Database` provides some helpful overloads for reading blobs like `ReadString(segment, cluster, blob, lock)` which reads the whole blob into a `std::string`. `ReadVector<T>(segment, cluster, blob, lock)` supports interpreting the content of the blob as a vector type `T`. ReadVector cannot check whether the blob actually contains anything of that type and if the blob does not contain a clean multiple of `T` objects, then the last (incomplete) object will be ignored.

The `lock`-Parameter in all read functions can be used to specify the lock to acquire for the read operation (default is a read lock). For a more detailed explanation see: [locks](locks.md) and [dirty reads](dirty_reads.md).

The basic read function is `Database::ReadBlob(segment, cluster, blob, lock)`, which returns a pair of `void*` and `blob_size`. One important point to note is that the blob data should be copied right after calling `ReadBlob()` and the caller should not hold on to the returned `void*` as it points into the client's database or transaction cache and may be overwritten or deleted when calling `Database::WriteBlob` or committing/aborting the transaction. In case of a [dirty read](dirty_reads.md), the returned `void*` points into a special temporary memory buffer and will be overwritten by the next dirty read operation. 

The client requests a blob at most once per transaction (exception: [dirty reads](dirty_reads.md)). Calling `ReadBlob()` on the same blob again will NOT request the blob from the database server again, but will instead return the blob content from the client cache. When using sticky [locks](locks.md) the client can even avoid re-requesting a previously read blob in the following transaction as long as the sticky lock is not revoked.

Write operations are stored in the client's local transaction cache. After writing a blob (`WriteBlob()`) the client will first consult the local transaction cache before reading the local database cache. So after a `WriteBlob()` you are guaranteed to see the just written data in a followup `ReadBlob()` even before committing the transaction. The client uses these caches to present a consistent database view to the application.

Example:
```cpp
// Use OpenMode::CreateAlways here to always start with an empty database
auto db = blobs::Database::Open("localhost/test.db", blobs::Database::OpenMode::CreateAlways);
db->ReadString(0, 0, 0); // -> ""
db->WriteString(0, 0, 0, "Hello Blobs!");
db->ReadString(0, 0, 0); // -> "Hello Blobs!"
blobs::Transaction::Commit();
db->Close();
```

The consistency of the cached client state includes all database operations, not just simple reads and writes. After creating/deleting blobs,clusters,segments the ranges returned by `GetAllBlobs/GetAllClusters/GetAllSegments` will reflect these changes immediately.

### Opening an in-memory databases
blobs.db supports opening in-memory only databases. These databases are opened normally via `Database::Open()` but instead of a file name/path you use the prefix `mem:` followed by an arbitrary (case-sensitive) identifier. This will create the transient data structures on the server as are created when opening a regular file database, but no file backend is created. Such in-memory databases behave just like regular file databases in all respects with the only difference being that an in-memory database is closed once the last client using it closes it or if the server shuts down.

Example:
```cpp
// Opening an in memory database is simply specifying an arbitrary name with the prefix "mem:"
auto memoryDb = blobs::Database::Open("localhost", "mem:test.db");
memoryDb->WriteString(0, 0, 0, "hello");
Transaction::Commit();

memoryDb->ReadString(0, 0, 0); // -> "hello"
Transaction::Abort();
memoryDb->Close();

// Try to reopen it
memoryDb = blobs::Database::Open("localhost", "mem:test.db");

// Reading this blob will return:
//  - "" if this was the only client/session on this in-memory database
//  - "hello" if at least one other client/session kept the database open while this client closed it
memoryDb->ReadString(0, 0, 0);
Transaction::Abort();
memoryDb->Close();
```


### Database file structure
The file structure of the database is documented in the [ImHex editor](https://imhex.werwolv.net/) compatible pattern [blobsdb.hexpat](../blobsdb.hexpat) that can be used to inspect file databases. A file database consists of a header, which contains two database roots and a byte indicating, which one of the roots is the active one.
The root itself contains a file offset to its free list and the snapshot. The snapshot (see also [MVCC](mvcc.md)) is the tree structure root that holds all database segments. The free list is used to to manage which blocks of the file are currently free to be reused.

When committing changes to the database, the server will internally copy the current snapshot (in memory) and replace only the modified segments, clusters, blobs with new objects, while keeping references to all unchanged elements of the database. To commit this new snapshot only the new blocks of data need to be written into the file followed by the snapshot structure itself and then the active snapshot can be switched by writing the new snapshot's offset into the currently inactive root and then updating the one byte to mark the inactive root as now active. \
One important thing to note is that an updated version of a blob is allocated in a new memory block inside the file, the old blob is never directly overwritten. It turns into unused file memory space once the active database root is switched because all overwritten memory blocks are entered into the new free list upon commit. 

So right after a commit the database file contains two complete snapshots of the database.
