# Client lib

This will be the DLL to link into the client programm to be able to connect to blobs.db and actually talk to the database server.
It will provide some needed abstractions to avoid writing much boilerplate.


## The client needs to know the commit id of each blob
That way he can keep an LRU cache of the most recently used blobs and can include the commit id of the currently known blobs similar 
to an "If-Modified-Since" or "E-Tag" header in HTTP and thus save the server the trouble of transmitting pages, which he already has.

To be able to keep this functionality even after a modifying blobs in a commit, the server must respond with the commit id when successfully commiting
a write transaction. That way the client can update all the commit ids of all modified blobs in his cache.