#include "game/content/content_source.h"

#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"

#include <sstream>
#include <utility>

namespace underworld::game::content {
namespace {

void addCompileDiagnostics(ContentSourceLoadResult& result,
                           const ContentValidationReport& report) {
    for (const auto& diagnostic : report.diagnostics) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::compile, {}, {}, 0, 0,
                                      diagnostic.field, diagnostic.code, {}, {},
                                      diagnostic.message});
    }
}

} // namespace

ContentSourceLoadResult loadContentSource(const ContentSourceSelection& selection) {
    if (selection.kind == ContentSourceKind::workspaceDirectory) {
        const auto loaded = loadContentWorkspaceDirectory(selection.workspaceRoot);
        if (!loaded.workspace) return {std::nullopt, loaded.diagnostics};
        return {LoadedContentBundle{ContentSourceKind::workspaceDirectory,
                                    selection.workspaceRoot.lexically_normal(),
                                    loaded.sourceFileCount,
                                    std::move(loaded.workspace->authored),
                                    std::move(loaded.workspace->registry),
                                    std::move(loaded.workspace->sources)},
                {}};
    }

    const auto authored = makeBuiltinAuthoredContent();
    const auto compiled = compileContent(authored);
    if (!compiled.registry) {
        ContentSourceLoadResult result;
        addCompileDiagnostics(result, compiled.report);
        return result;
    }
    return {LoadedContentBundle{ContentSourceKind::builtin, {}, 0, authored,
                                std::move(*compiled.registry), {}}, {}};
}

std::string formatContentWorkspaceDiagnostic(
    const ContentWorkspaceDiagnostic& diagnostic) {
    std::ostringstream output;
    if (!diagnostic.sourcePath.empty()) {
        output << diagnostic.sourcePath.generic_string();
        if (diagnostic.line != 0) output << ':' << diagnostic.line << ':' << diagnostic.column;
        output << ' ';
    }
    output << '[' << diagnostic.code << ']';
    if (!diagnostic.jsonPath.empty()) output << ' ' << diagnostic.jsonPath << ':';
    output << ' ' << diagnostic.message;
    if (!diagnostic.relatedSourcePath.empty()) {
        output << " (related: " << diagnostic.relatedSourcePath.generic_string() << ')';
    }
    return output.str();
}

} // namespace underworld::game::content
