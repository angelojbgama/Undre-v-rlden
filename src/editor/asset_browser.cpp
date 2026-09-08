#include "editor/asset_browser.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace underworld::editor {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void scanRoot(AssetBrowserRoot root, const std::filesystem::path& absoluteRoot,
              std::vector<AssetBrowserEntry>& output) {
    std::error_code error;
    const auto rootStatus = std::filesystem::symlink_status(absoluteRoot, error);
    if (error || !std::filesystem::is_directory(rootStatus) ||
        std::filesystem::is_symlink(rootStatus)) return;
    std::filesystem::recursive_directory_iterator iterator(
        absoluteRoot, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        const auto status = iterator->symlink_status(error);
        if (error) { error.clear(); continue; }
        if (std::filesystem::is_symlink(status)) { iterator.disable_recursion_pending(); continue; }
        if (!std::filesystem::is_regular_file(status) ||
            lower(iterator->path().extension().string()) != ".png") continue;
        const auto relative = std::filesystem::relative(iterator->path(), absoluteRoot, error);
        if (error || !AssetBrowserCatalog::isSafeRelativePath(relative)) {
            error.clear();
            continue;
        }
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(iterator->path(), sizeError);
        output.push_back({root, relative.lexically_normal(), sizeError ? 0 : size});
    }
}

} // namespace

bool AssetBrowserCatalog::isSafeRelativePath(const std::filesystem::path& path) noexcept {
    if (path.empty() || path.is_absolute() || !path.root_name().empty()) return false;

    // Asset paths are persisted as portable relative paths.  On POSIX,
    // std::filesystem intentionally does not recognize a Windows drive or
    // backslash separator, so check those spellings explicitly as well.
    const auto raw = path.generic_string();
    if (raw.empty() || raw.front() == '/' || raw.front() == '\\' ||
        (raw.size() >= 2 && std::isalpha(static_cast<unsigned char>(raw[0])) &&
         raw[1] == ':')) return false;

    std::size_t componentStart = 0;
    while (componentStart <= raw.size()) {
        const auto separator = raw.find_first_of("/\\", componentStart);
        const auto component = raw.substr(componentStart,
            separator == std::string::npos ? std::string::npos : separator - componentStart);
        if (component.empty() || component == "." || component == "..") return false;
        if (separator == std::string::npos) break;
        componentStart = separator + 1;
    }
    return true;
}

void AssetBrowserCatalog::refresh(
    const std::filesystem::path& gameAssetsRoot,
    const std::optional<std::filesystem::path>& contentWorkspaceRoot) {
    entries_.clear();
    scanRoot(AssetBrowserRoot::gameAssets, gameAssetsRoot, entries_);
    if (contentWorkspaceRoot) scanRoot(AssetBrowserRoot::contentWorkspace,
                                        *contentWorkspaceRoot, entries_);
    std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        if (left.root != right.root) return left.root < right.root;
        return left.relativePath.generic_string() < right.relativePath.generic_string();
    });
    entries_.erase(std::unique(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        return left.root == right.root && left.relativePath == right.relativePath;
    }), entries_.end());
}

std::vector<std::size_t> AssetBrowserCatalog::search(std::string_view query) const {
    const auto needle = lower(std::string(query));
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto haystack = lower(entries_[index].relativePath.generic_string());
        if (needle.empty() || haystack.find(needle) != std::string::npos) result.push_back(index);
    }
    return result;
}

} // namespace underworld::editor
