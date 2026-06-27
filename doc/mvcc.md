# MVCC transactions

One way to reduce [locking](locks.md) and thus increase throughput of an application is by letting read only clients read the [database](databases.md) in MVCC [transaction](transactions.md) mode. MVCC transaction mode suitable for clients that only perform reads on a database and don't need to see the newest version of the database but stil require a consistent view. Inside an MVCC transaction the client will read all blobs from a consistent readonly database snapshot that is created when the first client requests an MVCC transaction for that database and discarded once the last client closes the MVCC transaction for this database. Since this snapshot is readonly, no locks need to be set when reading data from this snapshot, which means MVCC clients cannot be blocked by any other clients and do not block other clients. 

## Usage
Setting a database into MVCC mode is as simple as calling `Database::SetMVCC(true)` on it. **IMPORTANT:** this will only take effect on the **next** transaction start. A database cannot change transaction modes while already inside a transaction. A special case of this is opening a database inside a transaction. Databases are opened by default in regular update transactions, so `Datbase::Open(...)->SetMVCC(true);` will not be applied on the current transaction, but on the next one. To open a database directly in MVCC mode, use the `Database::OpenMVCC()` function.

Write operations and all attempts to acquire a write lock during an MVCC transaction will throw `blobs::exception::CannotWriteLockInMVCC`.

MVCC transactions support [dirty reads](dirty_reads.md), which are as usual performed by passing `blobs::Lock::None` as `lock` parameter to any read operation. A dirty read in MVCC will ignore the current MVCC snapshot and always read the most up to date version of the blob from the active database snapshot.


Example:
```cpp
auto db = Database::Open("localhost", "mem:testMVCC"); // open in memory database in update mode
db->WriteString(0, 0, 0, "test"); // update blob 0,0,0
Transaction::Commit();

db->SetMVCC(true); // mark for MVCC mode for next transaction

// Perform a read in MVCC. This will result in:
// - "test" if this is the only client on the database
// - "" if another client opened the same database in MVCC BEFORE our Transaction::Commit()
//         and kept it open since then. The MVCC snapshot then doesn't yet contain our update to blob 0,0,0
db->ReadString(0, 0, 0);

// Now perform a dirty read from inside MVCC, which will return "test" as it reads the most up to date version of the blob
db->ReadString(0, 0, 0, blobs::Lock::None);

Transaction::Abort();
db->Close();
```


## Performance considerations
The MVCC snapshots are very cheap to create on the server (basically a pointer copy - see also [databases](databases.md)). The MVCC snapshot introduces a certain memory overhead in the server as the database's active snapshot diverges from the MVCC snapshot over time, some blobs will be held in two different versions. This overhead can be reduced by keeping the MVCC transactions short, which will also result in more frequent updates of the MVCC snapshot.

An active MVCC snapshot adds a small overhead to commit times on the server when a client is deleting blobs/clusters/segments. Without an active MVCC snapshot the server can simply discard these structures even if they haven't been loaded into the server's cache yet (because no client was accessing them). Now if there is an active MVCC snapshot then the server has to load all the deleted structures into memory and attach them to the MVCC snapshot before discarding them from the active database snapshot in case an MVCC client attempts to read the deleted structures after that commit.