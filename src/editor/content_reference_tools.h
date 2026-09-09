#pragma once

#include "editor/content_workspace_document.h"

#include <string>
#include <string_view>
#include <vector>

namespace underworld::editor {

struct ContentReferenceCandidate final {
    ContentDefinitionKey key;
    std::string displayName;
    std::string summary;
};

enum class AuthoredEntitySource { project, builtin };

struct AuthoredEntityCandidate final {
    ContentDefinitionKey key;
    std::string displayName;
    bool runtimeValid{};
    std::string diagnostic;
    AuthoredEntitySource source{AuthoredEntitySource::project};
};

// This index is intentionally built from the authored workspace view. It is
// useful to author maps while another, unrelated definition is still invalid;
// runtime code must continue to use ContentWorkspaceDocument::compiledRegistry.
class AuthoredEntityIndex final {
public:
    explicit AuthoredEntityIndex(const ContentWorkspaceDocument& document);

    [[nodiscard]] const std::vector<AuthoredEntityCandidate>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::vector<AuthoredEntityCandidate> candidates(
        ContentDefinitionKind kind, std::string_view query = {}) const;
    [[nodiscard]] const AuthoredEntityCandidate* find(
        const ContentDefinitionKey& key) const noexcept;

private:
    std::vector<AuthoredEntityCandidate> entries_;
};

// The picker deliberately works from the document index instead of a second
// registry. This keeps builtin and authored definitions referenceable while
// preserving the workspace's deterministic category/id order.
[[nodiscard]] std::vector<ContentReferenceCandidate> contentReferenceCandidates(
    const ContentWorkspaceDocument& document, ContentDefinitionKind kind,
    std::string_view query = {});

[[nodiscard]] std::string contentDefinitionDisplayName(
    const ContentWorkspaceDocument& document, const ContentDefinitionKey& key);
[[nodiscard]] std::string contentDefinitionSummary(
    const ContentWorkspaceDocument& document, const ContentDefinitionKey& key);

} // namespace underworld::editor
