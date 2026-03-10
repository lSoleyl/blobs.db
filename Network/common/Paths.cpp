#include <common/Paths.hpp>

#include <vector>
#include <sstream>
#include <filesystem>



namespace blobs {


std::wstring Paths::GetWorkingDirectory() {
  return std::filesystem::current_path().native();
}

bool Paths::IsSame(std::wstring_view path1, std::wstring_view path2) {
  try {
    // Check for equality first otherwise for non existing paths the result would be false when passing two identical paths.
    return path1 == path2 || std::filesystem::equivalent(path1, path2);
  } catch (std::filesystem::filesystem_error&) {
    // We don't really care about errors here.
    return false;
  }
}

std::optional<std::wstring> Paths::ResolvePath(std::wstring_view root, std::wstring_view resolve) {
  try {
    std::filesystem::path rootPath(root);
    std::filesystem::path resolvePath(resolve);


    auto absolutePath = resolvePath.is_absolute() ? resolvePath : rootPath / resolvePath;
  
    // Root must be a prefix of the passed absolute path then... How can we verify this?
    // If the relative path from rootPath to resolvePath is not empty and does not start with '..' then 
    // the passed resolvePath should be a valid path inside rootPath
    auto fromRoot = std::filesystem::relative(absolutePath, rootPath);
    return (fromRoot.empty() || *fromRoot.begin() == L"..") ? std::nullopt : std::optional<std::wstring>(absolutePath.native());
  } catch (std::filesystem::filesystem_error&) {
    // If some operation fails with an error (only relative() should be able to fail here) then resolving the path failed
    return std::nullopt;
  }
}


std::wstring Paths::ResolvePath(std::wstring_view pathString) {
  // This works correctly no matter whether an absolute or relative path is passed in and it also
  // replaces directory separators with the preferred version.
  return std::filesystem::absolute(pathString).native();
}


void Paths::MakeDirs(std::wstring_view path, bool parent) {
  std::filesystem::path dirPath(path);
  if (parent) {
    dirPath = dirPath.parent_path();
  }
  std::filesystem::create_directories(dirPath);
}


}

