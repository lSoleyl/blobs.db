## Multi blob operations
Reading/Writing many blobs through the basic [database](databases.md) operations `ReadBlob()/WriteBlob()` will result in quite a lot of network overhead as the client will acquire the corresponding [lock](locks.md) (and blob content) before continuing with the next blob resuling in a very inefficient ping-pong.

The class `blobs::MultiBlobRequest` is used together with `Database::ReadBlobs()` and `Database::WriteBlobs()` to acquire locks (and blobs contents) for multiple blobs at once.

In contrast to indiviudal read/write requests a multi blob request is treated like a single atomic action on the server and thus cannot cause deadlocks by partially acquring some of the locks while waiting for the remaining ones. These operations will only complete once all locks are available and then acquire all at once.

Multi blob requests currently only support reading and writing blobs. Creation and deletion of multiple blobs at once is not (yet) supported.

At the moment the maximum number of blobs that can be read/written per request are limited to 255. Attempting to create a request for more blobs will throw an exception.

### Reading multiple blobs per request
Lets start with a usage example:

```cpp
auto db = blobs::Database::Open("localhost/test.db");

// Create a request object for requesting 5 blobs. This will return
// a unique_ptr with a custom deleter, handling deletion of the returned object.
auto requestPtr = blobs::MultiBlobRequest::Create(5); 
auto& request = *requestPtr;

// We want to read the contiguous blob range from 
// segment 0, cluster 0, starting at blob 3
request.SetBlobRange(0, 0, 3);
// The above SetBlobRange() is a shorthand for:
request[0].Set(0, 0, 3);
request[1].Set(0, 0, 4);
request[2].Set(0, 0, 5);
// ...


// Perform the read operation
db->ReadBlobs(request);

// Now we can access the contents
request[0].GetStringView(); // -> returns the contents of blob 0,0,3
request[1].GetStringView(); // -> returns the contents of blob 0,0,4
//...
// or alternatively iterate over the returned blobs
for (auto& blobEntry : request) {
  blobEntry.GetStringView();
}

blobs::Transaction::Commit();
db->Close();
```

`Database::ReadBlobs()` takes a `MultiBlobRequest` reference and an optional `LockMode` parameter to support acquiring write locks and performing [dirty reads](dirty_reads.md). Be aware that when performing dirty reads the blob's contents will be allocated inside the request object itself and the memory returned by `GetStringView()` will be invalidated when deleting the request object or when reusing it for another `ReadBlobs()/WriteBlobs()` operation.

Should any of the read blobs not exist or be marked for deletion in the current transaction then the whole operation fails with no blob contents being returned and no locks being acquired.

### Writing multiple blobs per request
Writing multiple blobs is performed in a similar way to reading. The caller just has to set the blob's contents in the `MultiBlobRequest` object before calling `Database::WriteBlobs()`. 

One important thing to note is that the referenced blob content set in the `MultiBlobRequest` must stay valid for the duration of the `WriteBlobs()` call. If this cannot be guaranteed by the caller, you may use `MultiBlobRequest::CopyIntoCache()` to copy the blob content into the request object itself to ensure it will live long enough.

Just like reading with multiple blobs, the blobs to be written must already exist in the database, otherwise the call will fail.

Example:
```cpp
auto db = blobs::Database::Open("localhost/test.db");

// Create a request object for writing 2 blobs. This will return
// a std::unique_ptr<> with a custom deleter, handling deletion of the request object.
auto requestPtr = blobs::MultiBlobRequest::Create(2); 
auto& request = *requestPtr;

// First we want to write a constant string into blob 0,0,0
request[0].Set(0, 0, 0).SetString("hello blobs!");

{
  // Then we want to write a string that may not survive long enough 
  // and thus ha to be copied into the request's cache to blob 0,0,7
  std::string data(1024, 'A'); // 1024 'A' characters
  request[1].Set(0, 0, 7).SetString(request.CopyIntoCache(data));
}

// Perform the write operation
db->WriteBlobs(request);

// Commit the write operation
blobs::Transaction::Commit();
db->Close();
```

