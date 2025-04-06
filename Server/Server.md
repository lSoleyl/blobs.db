# Server



## Global commit id
The server holds a global 64-bit commit id(counter), which is stored in the database. 
Each transaction commit, which modifies at least one blob increments this counter by 1.

### Commit id in blob header
Each blob knows (in some header) the commit id in which it has been commited.
This serves two purposes:

  1. It allows the client to keep pages in its cache across transactions and in a new transaction only request pages, which changed since the last transaction. This can save a lot of network bandwidth.

  2. It allows the server to securely update data structures in the database. By attaching the commit id to updated pages/clusters/segments, the server can simply ignore partial updates on the next boot by ignoring all data, which has a higher commit id than the database.

### Id overflow
Since we use 64-bit an overflow should practically never occur, but we could define a threshold - if reached at server boot time -
would cause a rewrite of all ids back to 0.


## Transaction log
To ensure consistency even in face of a crash, we should keep a transaction log. The main idea is that writing updates to the transaction log
should be much faster than commiting these updates to the actual database file. 

### Transaction commit
If a client commits a transaction, then it transmits all modified blobs together with the commit message. These blobs need to be written into the 
transaction log and only once this write process is complete (!!! flushed to disk!?), the client can be notified that the transaction is actually commited 
and only then the transient data structures are updated too.

### Log propagation
To prevent the log from blowing up, we must in regular intervals propagate changes from the log into the database. 
It would be optimal, if this propagation could be processed by a separate thread. How exactly, is not yet known.
One important property of log propagation must be that after each write the database stays in a consistent state.

#### Transient transaction log
We can keep a transient copy of the transaction log and have a separate thread simply keep processing it as soon as something is added to it. 
This would make log propagation a bit more efficient and the server would only need to keep track of which pages are being written right now to not attempt
to read them from disk while they are being overwritten. But this will not happen by design as we keep all read pages in memory anyway (at least all recently read pages).
So the page being written by the recent transaction is already loaded into the server's memory, so no file access is necesary for this. 

As long as growing the database file does not somehow erase it temporarily, we can still safely read any other not yet touched page without any risks.

### Consistent database update process
One rather primitive way to achieve consistency is to simply update all written data in-place. Once the update is completed the global commit id is incremented accordingly.
That way the database can in recovery mode understand, which part of the transaction log has already been processed and which wasn't.

In place modification may however not be possible, because blobs (and their metadata) may change in size, so they must be relocated. So instead the database
should have some blocks of unused memory (we need some kind of smart memory management for this) and should write all updated blobs into separate blocks (copy on write).
Then it must copy the datastructures referencing the blob (segment->cluster->blob) and also put them into a new block. These datastructures will also hold a commit id (just like blobs).

By performing the update like this, we can perform a final update by simply overwriting the offset where the database header is found, which would be only an 8 byte write, 
which should by all accounts be atomic. Also if any of the writes fail, the database will be simply left in its previous state.

#### Considerations
Because the cluster needs to know all of its blobs, this copying approach would also mean that transactions would be processed slower the more blobs a cluster has.
Admittedly this is only an 8-byte copy per blob, but still means that we need to write 4kB more every 512 blobs.
An alternative would be to not create a flat list, but some multi level tree like list structure. That reduce the amount of data to copy, 
but would add an additioanl memory and runtime overhead for managing it.

### Log location
We could also allocate the database log inside the database file (idk yet in what kind of datastructure), but if the blobs to be written are already allocated in the database file
then the udpate is simply a matter of writing the offsets to these blobs into the copied datastructures, which would massively speed up log propagation.

## Memory management
The current design relies on data strucutres being allocated and deallocated in the database file just like in heap memory. So to implement this behavior we must have some allocator and
the necessary metadata in the databse file itself. (freeList/heap/...)

**Important:** Here we must be especially careful not to leave this memory management data structures in an inconsistent state no matter when the power is cut!

Since we cannot assume fixed sized blobs (even with memory pages due to the dynamic sized metadata), we must make sure to not cause any unnecessary fragmentation in the database.
We will assume constant time access to any part of the file (SSD drives) so we won't put too much effort in putting things together, which belong together.

### Fixed allocation sizes
We could store our free lists in less memory if we separate them into fixed sized chunks. This could also ensure that reallocation will not create any weird gaps... But then again...
Maybe we should just allocate as much as is needed and ignore the wasted memory and implement some cleanup routine, which can be run from time to time.

## Datastructures
In the following notation Pointers must be translated to 64-bit file offsets.
The database file has one fixed field at file offset 8, which is the pointer to the currently valid Database header.

    - filetype: char[8] = 'blobs.db'
	- database: Database*

### Database
The database header holds the following data

	- commitId: uint64_t (Global commit id - should start at 1 for the first commit [see ReadBlobs message]) 
	- segments: List<Segment*> (All segments)


### Segment
A segment holds the following data

	- id: uint32_t
	- name: string (optional name)
	- commitId: uint64_t (commit id when this structure was last updated)
	- clusters: List<Cluster*>

### Cluster
A cluster holds the following data

	- id: uint32_t (simply a running number)
	- commitId: uint64_t (commit id when this structure was last updated)
	- blobs: List<Blob*>
	- maxBlobId: uint32_t (needed?)

### Blob
A blob holds the following data

	- id: uint32_t
	- commitId: uint64_t (commit id when this blob was last modified)
	- size: uint32_t (bytes in this blob - 4GB max)
	- data: uint8_t[]
	- metaSize: uint32_t
	- metaData: uint8_t[]

The metadata fields can also be completely replaced if we simply denote each odd numbered blob as metadata for the previous one. 
The client would then just always request two blobs at once. This would cause an overhead in communication and memory usage, but could simplify the logic
and wouldn't hardcode this metadata into each blob (which may not be needed in some cases at all).



## MVCC Mode
With the commit ids and the tree like data strucutres we can also quite efficiently implement an MVCC mode, by simply keeping the transient copies of old
blobs even when new versions are commited (but we only do this if there is at least one MVCC client).

We would thus hold a second in-memory copy of the database, which would grow in size (blobs) the more blobs are being touched since the MVCC snapshot has been made.
New clients opening an MVCC transaction afterwards can simply reuse the same snapshot.
Once the last client closes the MVCC transaction, the (transient) snapshot can be deleted and the memory freed.
 