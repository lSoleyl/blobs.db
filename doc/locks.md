# Locks
In regular update [transactions](transactions.md) the client has to acquire a lock before reading or writing a blob to ensure correct transaction ordering for ensuring each transaction always sees a consistent [database](databases.md) state. This means that each transaction sees either all writes from another transaction (ordered after that transaction) or sees none of the writes from another transaction (ordered before that transaction). Ordering is achieved by blocking lock acquisition of a client if a conflicting transaction that is ordered before the client has not yet completed.

The usual locking semantics apply. Multiple clients can read lock the same resource while there is no write lock. Only one client can write lock the same resource while there is no read lock. Read locks are upgradable, which means that a client that holds a read lock can upgrade it to a write lock as long as no other client holds a read lock to that same resource. A read/write operation that cannot acquire a lock due to a locking conflict with an other client will block until the conflicting lock is released (when the conflicting client ends its transaction).

Besides locking blobs.db offers a readonly [MVCC transaction](mvcc.md) mode, which operates on a consistent snapshot of the whole database and does not set any locks.

All read operations in `Database` take an optional `lock` parameter, which can be set to `Lock::Write` to acquire a write lock for that read operation. Directly acquiring a write lock can be beneficial when attempting to read a blob and then update (write) it in the same transaction. Two clients performing that same update operation could deadlock if the initial read is performed with a read lock and both clients have acquired the read lock before attempting to upgrade it to a write lock. Since acquring a write lock directly is an atomic operation (the server doesn't internally acquire a read lock first), this operation will complete only if no other client holds a lock on that blob and will from then on prevent any other client from getting a lock on that blob.

## Sticky Locks
In regular database applications all locks are usually released when the transaction ends. blobs.db by default uses so called sticky locks. Sticky locks allow a client to keep all the locks, which were held during the previous transaction in its next transaction as long as no other client requested a lock on these blobs in the meantime. On each transaction start the client notifies the server whether sticky locks should be used with a specified database and the server will respond with a list of locks that the client can keep from its previous transaction.\
Together with the already existing blob cache on the client, this means that in single (or few) client applications the client can very efficiently update the same blobs in the next transaction without ANY server communication besides the transaction start and commit messages.

As stated this mechanism is very useful in single client applications but has the drawback of clients potentially holding locks during a transaction for blobs that they never write to or read from. So in multi client applications with a lot of contention this feature could cause more harm to overall application performance and should probably be disabled.

Sticky locks can be enabled/disabled on a per database level (see `Database::UseStickyLocks()`). This option takes effect at the start of the next transaction.
The default setting for newly opened databases (for the current [session](sessions.md)) can be configered via `Transaction::UseStickyLocks()`.

## Deadlocks
A deadlock occurs in a situation in which two (or more) transactions have to be simultaneously ordered before and after one another according to locking semantics. It is mostly caused by two transactions performing writes to the same blobs in a different order (by the time they attempt to write the same blob, both transactions have already started writing data) or by two transactions trying to upgrade a read lock on the same blob.

If a deadlock occurs the server the client transaction with the lowest priority (see `Transaction::SetPriority()`) is aborted and a `blobs::exception::Deadlock` is thrown in the client with the aborted transaction. If all involved clients have the same transaction priority, then the client, which caused the deadlock to be detected (the last request) has its transaction aborted.

There are a few strategies to prevent deadlocks:

### Lock ordering and write locking
If all locks are acquired in the same order (eg. in ascending segment,cluster,blob order) and all blobs that are written to are immediately acquired as write locks, then deadlocks cannot occur. This solution may however not be applicable to many real world scenarios as access patterns may not be so clearly plannable. To resolve deadlocks an appplication does not need to enforce absolute lock ordering and can usually get away with using a single blob as synchronization/serialization point. If all clients that can deadlock with one another (because they access the same parts of the database) have to FIRST acquire a write lock on a predefined blob (say blob 0) and only then perform their usual transaction then again no deadlocks can occur, because this single synchronization point will ensure that all these clients will run their transaction code one after another. This obviously restricts transaction parallelism and application throughput, so it is a trade-off (as everything).

### MVCC transactions
If the application is clearly separated into reading clients and updating clients then just changing setting the reading clients to use [MVCC transactions](mvcc.md) can already avoid any deadlocks that could occur between a reading client and a writing client, because the reading client will not acquire any locks in MVCC mode. The only downside to MVCC is that reading is performed on a database snapshot, which may contian the most up-to-date data. (The snapshot is updated whenever there is no active MVCC transaction running on the database).

### Dirty reads
See [dirty reads](dirty_reads.md)