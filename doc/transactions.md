# Transactions

blobs.db implements ACID transactions and supports regular update transactions with pessimistic locking as well as [MVCC transactions](mvcc.md) that allow lock free read-only access to a consistent database snapshot.

Transactions are always [session](sessions.md) global and apply to all [databases](databases.md) opened in the current session. 

The transaction mode ([MVCC](mvcc.md)/update) is configured per database (`Database::SetMVCC()`) with the default being the regular update mode.

Transactions are started implicitly in blobs.db whenever a database access is performed the first time outside of a transaction (except for [dirty reads](dirty_reads.md)). In that moment a transaction is started for all databases opened in that session on the same database server. If the session has also opened databases on another server, the transaction for these databases is started when they are first accessed (implemented like this to avoid starting unnecessary transactions on servers that are not being touched).

Transactions are committed/aborted explicitly through the calls to `Transaction::Commit()` and `Transaction::Abort()`, which both take an optional session paramater to specify the session in which the transaction should be committed/aborted. If no session parameter is specified then the single global [session](sessions.md) is used.

A [database](databases.md) cannot be closed if a transaction is currently in progress.

## Commit
Changes performed to a database during the transaction are only stored on the client and only visible to the client itself (saved in the cache of the `blobs::Database` instance). During the commit all these accumulated changes are transferred to the [server](server.md) to be persisted in the database file (or in-memory).

If multiple databases are opened on the same server, then the commit is performed for all databases simultaneously and once the `Transaction::Commit()` function returns, all affected database files will have been updated.

Since writing multiple files can never be an atomic operation, the commit across more than one database cannot be truly atomic either. The database files are still updated in sequence and if the server terminates in between two updates, one database may see the committed state, while the other still sees the old state. This is a consequence of deciding to NOT have a transaction log file, which could otherwise be used to detect such a termination and finalize the commit on the remaining databases. \
But no matter when the server terminates, the copy-on-write data structure of the [databases](databases.md) ensures that they will always be in a consistent state when opening them after the server termination. They will be either in the state before the commit or after the commit, nothing in between.

If multiple databases are opened across multiple servers, then the client will send a commit message to each database server and wait for all of them to complete the commit before returning from the `Transaction::Commit()` function. This again has the potential issue that blobs.db cannot guarantee that for the involved databases either all are committed or none is committed. This also cannot be solved with a simple transaction log.

After the commit all [locks](locks.md) held on databases in regular update transaction mode can be reclaimed by the server.

## Abort
As all modified database state is stored only on the client until the transaction is committed, aborting a transaction simply means discarding all these modifications on the client and notifying the database server about the aborted transaction to potentially reclaim any held [locks](locks.md).

A transaction is implictily aborted when a database operation deadlocks (a `blob::exception::Deadlock` is thrown on the client) or when a client just closes the connection to the server.