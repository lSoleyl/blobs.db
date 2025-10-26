#pragma once

#include <string>
#include <string_view>

namespace blobs::encoding {

/** Conversion from UTF16 to UTF8
 */
std::string ToUTF8(std::wstring_view u16String);

/** Conversion from UTF8 to UTF16
 */
std::wstring ToUTF16(std::string_view u8String);


}
