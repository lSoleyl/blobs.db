#include <blobs/Blobs.hpp>

#include <iostream>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <random>

constexpr size_t kB = 1024;
constexpr size_t MB = kB * 1024;
constexpr size_t GB = MB * 1024;

struct Unit { size_t bytes; std::string_view suffix; };
const std::vector<Unit> units = {
  { GB, "GB" },
  { MB, "MB" },
  { kB, "kB" },
  {  1, "bytes" }
};

struct Size {
  Size(size_t bytes) : bytes(bytes) {}
  size_t bytes;
};

std::ostream& operator<<(std::ostream& out, const Size& size) {
  bool first = true;
  auto bytes = size.bytes;
  for (auto& unit : units) {
    if (bytes >= unit.bytes) {
      if (first) {
        first = false;
      } else {
        out << ", ";
      }
      auto factor = bytes / unit.bytes;
      out << factor << ' ' << unit.suffix;
      bytes %= unit.bytes;
    }
  }

  if (first) {
    // special case: 0 bytes
    out << "0 bytes";
  }

  return out;
}



struct Configuration {
  blobs::segment_id nSegments = 20;
  blobs::cluster_id nClusters = 20;
  blobs::blob_id nBlobs = 20;
  blobs::blob_size blobDataSize = 1024;
  size_t blobsPerTxn = 100;

  blobs::Database* db = nullptr;

  /** This method will be used to print out an estimate of the total size
   */
  void EstimateTotalSize() const {
    // Size of the raw data structures in bytes
    const size_t BlobSize = 96;
    const size_t ClusterSize = 200;
    const size_t BlobMapEntry = 24; // blob_id + std::shared_ptr<> ~24 bytes for alignment
    const size_t SegmentSize = 200;
    const size_t ClusterMapEntry = 24;


    auto perBlob = BlobSize + blobDataSize;
    auto perCluster = ClusterSize + (perBlob + BlobMapEntry) * nBlobs;
    auto perSegment = SegmentSize + (perCluster + ClusterMapEntry) * nClusters;
    auto perDatabase = perSegment * nSegments; // simplified

    std::cout
      << "Estimated server memory usage: \n"
      << " per Blob:     " << Size(perBlob) << "\n"
      << " per Cluster:  " << Size(perCluster) << "\n"
      << " per Segment:  " << Size(perSegment) << "\n"
      << " per Database: " << Size(perDatabase) << "\n"
    ;
  }


  /** This method will create the blobs, clusters, segments
   */
  void CreateData() const {
    size_t createdBlobs = 0;
    std::string blobData(blobDataSize, '\0');

    for (blobs::segment_id iSegment = 0; iSegment < nSegments; ++iSegment) {
      if (iSegment) { // first segment always already exists
        db->CreateSegment();
      }

      for (blobs::cluster_id iCluster = 0; iCluster < nClusters; ++iCluster) {
        if (iCluster) { // first cluster always already exists
          db->CreateCluster(iSegment);
        }

        for (blobs::blob_id iBlob = 0; iBlob < nBlobs; ++iBlob) {
          GenerateData({ iSegment, iCluster, iBlob }, blobData);
          
          if (iBlob) { 
            db->CreateString(iSegment, iCluster, blobData);
          } else { // first blob always already exists
            db->WriteString(iSegment, iCluster, iBlob, blobData);
          }

          if (++createdBlobs % blobsPerTxn == 0) {
            // commit after the configured amount of blobs written
            std::cout << "Created " << createdBlobs << " blobs... committing\n";
            blobs::Transaction::Commit();
          }
        }
      }
    }

    blobs::Transaction::Commit();
  }
  
  /** Reads all blobs in a sequential order nLoops times.
   *  The blobs are all read using dirty reads to avoid any caching and the need to handle transactions
   */
  void ReadSequential(size_t nLoops) {
    const size_t totalBlobs = TotalBlobs();
    std::string expectedDataBuffer(blobDataSize, '\0');

    for (size_t loop = 0; loop < nLoops; ++loop) {
      std::cout << "ReadSequential loop " << (loop + 1) << "/" << nLoops << std::endl;
      for (size_t i = 0; i < totalBlobs; ++i) {
        auto location = LocationFromIndex(i);
        GenerateData(location, expectedDataBuffer);
        CheckBlobContents(location, expectedDataBuffer);
      }
    }
  }


  /** Reads TotalBlobs() blobs of the database in a pseudo random order and repeats this nLoops times.
   *  The blobs are all read using dirty reads to avoid any caching and the need to handle transactions
   */
  void ReadRandom(size_t nLoops) {
    const size_t totalBlobs = TotalBlobs();
    std::string expectedDataBuffer(blobDataSize, '\0');

    // Use fixed seed for reproducible behavior
    std::mt19937_64 rand(0); 
    std::uniform_int_distribution<size_t> dist(0, totalBlobs-1);

    for (size_t loop = 0; loop < nLoops; ++loop) {
      std::cout << "ReadRandom loop " << (loop + 1) << "/" << nLoops << std::endl;
      for (size_t i = 0; i < totalBlobs; ++i) {
        auto location = LocationFromIndex(dist(rand));
        GenerateData(location, expectedDataBuffer);
        CheckBlobContents(location, expectedDataBuffer);
      }
    }
  }


