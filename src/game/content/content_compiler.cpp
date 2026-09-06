#include "game/content/content_compiler.h"

#include <exception>

namespace underworld::game::content {

ContentCompileResult ContentCompiler::compile(const AuthoredContentPack& authored) const {
    ContentCompileResult result;
    result.report = ContentValidator{}.validate(authored);
    if (result.report.hasErrors()) return result;

    try {
        AuthoredContentPack copy = authored;
        GameContentRegistry registry;
        registry.addCompiled(std::move(copy));
        result.registry.emplace(std::move(registry));
    } catch (const std::exception& exception) {
        result.report.diagnostics.push_back({ContentDiagnosticSeverity::error,
            "catalog_rejected", exception.what(), ContentKind::tileset, {}, "registry"});
        result.registry.reset();
    }
    return result;
}

ContentCompileResult compileContent(const AuthoredContentPack& authored) {
    return ContentCompiler{}.compile(authored);
}

} // namespace underworld::game::content
