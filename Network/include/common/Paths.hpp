#pragma once

#include <string>
#include <optional>

namespace blobs {

/** A class with static helper functions for dealing with file paths
 */
class Paths {
public:

  static std::wstring GetWorkingDirectory();


  /** Helper function to match two file paths and check whether they resolve to the same file.
   *  This is mainly to handle case-insensitivy on windows systems
   *  Warning: This will return false if two paths differ in casing and the path refers to a non existing file
   */
  static bool IsSame(std::wstring_view path1, std::wstring_view path2);

  /** Resolves a path relative to the specified root path.
   *  The specified relative path cannot navigate outside of the root path, so all navigation elements attempting to do so
   *  will be ignored.
   *  An absolute path is accepted if it starts with the specified root. Otherwise an empty optional is returned.
   */
  static std::optional<std::wstring> ResolvePath(std::wstring_view root, std::wstring_view path);

  /** Resolves the specified path by removing any navigation elements from it.
   *  If a relative path is passed, then it is resolved relative to the current working directory.
   * 
   * @throws std::filesystem::filesystem_error if conversion to an absolute path fails.
   */
  static std::wstring ResolvePath(std::wstring_view path);

  /** Create all directories specified in the given path
   * 
   * @param parent if true, then the path's parent directories are created and the last path element is not treated like a directory.
   */
  static void MakeDirs(std::wstring_view path, bool parent = false);
};


}