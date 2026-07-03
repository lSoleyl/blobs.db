
#include <common/Encoding.hpp>

#include <Windows.h>

namespace blobs::encoding {


std::wstring ToUTF16(std::string_view content) {
  std::wstring result;
  auto requiredSize = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, content.data(), static_cast<int>(content.size()), nullptr, 0);
  result.resize(requiredSize);
  MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, content.data(), static_cast<int>(content.size()), result.data(), static_cast<int>(result.size()));
  return result;
}

std::string ToUTF8(std::wstring_view content) {
  std::string result;
  auto requiredSize = WideCharToMultiByte(CP_UTF8, NULL, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
  result.resize(requiredSize);
  WideCharToMultiByte(CP_UTF8, NULL, content.data(), static_cast<int>(content.size()), result.data(), static_cast<int>(result.size()), nullptr, nullptr);
  return result;
}



}