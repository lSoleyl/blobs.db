#include "pch.hpp"
#include "Database.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), lastSegmentId(0) {
  segments.emplace(0, std::make_unique<Segment>(0));
}

Database& Database::Get(std::string_view databaseName) {
  auto pos = databases.find(databaseName);
  if (pos != databases.end()) {
    return pos->second;
  } 

  std::string nameStr(databaseName.data(), databaseName.size());
  return databases.emplace(nameStr, Database(nameStr)).first->second;
}



}}