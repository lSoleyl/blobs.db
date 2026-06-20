# The blobs.db server

To start up a [database](databases.md) server with default configuration simply run the `blobs_server.exe`.
The server will by default listen on port 8108 and serve databases only from the relative `.\databases` folder (that will be created if needed).

The blobs_server.exe file is fully self contained and does not depend on any .dll or configuration files (except for the C++ runtime dlls and WinAPI). The server doesn't write any transaction log files as the database's copy-on-write file data structure ensures a consistent state no matter when a writing process should be interrupted by a power outage.
The server is also pretty compact with currently just 345 KB file size (version 0.8.0).


To be able to connect to a running server, the client application must be linked against the `blobs.db.lib`. 
A client application can alternatively be linked against the `blobs.sa.db.lib` if the client should run in [standalone mode](standalone.md) (server integrated in the client).

**Warning:** At the moment the server accepts all incoming connections on the opened port as it doesn't currently implement any authentication and also no transport encryption. Deploying the server on a publicly accessible endpoint is a security risk!

The server opens all [database](databases.md) files in exclusive write mode to ensure that no two servers running on the same machine accidentially attempt to modify the same database file.

## Database root (`--dbroot` option)
The server has a database root directory (default: `.\databases`). This root directory is where all databases that are accessible by this server live and is created if it doesn't exist yet. It serves as a way to limit the access of the clients to a specific part of the filesystem and can be disabled (`--nodbroot` option) if clients should be allowed to open/create databases anywhere in the filesystem.

Even with a database root set, the clients can still open databases with absolute paths as long as they end up inside the databse root directory. If for example the database root is specified as `C:\data\dbs` then a database can be opened either as `test.db` or as `C:\data\dbs\test.db`, but `C:\test.db` will be rejected.

If the database root feature is disabled via `--nodbroot` then all relative database paths will be resolved relative to the working directory of the server process.

## Logging
The server process by default logs in `INFO` level to stdout.\
The loglevel can be changed to any of `OFF`, `DEBUG`, `INFO`, `WARN`, `ERROR` using the `--loglevel` parameter.\
Logging can be written into a file by specifying the `--logfile` parameter.

