#include <common/BlobLocation.hpp>
#include <cassert>

namespace blobs {

BlobLocation::BlobLocation() : segment(0), cluster(0), blob(0) {
  FIXME("This field initialization is only here to remove the warning, but it is only unnecessary overhead in the regular case")
}

BlobLocation::BlobLocation(segment_id segment, cluster_id cluster, blob_id blob) : segment(segment), cluster(cluster), blob(blob) {}

bool BlobLocation::operator==(const BlobLocation& other) const {
  return segment == other.segment && cluster == other.cluster && blob == other.blob;
}

bool BlobLocation::operator!=(const BlobLocation& other) const {
  return !(*this == other);
}

bool BlobLocation::operator<(const BlobLocation& other) const {
  if (segment < other.segment) {
    return true;
  }

  if (segment > other.segment) {
    return false;
  }

  if (cluster < other.cluster) {
    return true;
  }

  if (cluster > other.cluster) {
    return false;
  }

  return blob < other.blob;
}

bool BlobLocation::operator<=(const BlobLocation& other) const {
  return *this == other || *this < other;
}

bool BlobLocation::operator>(const BlobLocation& other) const {
  return !(*this <= other);
}

bool BlobLocation::operator>=(const BlobLocation& other) const {
  return !(*this < other);
}


std::ostream& operator<<(std::ostream& out, const BlobLocation& location) {
  out << '(';
  
  // Translate special segment ids
  if (location.segment <= constants::MaxSegmentId) {
    out << location.segment << ',';
  } else if (location.segment == constants::NextFreeSegmentId) {
    out << "NextFreeSegmentId,";
  } else if (location.segment == constants::SegmentListId) {
    out << "SegmentListId,";
  } else {
    assert(!"Unknown segment id constant");
    out << "???,";
  }

  // Translate special cluster ids
  if (location.cluster <= constants::MaxClusterId) {
    out << location.cluster << ',';
  } else  if (location.cluster == constants::NextFreeClusterId) {
    out << "NextFreeClusterId,";
  } else if (location.cluster == constants::SegmentDeleteId) {
    out << "SegmentDeleteId,";
  } else if (location.cluster == constants::ClusterListId) {
    out << "ClusterListId,";
  } else {
    assert(!"Unknown cluster id constant");
    out << "???,";
  }

  // Translate special blob ids
  if (location.blob <= constants::MaxBlobId) {
    out << location.blob;
  } else if (location.blob == constants::NextFreeBlobId) {
    out << "NextFreeBlobId";
  } else if (location.blob == constants::ClusterDeleteId) {
    out << "ClusterDeleteId";
  } else if (location.blob == constants::BlobListId) {
    out << "BlobListId";
  } else {
    assert(!"Unknown blob id constant");
    out << "???,";
  }
  return out << ')';
}

}

namespace {
  // hash_combine as is used by boost
  template <class T>
  void hash_combine(std::size_t& hash, const T& v) noexcept {
    std::hash<T> hasher;
    hash ^= hasher(v) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }

  // Generic hasher function for arbitrary number of parameters
  template<typename... T>
  size_t hash_all(const T&... param) noexcept {
    size_t hash = 0;
    (hash_combine(hash, param), ...);
    return hash;
  }
}


size_t std::hash<blobs::BlobLocation>::operator()(const blobs::BlobLocation& location) const noexcept {
  return hash_all(location.segment, location.cluster, location.blob);
}
