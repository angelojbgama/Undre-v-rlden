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
