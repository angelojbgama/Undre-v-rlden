from __future__ import annotations


TRANSLATIONS = {
    "pt-BR": {
        "app": "Dungeon Underworld — Content Studio",
        "map": "Mapa", "content": "Conteúdo", "maps": "Mapas", "layers": "Camadas",
        "entities": "Entidades", "inspector": "Inspetor", "diagnostics": "Diagnósticos",
        "new_project": "Novo Projeto", "open": "Abrir", "save": "Salvar", "save_as": "Salvar Como",
        "save_all": "Salvar Tudo", "validate": "Validar Workspace", "export": "Exportar DMAP",
        "playtest": "Playtest", "undo": "Desfazer", "redo": "Refazer", "grid": "Grade",
        "select": "Selecionar", "pencil": "Lápis", "erase": "Apagar", "rectangle": "Retângulo",
        "fill": "Preencher", "collision": "Colisão", "entity": "Entidade", "region": "Região",
        "search": "Procurar", "project": "Projeto", "builtin": "Engine / Builtin",
        "create": "Criar", "delete": "Excluir", "duplicate": "Duplicar", "add": "Adicionar",
        "remove": "Remover", "rename": "Renomear", "entry_map": "Mapa de Entrada",
        "untitled": "Sem título", "no_selection": "Nenhuma seleção", "invalid": "Inválido",
        "file": "Arquivo", "edit": "Editar", "view": "Exibir", "tools": "Ferramentas",
        "new_content": "Novo Workspace de Conteúdo", "open_project": "Abrir Projeto...",
        "open_content": "Abrir Conteúdo", "quit": "Sair", "frame": "Enquadrar Mapa",
        "language": "Idioma", "definitions": "Definições", "assets": "Assets",
        "semantics_stamps": "Semântica / Stamps", "scenes": "Cenas", "rules_links": "Regras / Links",
        "tools_select": "Selecionar", "tools_pencil": "Lápis", "tools_erase": "Apagar",
        "tools_rectangle": "Retângulo", "tools_fill": "Preencher", "tools_eyedropper": "Escolher Tile",
        "tools_tile_selection": "Selecionar Tiles", "tools_collision": "Colisão +",
        "tools_collision_erase": "Colisão -", "tools_collision_rectangle": "Colisão Ret +",
        "tools_collision_rectangle_erase": "Colisão Ret -", "tools_collision_fill": "Colisão Fill +",
        "tools_collision_fill_erase": "Colisão Fill -", "tools_entity": "Entidade",
        "tools_spawn": "Player Spawn", "tools_link": "Link de Mapa", "tools_region": "Região",
        "tools_stamp": "Stamp", "tools_pan": "Pan", "ready": "Pronto",
    },
    "en-US": {
        "app": "Dungeon Underworld — Content Studio", "map": "Map", "content": "Content",
        "maps": "Maps", "layers": "Layers", "entities": "Entities", "inspector": "Inspector",
        "diagnostics": "Diagnostics", "new_project": "New Project", "open": "Open", "save": "Save",
        "save_as": "Save As", "save_all": "Save All", "validate": "Validate Workspace",
        "export": "Export DMAP", "playtest": "Playtest", "undo": "Undo", "redo": "Redo", "grid": "Grid",
        "select": "Select", "pencil": "Pencil", "erase": "Erase", "rectangle": "Rectangle",
        "fill": "Fill", "collision": "Collision", "entity": "Entity", "region": "Region",
        "search": "Search", "project": "Project", "builtin": "Engine / Builtin", "create": "Create",
        "delete": "Delete", "duplicate": "Duplicate", "add": "Add", "remove": "Remove",
        "rename": "Rename", "entry_map": "Entry Map", "untitled": "Untitled", "no_selection": "No selection",
        "invalid": "Invalid",
        "file": "File", "edit": "Edit", "view": "View", "tools": "Tools",
        "new_content": "New Content Workspace", "open_project": "Open Project...",
        "open_content": "Open Content", "quit": "Exit", "frame": "Frame Map",
        "language": "Language", "definitions": "Definitions", "assets": "Assets",
        "semantics_stamps": "Semantics / Stamps", "scenes": "Scenes", "rules_links": "Rules / Links",
        "tools_select": "Select", "tools_pencil": "Pencil", "tools_erase": "Erase",
        "tools_rectangle": "Rectangle", "tools_fill": "Fill", "tools_eyedropper": "Pick Tile",
        "tools_tile_selection": "Select Tiles", "tools_collision": "Collision +",
        "tools_collision_erase": "Collision -", "tools_collision_rectangle": "Collision Rect +",
        "tools_collision_rectangle_erase": "Collision Rect -", "tools_collision_fill": "Collision Fill +",
        "tools_collision_fill_erase": "Collision Fill -", "tools_entity": "Entity",
        "tools_spawn": "Player Spawn", "tools_link": "Map Link", "tools_region": "Region",
        "tools_stamp": "Stamp", "tools_pan": "Pan", "ready": "Ready",
    },
}


class Translator:
    def __init__(self, language: str = "pt-BR") -> None:
        self.language = language if language in TRANSLATIONS else "pt-BR"

    def set_language(self, language: str) -> None:
        if language not in TRANSLATIONS:
            raise ValueError(language)
        self.language = language

    def __call__(self, key: str) -> str:
        return TRANSLATIONS[self.language].get(key, key)
