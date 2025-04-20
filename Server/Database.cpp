#include "pch.hpp"
#include "Database.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), lastSegmentId(0), commitId(1) {
  segments.emplace(0, std::make_unique<Segment>(0));
  //TODO: how do we initialize the initial blob to commitId 1?
}

Database& Database::Get(std::string_view databaseName) {
  auto pos = databases.find(databaseName);
  if (pos != databases.end()) {
    return pos->second;
  } 

  std::string nameStr(databaseName.data(), databaseName.size());
  return databases.emplace(nameStr, Database(nameStr)).first->second;
}

Blob* Database::GetBlob(const BlobLocation& location) {
  if (auto segment = GetSegment(location.segment)) {
    if (auto cluster = segment->GetCluster(location.cluster)) {
      if (auto blob = cluster->GetBlob(location.blob)) {
        return blob;
      }
    }
  }
  return nullptr;
}


Segment* Database::GetSegment(segment_id segment) {
  auto pos = segments.find(segment);
  return (pos != segments.end()) ? pos->second.get() : nullptr;
}

}}