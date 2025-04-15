#pragma once

#include "Segment.hpp"

namespace blobs {
namespace server {

class Database {
public:
  //TODO: later we must separate these two paths as opening will mean reading and parsing a file, while
  //      fetching is simply a memory lookup. We shouldn't block the server while watiting for the database to be fully loaded and ready.

  /** Fetch an already opened database or open the specified database 
   */
  static Database& Get(std::string_view databaseName);

  //TODO: add commitId
  
  //TODO: should the database keep an open count to efficiently perform the check whether any client still uses it?
private:
  Database(std::string name);

  std::string name;

  /** Global commit id counter of the last commited transaction for this database.
   *  Initialized to 1 for a new database.
   */
  commit_id commitId;
  segment_id lastSegmentId;
  std::unordered_map<segment_id, std::unique_ptr<Segment>> segments;
  static std::map<std::string, Database, std::less<>> databases;
};



}}

