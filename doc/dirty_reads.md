# Dirty reads
A dirty read is performed by passing `Lock::None` as lock parameter to any read operation. Such a read will never go the client's local blob cache and will always fetch the blob from the server. A dirty read will never acquire any locks and always return the current state of the blob (which may be inconsistent with the same blob in the currently running transaction). A dirty read is also not blocked by another client holding a write lock on the read blob.

In some scenarios where reading a blob in a state that is not consistent with the rest of the transaction is a smaller issue than the performance overhead of (b)[locking](locks.md) dirty reads can be a useful tool to improve application throughput.

Even in scenarios where consistency is important, a dirty read can be performed ahead of the actual read to improve performance.
One such scenario would be a client needing to only update a blob if it is in a certain state (eg. a one time initialization). In that case first read [locking](locks.md) it for the check will risk a deadlock with another client doing the same if the blob actually needs an update. Directly write locking it could however be a performance pessimization if this update needs to be performed rarely, especially if the write lock is acquired at the beginning of a long running [transaction](transactions.md) and locks cannot be manually released while inside a running transaction.

If the application is willing to sacrifice a bit of consistency then this issue can be resolved by performing a dirty read to determine whether the blob needs an update.

If the client determines that an update of the blob is necessary according to the dirty version, the client can then read the blob again while setting a write lock to check whether the update is still necessary (it could have been updated in the mean time) and if so update that blob. By directly setting the write lock the client also avoids potential deadlocks.


## Sample code

```cpp
// Inside a transaction

auto blobContent = database->ReadString(0, 0, 0, blobs::Lock::None);
if (blobContent.empty()) {
    // blob needs initialization according to dirty read, only now acquire the write lock
    blobContent = database->ReadString(0, 0, 0, blobs::Lock::Write);
    if (blobContent.empty()) {
        // blob needs initialization according to our transaction state.
        database->WriteString(0, 0, 0, "dirty init");
    }
}


// ...
Transaction::Commit();

```

## MVCC
Dirty reads are also supported inside [MVCC transactions](mvcc.md) and will read the blob from the live database instead of the MVCC snapshot. This may be helpful if the most up to date value of the blob is needed for some reason inside an MVCC transaction.
