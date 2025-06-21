# Client lib

This static library holds the client logic and implementation of client datastructures. 
This library is used to reuse the client logic between ClientDLL and CLientDLLStandalone.


## The client needs to know the commit id of each blob
That way he can keep an LRU cache of the most recently used blobs and can include the commit id of the currently known blobs similar 
to an "If-Modified-Since" or "E-Tag" header in HTTP and thus save the server the trouble of transmitting pages, which he already has.

To be able to keep this functionality even after a modifying blobs in a commit, the server must respond with the commit id when successfully commiting
a write transaction. That way the client can update all the commit ids of all modified blobs in his cache.