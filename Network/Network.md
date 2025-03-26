# Networking static library

Wraps all the logic necessary for network communication between client and server and is used by both client and server as
it defines the common message data structures.

## Basic networking concept

All network communication is performed by a single thread using IO completion ports for efficient communication.