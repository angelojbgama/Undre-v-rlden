#pragma once

#include "game/presentation/visual_content.h"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::editor {

enum class AssetBrowserRoot {
    gameAssets,
    contentWorkspace,
};

struct AssetBrowserEntry final {
    AssetBrowserRoot root{AssetBrowserRoot::gameAssets};
    std::filesystem::path relativePath;
    std::uintmax_t fileSize{};
};

// Metadata-only catalog. Image decoding remains in EditorVisualPreview, so a
// picker refresh never performs expensive decode work on the UI thread.
class AssetBrowserCatalog final {
public:
    void refresh(const std::filesystem::path& gameAssetsRoot,
                 const std::optional<std::filesystem::path>& contentWorkspaceRoot);

    [[nodiscard]] const std::vector<AssetBrowserEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::vector<std::size_t> search(std::string_view query) const;
    [[nodiscard]] static bool isSafeRelativePath(const std::filesystem::path& path) noexcept;

private:
    std::vector<AssetBrowserEntry> entries_;
};

} // namespace underworld::editor
