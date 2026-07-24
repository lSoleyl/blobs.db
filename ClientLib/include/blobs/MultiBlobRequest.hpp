#pragma once

#include "Config.hpp"

#include <memory>
#include <vector>
#include <string_view>

namespace blobs {


/** Objects of this class are passed to Database::ReadBlobs() or Database::WriteBlobs() to read/write multiple blobs
 *  in a single request.
 * 
 *  When reading multiple blobs the blob entries' data member will usually point into the client's transaction commit cache or database cache.
 *  These memory references will stay valid at least until the blob is overwritten or the transaction is committed/aborted.
 *  When performing dirty reads the memory will point into the request's local blob cache, which will stay valid until this MultiBlobRequest
 *  is reused or deleted.
 * 
 *  When writing multiple blobs the caller is responsible to make sure that all data refrenced by the `BlobEntry::data` members is valid
 *  at least until the Database::WriteBlobs() call finishes. To ensure this the caller may use MultiBlobRequest::CopyIntoCache() to copy the
 *  data to write into the request's local blob cache to guarantee that it will stay valid long enough.
 */
class MultiBlobRequest {
public:
  struct Deleter { void operator()(MultiBlobRequest* request) { request->Release(); } };

  /** Creates a new MultiBlobRequest object preallocating it for the specified number of blobs to request.
   */
  static std::unique_ptr<MultiBlobRequest, Deleter> Create(size_t nBlobs) {
    return std::unique_ptr<MultiBlobRequest, Deleter>(CreateInternal(nBlobs));
  }


  /** The entries stored in a multi blob request.
   */
  struct BlobEntry {
    segment_id segment = 0;
    cluster_id cluster = 0;
    blob_id blob = 0;

    const void* data = nullptr;
    size_t size = 0; // of data

    /** Utility method to copy the content of the blob into a std::string (or std::wstring) and return it
     */
    template<typename CharT = char>
    std::basic_string<CharT> GetString() const {
      return std::basic_string<CharT>(static_cast<const CharT*>(data), size / sizeof(CharT));
    }

    /** Utility method to acces the contents for this blob as string_view.
     *  Warning: As this will usually point into the client's transaction cache or temporary memory.
     *           Check the description on MultiBlobRequest for how long the data will remain vaid.
     */
    template<typename CharT = char>
    std::basic_string_view<CharT> GetStringView() const {
      return std::basic_string_view<CharT>(static_cast<const CharT*>(data), size / sizeof(CharT));
    }

    /** Utility method to assign a string to write in the multi blob request. 
     *  The caller must ensure that the set string reference remains valid until the Database::WriteBlobs() call finishes
     *  If in doubt, use the request's CopyIntoCache() method to copy the string into the request's internal blob cache
     *  and assign that as value.
     */
    template<typename CharT = char>
    void SetString(std::basic_string_view<CharT> stringData) {
      data = stringData.data();
      size = stringData.size() * sizeof(CharT);
    }

    /** Same above. This overload allows the compiler to correctly deduce the template argument.
     */
    template<typename CharT = char>
    void SetString(const CharT* stringData) {
      SetString(std::basic_string_view<CharT>(stringData));
    }
  };

  /** begin()/end() for range based iteration
   */
  BLOBS_EXPORT BlobEntry* begin();
  BLOBS_EXPORT BlobEntry* end();
  BLOBS_EXPORT const BlobEntry* begin() const;
  BLOBS_EXPORT const BlobEntry* end() const;

  /** Index access
   */
  BLOBS_EXPORT BlobEntry& operator[](size_t i);
  BLOBS_EXPORT const BlobEntry& operator[](size_t i) const;

  /** Returns number of requested blobs in this request
   */
  BLOBS_EXPORT size_t Size() const;
  

  /** Copies the given data into the request's local blob cache. This cache is used to hold
   *  the results from dirty reads as these results are not stored anywhere on the client 
   *  (neither database cache, nor transaction's commit cache) to make these contents outlive the
   *  duration of the Database::ReadBlobs() call.
   */
  BLOBS_EXPORT const void* CopyIntoCache(const void* data, size_t size);

  /** A convenience overload, which copies the passed string into the request's blob cache and returns
   *  a string reference with the same content, but now pointing into the blob cache copy. This method may be 
   *  used together with BlobEntry::SetString() to assign strings that may not live long enough.
   */
  template<typename CharT = char>
  std::basic_string_view<CharT> CopyIntoCache(std::basic_string_view<CharT> stringData) {
    return std::basic_string_view<CharT>(static_cast<CharT*>(CopyIntoCache(stringData.data(), stringData.size() * sizeof(CharT)), stringData.size()));
  }

  /** This overload is needed, because the compiler cannot deduce the string_view template argument from a const char*
   */
  template<typename CharT = char>
  std::basic_string_view<CharT> CopyIntoCache(const CharT* stringData) {
    return CopyIntoCache(std::basic_string_view<CharT>(stringData));
  }

  /** Clears the request's internal blob cache. This method will be called when entering ReadBlobs() and when exiting WriteBlobs()
   */
  BLOBS_EXPORT void ClearCache();
private:
  MultiBlobRequest(size_t nBlobs);
  ~MultiBlobRequest();

  BLOBS_EXPORT static MultiBlobRequest* CreateInternal(size_t nBlobs);
  
  /** Called instead of `delete` to release the MultiBlobRequest. This is automatically called by the Deleter
   *  in the std::unique_ptr<> returned from Create()
   */
  BLOBS_EXPORT void Release();


  std::vector<BlobEntry> entries;
  std::vector<std::vector<uint8_t>> blobCache; // A local blob cache, that is needed to hold the blob data as the default cache can only hold one blob
                                               // This cache can also be used to store the blob content for WriteBlobs()
};

}