  /** Reads every blob to valiate its contents and then updates it with a slightly modified content and repeats this process nLoops times.
   *  The transaction is committed every blobsPerTxn blobs
   */
  void UpdateSequential(size_t nLoops) {
    const size_t totalBlobs = TotalBlobs();
    std::string expectedDataBuffer(blobDataSize, '\0');

    for (size_t loop = 0; loop < nLoops; ++loop) {
      int readOffset = static_cast<int>(loop);

      std::cout << "UpdateSequential loop " << (loop + 1) << "/" << nLoops << std::endl;
      for (size_t i = 0; i < totalBlobs; ++i) {
        auto location = LocationFromIndex(i);
        
        // Validate that the blob looks as expected
        GenerateData(location, expectedDataBuffer, readOffset);
        CheckBlobContents(location, expectedDataBuffer);

        // Now update the data and write it back into the blob
        std::transform(expectedDataBuffer.begin(), expectedDataBuffer.end(), expectedDataBuffer.begin(), [](char byte) { return byte + 1; });
        db->WriteString(location.segment, location.cluster, location.blob, expectedDataBuffer);

        if (i % blobsPerTxn == 0) {
          blobs::Transaction::Commit();
        }
      }
    }

    blobs::Transaction::Commit();
  }



private:
  struct Location {
    blobs::segment_id segment;
    blobs::cluster_id cluster;
    blobs::blob_id blob;
  };

  /** Generates the inital test data fill the generated blobs with.
   * 
   * @param location the location to generate the blob for. The address will be written into the blob itself and
   *                 the pattern's start byte is based of the address.
   * @param dataOut where to write the contetn to. This string should be presized to the blob size and will be filled with the pattern.
   * @param offset used for update runs when updating the blobs to create a slightly modified pattern.
   */
  void GenerateData(const Location& location, std::string& dataOut, int offset = 0) const {
    auto writePos = dataOut.data();
    auto writeEnd = writePos + dataOut.size();
    // Convert location into size_t[], because we cannot know whether segment_id, cluster_id, blob_id are all the same
    // size and thus no padding bytes are included in the structure itself. Otherwise we could just write the structure itself.
    size_t prefix[] = { location.segment, location.cluster, location.blob }; 
    auto prefixBegin = reinterpret_cast<char*>(prefix);
    auto prefixEnd = prefixBegin + sizeof(prefix);

    writePos = std::copy(prefixBegin, prefixEnd, writePos);
    char byte = std::accumulate(prefixBegin, prefixEnd, (char)0, [](char a, char b) { return a ^ b; });

    // Fill data with increasing byte pattern
    std::generate(writePos, writeEnd, [&]() { return ++byte; });

    if (offset) {
      // Apply offset to each byte
      std::transform(dataOut.begin(), dataOut.end(), dataOut.begin(), [=](char byte) { return byte + offset; });
    }
  }

  void CheckBlobContents(const Location& location, const std::string& expectedData) {
    auto [data, size] = db->ReadBlob(location.segment, location.cluster, location.blob, blobs::Lock::None);
    auto begin = static_cast<const char*>(data);
    auto end = begin + size;

    // Now use std::mismatch() to compare the acutal blob data with the expected blob data and
    // be able to reuse the position for a more accurate error message in case there is a mismatch
    auto [dataPos, expectedPos] = std::mismatch(begin, end, expectedData.begin(), expectedData.end());
    if (dataPos != end || expectedPos != expectedData.end()) {
      auto index = std::distance(begin, dataPos);
      std::ostringstream msg;
      msg << "Blob data mismatch in blob (" << location.segment << "," << location.cluster << "," << location.blob << ") at byte " << index << "!!!\n";
      msg << " expected: " << std::hex;
      if (expectedPos != expectedData.end()) {
        msg << static_cast<uint32_t>(*expectedPos);
      } else {
        msg << "[end of data]";
      }
      msg << "\n actual: ";
      if (dataPos != end) {
        msg << static_cast<uint32_t>(*dataPos);
      } else {
        msg << "[end of data]";
      }
      throw std::exception(msg.str().c_str());
    }
  }

  /** Calculates the location from an index between 0 and TotalBlobs()
   */
  Location LocationFromIndex(size_t index) const {
    size_t blobsPerSegment = static_cast<size_t>(nBlobs) * nClusters;
    Location location;
    location.segment = static_cast<blobs::segment_id>(index / blobsPerSegment);
    index %= blobsPerSegment;
    location.cluster = static_cast<blobs::cluster_id>(index / nBlobs);
    location.blob = static_cast<blobs::blob_id>(index % nBlobs);
    return location;
  }

  size_t TotalBlobs() const {
    return static_cast<size_t>(nBlobs) * nClusters * nSegments;
  }

};



// This client will be used to test the LRU uncaching of blobs,clusters,segments in the server.
// Just start the server with a small enough limit and launch this client.
// This client will create 10 segments with 10 clusters each with 10 blobs each. Each of size 1024 bytes.

int main() {
  

  Configuration config;
  config.EstimateTotalSize();
  std::cout << "Confirm to start the test\n";
  system("PAUSE");

  try {
    blobs::Initialize();
    config.db = blobs::Database::Open("localhost/test_server_lru.db", blobs::Database::OpenMode::CreateAlways);

    // Now since we are testing the server's LRU cache, we don't want sticky locks to result in skipping the database access
    blobs::Transaction::UseStickyLocks(false);

    config.CreateData();
  
    // Then Read data sequentially
    config.ReadSequential(10);

    // Then read randomly
    config.ReadRandom(10);

    // Then read and update data
    config.UpdateSequential(10);


  } catch (blobs::Exception& ex) {
    blobs::Transaction::Abort();
    std::cerr << "Error caught: " << ex.what();
  } catch (std::exception& ex) {
    blobs::Transaction::Abort();
    std::cerr << ex.what();
  }

  if (config.db) { // opening database may fail
    config.db->Close();
  }
  blobs::Shutdown();
  return 0;
}