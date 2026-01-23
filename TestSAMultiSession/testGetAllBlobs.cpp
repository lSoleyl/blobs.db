#include "pch.hpp"

using namespace blobs;



//TODO querying the list, then deleting a blob and creating a blob and re-querying the list in the same transaction should work correctly

//TODO querying the list should block blob creation/deletion and cluster deletion


//TODO querying the list should be blocked by blob creation/deletion and cluster deletion


//TODO Querying the list AFTER deleting a blob should work

//TODO after creating a cluster we implicitly hold the blob list lock for that cluster (should be preserved as a sticky lock)

//TODO query list, create blob, commit, query list (ensure the list is correct / not wrongly cached)
