from __future__ import annotations


TRANSLATIONS = {
    "pt-BR": {
        "app": "Dungeon Underworld — Content Studio",
        "map": "Mapa", "maps_mode": "Mapas", "content": "Conteúdo", "content_mode": "Conteúdos", "maps": "Mapas", "layers": "Camadas", "tiles": "Tiles",
        "entities": "Entidades", "spritesheets_animations": "Spritesheet / Animação", "objects_tab": "Objeto", "players_tab": "Player", "inspector": "Inspetor", "diagnostics": "Diagnósticos",
        "new_project": "Novo Projeto", "open": "Abrir", "save": "Salvar", "save_as": "Salvar Como",
        "save_all": "Salvar Tudo", "validate": "Validar Workspace", "export": "Exportar DMAP",
        "playtest": "Playtest", "undo": "Desfazer", "redo": "Refazer", "grid": "Grade",
        "select": "Selecionar", "selection_deleted": "Seleção excluída", "pencil": "Lápis", "erase": "Apagar", "rectangle": "Retângulo",
        "fill": "Preencher", "collision": "Colisão", "entity": "Entidade", "region": "Região",
        "search": "Procurar", "project": "Projeto", "builtin": "Engine / Builtin",
        "create": "Criar", "delete": "Excluir", "duplicate": "Duplicar", "add": "Adicionar",
        "remove": "Remover", "rename": "Renomear", "entry_map": "Mapa de Entrada",
        "map_create": "Criar mapa", "map_edit_properties": "Propriedades do mapa", "map_name_id": "Nome / Map ID",
        "map_width_tiles": "Largura (tiles)", "map_height_tiles": "Altura (tiles)", "map_create_player_spawn": "Criar Player Spawn inicial",
        "map_folder": "Pasta", "map_no_folder": "Sem pasta",
        "map_new": "Novo mapa", "map_import": "Importar UMAP", "map_remove": "Excluir mapa", "map_set_entry": "Definir como entrada", "map_edit_properties_action": "Editar propriedades do mapa...",
        "map_resize_hint": "Ao redimensionar, tiles e colisões da área superior esquerda são preservados.", "map_id_required": "Informe o nome / Map ID.",
        "player_start_moved": "Player Start movido",
        "untitled": "Sem título", "no_selection": "Nenhuma seleção", "invalid": "Inválido",
        "file": "Arquivo", "edit": "Editar", "view": "Exibir", "tools": "Ferramentas",
        "new_content": "Novo Workspace de Conteúdo", "open_project": "Abrir Projeto...",
        "open_content": "Abrir Conteúdo", "quit": "Sair", "frame": "Enquadrar Mapa",
        "language": "Idioma", "definitions": "Definições", "assets": "Assets",
        "theme": "Tema", "theme_system": "Seguir o sistema", "theme_light": "Claro", "theme_dark": "Escuro",
        "semantics_stamps": "Semântica / Stamps", "map_elements": "Elementos do Mapa", "scenes": "Cenas", "rules_links": "Regras / Links",
        "tools_select": "Selecionar", "tools_pencil": "Lápis", "tools_erase": "Apagar",
        "tools_rectangle": "Retângulo", "tools_fill": "Preencher", "tools_eyedropper": "Escolher Tile",
        "tools_tile_selection": "Selecionar Tiles", "tools_collision": "Colisão +",
        "tools_collision_erase": "Colisão -", "tools_collision_rectangle": "Colisão Ret +",
        "tools_collision_rectangle_erase": "Colisão Ret -", "tools_collision_fill": "Colisão Fill +",
        "tools_collision_fill_erase": "Colisão Fill -", "tools_entity": "Entidade",
        "tools_spawn": "Player Spawn", "tools_link": "Link de Mapa", "tools_region": "Região",
        "tools_stamp": "Stamp", "tools_pan": "Pan", "ready": "Pronto",
        "snap": "Snap", "overlays": "Sobreposições", "playtest_toolbar": "Playtest",
        "player_spawn": "Player Spawn", "map_transition": "Transição de Mapa", "region_element": "Região / Trigger",
        "map_elements_hint": "Arraste um elemento para o mapa", "tileset_import": "Importar Tileset", "import_tileset": "Importar Tileset...",
        "browse": "Procurar", "source_image": "Imagem fonte", "tileset_id": "ID do Tileset",
        "display_name": "Nome exibido", "tile_width": "Largura do tile", "tile_height": "Altura do tile",
        "spacing": "Espaçamento", "margin": "Margem", "copy_to_workspace": "Copiar para o workspace",
        "no_image": "Nenhuma imagem", "image_unavailable": "Imagem indisponível", "invalid_image": "Imagem inválida",
        "grid_summary": "{width} × {height} px — {columns} × {rows} frames ({frames} no total)", "grid_unused_edge": "Borda fora dos frames: {width} px à direita e {height} px abaixo. Essa área não será importada.",
        "frames_preview": "Prévia da divisão em frames", "frame_preview_help": "Cada linha azul fina marca somente o corte onde começa a próxima coluna ou linha de frames. A linha é apenas uma guia da prévia e não altera a imagem importada.", "frame_bounds": "Exemplo: o frame 0, no canto superior esquerdo, usa X {left} a {right} e Y {top} a {bottom}, totalizando {width} × {height} px.",
        "spritesheet_import": "Importar spritesheet...", "image_id": "ID da imagem", "animation_id": "ID da animação", "frame_width": "Largura do frame", "frame_height": "Altura do frame", "frame_duration": "Duração por frame (ticks)", "animation_preview": "Como ficará a animação", "play_animation": "Play", "pause_animation": "Pause", "no_animations": "Nenhuma animação importada", "animation_imported": "Spritesheet e animação importados", "edit_animation_frames": "Editar frames...",
        "delete_animation": "Excluir animação...",
        "delete_animation_confirm": "Excluir a animação \"{id}\"? O PNG original não é apagado.",
        "animation_deleted": "Animação excluída",
        "animation_in_use": "Não é possível excluir: em uso por {ids}. Remova as referências primeiro.", "edit_spritesheet_import": "Editar importação...", "animation_frame": "Frame", "frame_source_x": "Recorte X", "frame_source_y": "Recorte Y", "frame_source_width": "Largura do recorte", "frame_source_height": "Altura do recorte", "frame_source_context": "Spritesheet completo e recortes", "frame_result_preview": "Resultado do frame", "frame_offset_x": "Deslocamento visual X", "frame_offset_y": "Deslocamento visual Y", "frame_anchor_x": "Âncora X / origem", "frame_anchor_y": "Âncora Y / pés", "frame_reference_animation": "Referência transparente", "frame_reference_none": "Sem referência", "frame_anchor_from_reference": "Alinhar âncora aos pés da referência", "apply_anchor_all_frames": "Aplicar âncora a todos os frames", "center_current_frame": "Centralizar este frame", "center_all_frames": "Centralizar todos", "reset_current_frame": "Zerar deslocamento deste frame", "reset_all_frames": "Zerar todos os deslocamentos", "reset_frame_source": "Restaurar recorte deste frame", "frame_alignment_help": "A visualização superior mantém a proporção do spritesheet. Arraste o retângulo azul para corrigir o recorte quando o desenho invade outro frame, ou ajuste X, Y, largura e altura. Na visualização inferior, arraste o conteúdo para alterar somente sua posição visual. Os recortes podem se sobrepor e o PNG original não será alterado.", "animation_frames_updated": "Alinhamento dos frames atualizado", "spritesheet_import_updated": "Configuração de importação atualizada",
        "frame_canvas_width": "Largura do espaço de cada frame", "frame_canvas_height": "Altura do espaço de cada frame", "move_all_frames": "Mover todos os frames juntos", "frame_canvas_help": "Aumente este espaço para criar uma célula transparente maior ao redor de cada desenho. Ao salvar, o Studio gera outro PNG corrigido e mantém o original intacto.", "frame_does_not_fit_canvas": "O frame {frame} ainda está fora do espaço definido. Aumente a largura/altura do espaço ou aproxime o desenho do centro.", "frame_canvas_too_large": "O spritesheet corrigido ficaria grande demais. Reduza o espaço de cada frame.",
        "damage_frame": "Frame recebendo dano", "damage_frame_ticks": "Duração do dano (ticks)", "destruction_frame_ticks": "Ticks por frame da quebra", "destructible_frames_help": "Todos os estados usam o mesmo spritesheet. Escolha um frame para o objeto parado, um para o dano e a faixa que será reproduzida ao quebrar. O último frame da quebra permanece como imagem final.",
        "search_objects": "Procurar objetos", "create_object": "Criar objeto...", "configure_object": "Configurar objeto...", "place_object": "Colocar no mapa", "object_name": "Nome", "object_id": "ID do objeto", "object_animation": "Spritesheet / animação principal", "object_type": "Comportamento", "object_preset_scenery": "Cenário", "object_preset_interactable": "Interagível simples", "object_preset_destructible": "Destrutível", "object_preset_container": "Baú / contêiner", "object_preset_door": "Porta", "scenery_configuration": "Configuração do cenário", "scenery_loop": "Repetir animação em loop", "scenery_loop_help": "Use para fogo, água, brilho e outros cenários que devem continuar animados durante o jogo.", "destructible_configuration": "Configuração do destrutível", "maximum_health": "Vida do objeto", "destructible_idle_frame": "Frame parado / intacto", "destruction_start_frame": "Primeiro frame da quebra", "destruction_end_frame": "Último frame da quebra", "damage_animation": "Animação ao receber dano (opcional)", "damage_duration": "Duração do dano (ticks)", "destroying_animation": "Outra animação de quebra (opcional)", "destruction_duration": "Duração da quebra (ticks)", "destroyed_animation": "Imagem depois de quebrado (opcional)", "no_state_animation": "Nenhuma", "destructible_help": "Use um único spritesheet com o objeto intacto e sua quebra: normalmente o frame 0 fica parado e os frames 1 até o último formam a destruição. Uma animação de quebra escolhida separadamente substitui essa faixa. Ataques não fatais podem usar a animação de dano opcional.", "destructible_preview_attack": "Vida {current}/{maximum} — clique no sprite para simular um ataque.", "destructible_preview_damaged": "Dano recebido — vida {current}/{maximum}.", "destructible_preview_breaking": "Vida 0/{maximum} — quebrando...", "destructible_preview_destroyed": "Objeto quebrado. Clique novamente para reiniciar a simulação.", "chest_configuration": "Configuração do baú", "container_capacity": "Capacidade (slots)", "closed_frame": "Frame fechado", "opening_start_frame": "Primeiro frame ao abrir", "opening_end_frame": "Último frame ao abrir", "chest_animation_help": "Enquanto estiver fechado, somente o frame fechado será mostrado. Ao interagir, os frames de abertura serão tocados uma vez e o objeto ficará travado no último frame.", "click_object_preview": "Clique no sprite para simular a ação do jogador.", "object_preview_looping": "Animação em loop — será repetida continuamente no jogo.", "object_preview_opening": "Abrindo...", "object_preview_opened": "Aberto — o último frame permanece visível.", "chest_contents_help": "Itens: coloque o baú, selecione-o no mapa e use initialContents > Adicionar no inspetor da direita.", "object_creation_help": "O comportamento cria somente capacidades suportadas pelo runtime. Em baús, os itens são definidos em Conteúdo inicial depois que cada instância é colocada no mapa.", "destructible_reward_profile": "Perfil de drop", "destructible_reward_none": "Sem drop", "destructible_reward_help": "O perfil de drop usa o sistema de recompensas existente para definir probabilidades e quantidades de pickups, como coração ou ouro. Sem perfil, o objeto não dropa nada.", "leave_destroyed_residue": "Manter resto depois de destruir", "object_collision_group": "Colisão", "object_collision_enabled": "Possui colisão", "edit_collision_mask": "Editar máscara de colisão...", "object_collision_help": "A máscara é autorada por pixel, mas o runtime a converte em retângulos compactos. Colisão, interação e hurtbox permanecem independentes.", "mask_editor_title": "Editor de Máscara / Shape", "mask_tool": "Ferramenta", "mask_tool_brush": "Pincel", "mask_tool_erase": "Borracha", "mask_tool_rectangle": "Retângulo", "mask_generate_alpha": "Gerar pelo Alpha", "mask_fill_all": "Preencher Tudo", "mask_clear_all": "Limpar", "mask_zoom": "Zoom", "mask_show_sprite": "Mostrar sprite", "mask_show_mask": "Mostrar máscara", "mask_show_anchor": "Mostrar anchor", "mask_origin": "Origem: {x}, {y}", "object_needs_animation": "Importe pelo menos uma animação antes de criar um objeto.", "object_created": "Objeto criado", "object_configured": "Objeto configurado", "object_contents_updated": "Conteúdo inicial do objeto atualizado", "preview_interaction": "Prévia da interação: abrir", "no_objects": "Nenhum objeto criado",
        "search_players": "Procurar players", "create_player": "Adicionar Player...", "configure_player": "Configurar Player...", "player_identity": "Identidade do Player", "player_name": "Nome", "player_id": "ID do Player", "player_progression": "Progressão", "player_idle": "Parado / Idle", "player_walk": "Andando / Walk", "player_hurt": "Recebendo dano / Hurt", "player_sword": "Ataque de espada", "player_bow": "Ataque de arco", "player_direction_down": "Baixo", "player_direction_up": "Cima", "player_direction_side": "Lateral", "player_animation_none": "Nenhuma", "player_preview_action": "Ação", "player_preview_direction": "Direção", "player_visual_help": "Escolha uma animação para baixo, cima e lateral. Nesta primeira etapa, o lateral continua usando o flip existente para a direção oposta. A colisão autorável pelo mesmo editor de máscara dos objetos entra na Parte 2.", "player_library_help": "Players são conteúdo autorável. Esta etapa define identidade, progressão e animações; o runtime continua usando o Player atual até a futura integração.", "player_needs_animations": "Importe ou crie animações antes de adicionar um Player.", "player_created": "Player criado", "player_configured": "Player configurado", "player_deleted": "Player excluído", "player_delete_confirm": "Excluir o Player {player} e sua definição visual?",
        "tilesets": "Tilesets", "search_tilesets": "Procurar tilesets", "add_files": "Adicionar arquivos...",
        "add_folder": "Importar pasta...", "new_tileset_folder": "Nova pasta...", "reimport": "Reimportar", "reimported": "Tileset reimportado",
        "conflict": "Conflito", "skip": "Ignorar", "change_id": "Mudar ID", "incompatible": "incompatível",
        "tile_size": "Tamanho do tile", "default_tile_size": "Tamanho padrão", "apply_to_all": "Aplicar a todos", "tileset_grid": "Grade resultante",
        "manage_tileset": "Gerenciar tileset...", "tileset_properties_saved": "Propriedades do tileset atualizadas",
        "tileset_folder": "Pasta de tilesets", "tileset_no_folder": "Sem pasta", "tileset_folder_memberships": "Pastas organizacionais", "tileset_folder_memberships_hint": "Um tileset pode pertencer a mais de uma pasta.", "add_to_tileset_folder": "Adicionar à pasta", "remove_from_tileset_folder": "Remover desta pasta", "rename_tileset_folder": "Renomear pasta...", "delete_tileset_folder": "Excluir pasta", "tileset_folder_name": "Nome da pasta", "tileset_folder_exists": "Já existe uma pasta com esse nome.", "tileset_folder_delete_confirm": "Excluir a pasta {folder}? Os tilesets não serão excluídos.",
        "scan_subfolders": "Incluir subpastas", "batch_tileset_import": "Importar múltiplos tilesets",
        "tileset_asset_root_note": "Imagens externas serão copiadas para assets/tilesets.",
        "tileset_asset_root_required": "A pasta assets do projeto não está disponível.",
        "smart_terrain": "Smart Terrain", "terrain_family": "Família de terreno", "terrain_role": "Papel", "terrain_family_cards": "Famílias — passe o mouse para pré-visualizar o conjunto",
        "terrain_family_floor_behavior": "Piso", "terrain_family_wall_behavior": "Parede",
        "terrain_family_mixed_behavior": "Piso + parede",
        "terrain_family_patterns": "Padrões da família (clique para posicionar o stamp inteiro)",
        "terrain_pattern_active": "Padrão Smart Terrain ativo: {name}",
        "terrain_seed": "Seed determinística", "floor": "Floor", "wall": "Wall", "room_brush": "Room / Area Brush",
        "terrain_active": "Terrain ativo: {family} / {role}", "terrain_active_walkable": "Terrain ativo: {family} — sem colisão", "terrain_active_solid": "Terrain ativo: {family} — com colisão", "no_terrain_family": "Nenhuma família semântica disponível",
        "semantic_editor": "Editor de Tile Semantic", "semantic_id": "ID semântico", "tile_name_id": "Nome / ID do tile", "family": "Família",
        "role": "Papel", "topology": "Topologia", "preferred_layer": "Camada preferida", "north": "Norte",
        "east": "Leste", "south": "Sul", "west": "Oeste", "flip_x_allowed": "Permitir flip X",
        "save_semantic": "Salvar semântica", "edit_tile_semantic": "Editar tile", "edit_tile_name_family": "Editar nome e família do tile...",
        "define_tile_pixel_collision": "Definir colis\u00e3o por pixel...", "edit_tile_pixel_collision": "Editar colis\u00e3o por pixel...", "remove_tile_pixel_collision": "Remover colis\u00e3o por pixel",
        "tile_collision_editor_title": "Colis\u00e3o por Pixel do Tile", "tile_collision_saved": "Colis\u00e3o por pixel do tile salva", "tile_collision_removed": "Colis\u00e3o por pixel do tile removida",
        "room_preview": "Pré-visualização da sala", "terrain_rectangle_preview": "Pré-visualização do retângulo de terreno", "erase_rectangle_preview": "Arraste para selecionar a área que será apagada",
        "semantic_saved": "Tile semantic salvo",
        "terrain_missing_candidate": "Nenhum tile semântico compatível foi encontrado",
        "configure_terrain_rule": "Gerenciar lógica Smart Terrain...", "terrain_rule_editor": "Gerenciador de Smart Terrain",
        "terrain_rule_family": "Família da regra", "terrain_rule_role": "Papel da regra", "terrain_rule_collision": "Colisão da família", "terrain_rule_slots": "Slots visuais 3 × 3",
        "terrain_rule_slots_hint": "Selecione uma posição da grade e clique no tile correspondente do atlas. A posição define a vizinhança usada pelo autotile.",
        "terrain_rule_click_atlas": "selecione um tile no atlas", "terrain_rule_save": "Salvar lógica", "terrain_rule_saved": "Lógica de Smart Terrain salva",
        "terrain_rule_existing": "Lógica existente", "terrain_rule_none": "Nova lógica", "terrain_rule_new": "Nova",
        "terrain_rule_delete": "Excluir lógica",
        "terrain_rule_delete_confirm": "Excluir a lógica {family} / {role}? As definições semânticas geradas serão removidas.",
        "terrain_rule_variants": "Variações do piso",
        "terrain_rule_floor_hint": "Clique em um tile do atlas para adicioná-lo como variação 1 × 1 e ajuste o peso relativo. Exemplo: liso 8 + rachado 2 = aproximadamente 80% / 20%. A lista não é limitada a nove variações.",
        "terrain_rule_weight": "Frequência da variação", "terrain_rule_weight_suffix": " de peso",
        "terrain_rule_clear_slot": "Remover variação", "terrain_rule_variant_tooltip": "Tile #{index} — peso {weight}, aproximadamente {percent}%",
        "terrain_rule_variant_item": "Tile #{index} — peso {weight} ({percent}%)",
        "terrain_rule_variant_empty": "Nenhuma variação ainda. Selecione um tile no atlas e clique em “Adicionar variante”.",
        "terrain_rule_add_variant": "Adicionar variante",
        "terrain_rule_tab_variants": "Variantes", "terrain_rule_tab_boundary": "Conectividade", "terrain_rule_tab_patterns": "Padrões",
        "terrain_rule_patterns_hint": "Estes stamps têm todas as células classificadas nesta família e são aceitos como padrões NxM atômicos do Smart Terrain.",
        "terrain_rule_patterns_empty": "Nenhum stamp desta família ainda. Crie stamps cujas células apontem para semantics desta família para usá-los como padrões.",
        "terrain_rule_pattern_tooltip": "Padrão {name} — {width}×{height}, {count} células",
        "terrain_collision": "Colisão ao pintar", "preserve_collision": "Preservar colisão atual", "collision_on": "Ativar colisão", "collision_off": "Desativar colisão",
    },
    "en-US": {
        "app": "Dungeon Underworld — Content Studio", "map": "Map", "content": "Content",
        "maps_mode": "Maps", "content_mode": "Content", "maps": "Maps", "layers": "Layers", "tiles": "Tiles", "entities": "Entities", "spritesheets_animations": "Spritesheet / Animation", "objects_tab": "Object", "players_tab": "Player", "inspector": "Inspector",
        "diagnostics": "Diagnostics", "new_project": "New Project", "open": "Open", "save": "Save",
        "save_as": "Save As", "save_all": "Save All", "validate": "Validate Workspace",
        "export": "Export DMAP", "playtest": "Playtest", "undo": "Undo", "redo": "Redo", "grid": "Grid",
        "select": "Select", "selection_deleted": "Selection deleted", "pencil": "Pencil", "erase": "Erase", "rectangle": "Rectangle",
        "fill": "Fill", "collision": "Collision", "entity": "Entity", "region": "Region",
        "search": "Search", "project": "Project", "builtin": "Engine / Builtin", "create": "Create",
        "delete": "Delete", "duplicate": "Duplicate", "add": "Add", "remove": "Remove",
        "rename": "Rename", "entry_map": "Entry Map", "untitled": "Untitled", "no_selection": "No selection",
        "map_create": "Create map", "map_edit_properties": "Map properties", "map_name_id": "Name / Map ID",
        "map_width_tiles": "Width (tiles)", "map_height_tiles": "Height (tiles)", "map_create_player_spawn": "Create initial Player Spawn",
        "map_folder": "Folder", "map_no_folder": "No folder",
        "map_new": "New map", "map_import": "Import UMAP", "map_remove": "Remove map", "map_set_entry": "Set as entry", "map_edit_properties_action": "Edit map properties...",
        "map_resize_hint": "When resizing, tiles and collision in the upper-left area are preserved.", "map_id_required": "Enter the name / Map ID.",
        "player_start_moved": "Player Start moved",
        "invalid": "Invalid",
        "file": "File", "edit": "Edit", "view": "View", "tools": "Tools",
        "new_content": "New Content Workspace", "open_project": "Open Project...",
        "open_content": "Open Content", "quit": "Exit", "frame": "Frame Map",
        "language": "Language", "definitions": "Definitions", "assets": "Assets",
        "theme": "Theme", "theme_system": "Follow System", "theme_light": "Light", "theme_dark": "Dark",
        "semantics_stamps": "Semantics / Stamps", "map_elements": "Map Elements", "scenes": "Scenes", "rules_links": "Rules / Links",
        "tools_select": "Select", "tools_pencil": "Pencil", "tools_erase": "Erase",
        "tools_rectangle": "Rectangle", "tools_fill": "Fill", "tools_eyedropper": "Pick Tile",
        "tools_tile_selection": "Select Tiles", "tools_collision": "Collision +",
        "tools_collision_erase": "Collision -", "tools_collision_rectangle": "Collision Rect +",
        "tools_collision_rectangle_erase": "Collision Rect -", "tools_collision_fill": "Collision Fill +",
        "tools_collision_fill_erase": "Collision Fill -", "tools_entity": "Entity",
        "tools_spawn": "Player Spawn", "tools_link": "Map Link", "tools_region": "Region",
        "tools_stamp": "Stamp", "tools_pan": "Pan", "ready": "Ready",
        "snap": "Snap", "overlays": "Overlays", "playtest_toolbar": "Playtest",
        "player_spawn": "Player Spawn", "map_transition": "Map Transition", "region_element": "Region / Trigger",
        "map_elements_hint": "Drag an element to the map", "tileset_import": "Import Tileset", "import_tileset": "Import Tileset...",
        "browse": "Browse", "source_image": "Source image", "tileset_id": "Tileset ID",
        "display_name": "Display name", "tile_width": "Tile width", "tile_height": "Tile height",
        "spacing": "Spacing", "margin": "Margin", "copy_to_workspace": "Copy to workspace",
        "no_image": "No image", "image_unavailable": "Image unavailable", "invalid_image": "Invalid image",
        "grid_summary": "{width} × {height} px — {columns} × {rows} frames ({frames} total)", "grid_unused_edge": "Edge outside frames: {width} px on the right and {height} px below. This area will not be imported.",
        "frames_preview": "Frame division preview", "frame_preview_help": "Each thin blue line marks only the cut where the next frame column or row begins. The line is only a preview guide and does not alter the imported image.", "frame_bounds": "Example: frame 0, at the top left, uses X {left} through {right} and Y {top} through {bottom}, totaling {width} × {height} px.",
        "spritesheet_import": "Import spritesheet...", "image_id": "Image ID", "animation_id": "Animation ID", "frame_width": "Frame width", "frame_height": "Frame height", "frame_duration": "Duration per frame (ticks)", "animation_preview": "Animation result", "play_animation": "Play", "pause_animation": "Pause", "no_animations": "No imported animations", "animation_imported": "Spritesheet and animation imported", "edit_animation_frames": "Edit frames...",
        "delete_animation": "Delete animation...",
        "delete_animation_confirm": "Delete animation \"{id}\"? The original PNG is not removed.",
        "animation_deleted": "Animation deleted",
        "animation_in_use": "Cannot delete: in use by {ids}. Remove the references first.", "edit_spritesheet_import": "Edit import...", "animation_frame": "Frame", "frame_source_x": "Crop X", "frame_source_y": "Crop Y", "frame_source_width": "Crop width", "frame_source_height": "Crop height", "frame_source_context": "Full spritesheet and crops", "frame_result_preview": "Frame result", "frame_offset_x": "Visual X offset", "frame_offset_y": "Visual Y offset", "frame_anchor_x": "Anchor X / origin", "frame_anchor_y": "Anchor Y / feet", "frame_reference_animation": "Ghost reference", "frame_reference_none": "No reference", "frame_anchor_from_reference": "Align anchor to reference feet", "apply_anchor_all_frames": "Apply anchor to all frames", "center_current_frame": "Center this frame", "center_all_frames": "Center all", "reset_current_frame": "Reset this frame offset", "reset_all_frames": "Reset all offsets", "reset_frame_source": "Restore this frame crop", "frame_alignment_help": "The upper view preserves the spritesheet aspect ratio. Drag the blue rectangle to repair the crop when artwork crosses into another frame, or edit X, Y, width, and height. In the lower view, drag the content to change only its visual position. Crops may overlap and the original PNG is not modified.", "animation_frames_updated": "Frame alignment updated", "spritesheet_import_updated": "Import settings updated",
        "frame_canvas_width": "Width of each frame space", "frame_canvas_height": "Height of each frame space", "move_all_frames": "Move all frames together", "frame_canvas_help": "Increase this space to create a larger transparent cell around each drawing. On save, the Studio generates a corrected PNG and leaves the original untouched.", "frame_does_not_fit_canvas": "Frame {frame} is still outside its space. Increase the space width/height or move the artwork closer to the center.", "frame_canvas_too_large": "The corrected spritesheet would be too large. Reduce each frame's space.",
        "damage_frame": "Damage frame", "damage_frame_ticks": "Damage duration (ticks)", "destruction_frame_ticks": "Ticks per breaking frame", "destructible_frames_help": "All states use the same spritesheet. Choose one frame for the idle object, one for damage, and the range played when it breaks. The last breaking frame remains as the final image.",
        "search_objects": "Search objects", "create_object": "Create object...", "configure_object": "Configure object...", "place_object": "Place in map", "object_name": "Name", "object_id": "Object ID", "object_animation": "Main spritesheet / animation", "object_type": "Behavior", "object_preset_scenery": "Scenery", "object_preset_interactable": "Simple interactable", "object_preset_destructible": "Destructible", "object_preset_container": "Chest / container", "object_preset_door": "Door", "scenery_configuration": "Scenery configuration", "scenery_loop": "Repeat animation in a loop", "scenery_loop_help": "Use for fire, water, glow, and other scenery that should remain animated during gameplay.", "destructible_configuration": "Destructible configuration", "maximum_health": "Object health", "destructible_idle_frame": "Idle / intact frame", "destruction_start_frame": "First breaking frame", "destruction_end_frame": "Last breaking frame", "damage_animation": "Damage animation (optional)", "damage_duration": "Damage duration (ticks)", "destroying_animation": "Other breaking animation (optional)", "destruction_duration": "Breaking duration (ticks)", "destroyed_animation": "Image after breaking (optional)", "no_state_animation": "None", "destructible_help": "Use one spritesheet containing the intact object and its destruction: normally frame 0 remains idle and frames 1 through the last form the breaking sequence. A separately selected breaking animation replaces that range. Non-lethal hits may use the optional damage animation.", "destructible_preview_attack": "Health {current}/{maximum} — click the sprite to simulate an attack.", "destructible_preview_damaged": "Damage received — health {current}/{maximum}.", "destructible_preview_breaking": "Health 0/{maximum} — breaking...", "destructible_preview_destroyed": "Object destroyed. Click again to restart the simulation.", "chest_configuration": "Chest configuration", "container_capacity": "Capacity (slots)", "closed_frame": "Closed frame", "opening_start_frame": "First opening frame", "opening_end_frame": "Last opening frame", "chest_animation_help": "While closed, only the closed frame is shown. On interaction, the opening frames play once and the object remains locked on the final frame.", "click_object_preview": "Click the sprite to simulate the player action.", "object_preview_looping": "Looping animation — it will repeat continuously in the game.", "object_preview_opening": "Opening...", "object_preview_opened": "Open — the final frame remains visible.", "chest_contents_help": "Items: place the chest, select it in the map, then use initialContents > Add in the right inspector.", "object_creation_help": "The behavior creates only capabilities supported by runtime. For chests, items are set in Initial contents after each instance is placed in the map.", "destructible_reward_profile": "Drop profile", "destructible_reward_none": "No drop", "destructible_reward_help": "The drop profile reuses the existing reward system to define pickup probabilities and counts, such as hearts or gold. With no profile, the object drops nothing.", "leave_destroyed_residue": "Keep residue after destruction", "object_collision_group": "Collision", "object_collision_enabled": "Has collision", "edit_collision_mask": "Edit collision mask...", "object_collision_help": "The mask is authored per pixel, while runtime converts it to compact rectangles. Collision, interaction, and hurtbox remain independent.", "mask_editor_title": "Mask / Shape Editor", "mask_tool": "Tool", "mask_tool_brush": "Brush", "mask_tool_erase": "Eraser", "mask_tool_rectangle": "Rectangle", "mask_generate_alpha": "Generate from Alpha", "mask_fill_all": "Fill All", "mask_clear_all": "Clear", "mask_zoom": "Zoom", "mask_show_sprite": "Show sprite", "mask_show_mask": "Show mask", "mask_show_anchor": "Show anchor", "mask_origin": "Origin: {x}, {y}", "object_needs_animation": "Import at least one animation before creating an object.", "object_created": "Object created", "object_configured": "Object configured", "object_contents_updated": "Object initial contents updated", "preview_interaction": "Interaction preview: open", "no_objects": "No objects created",
        "search_players": "Search players", "create_player": "Add Player...", "configure_player": "Configure Player...", "player_identity": "Player Identity", "player_name": "Name", "player_id": "Player ID", "player_progression": "Progression", "player_idle": "Idle", "player_walk": "Walk", "player_hurt": "Hurt", "player_sword": "Sword Attack", "player_bow": "Bow Attack", "player_direction_down": "Down", "player_direction_up": "Up", "player_direction_side": "Side", "player_animation_none": "None", "player_preview_action": "Action", "player_preview_direction": "Direction", "player_visual_help": "Choose animations for down, up and side. In this first part, side keeps using the existing horizontal flip for the opposite direction. Authorable collision using the same mask editor as objects comes in Part 2.", "player_library_help": "Players are authored content. This part defines identity, progression and animations; runtime keeps using the current Player until the later integration.", "player_needs_animations": "Import or create animations before adding a Player.", "player_created": "Player created", "player_configured": "Player configured", "player_deleted": "Player deleted", "player_delete_confirm": "Delete Player {player} and its visual definition?",
        "tilesets": "Tilesets", "search_tilesets": "Search tilesets", "add_files": "Add Files...",
        "add_folder": "Import Folder...", "new_tileset_folder": "New Folder...", "reimport": "Reimport", "reimported": "Tileset reimported",
        "conflict": "Conflict", "skip": "Skip", "change_id": "Change ID", "incompatible": "incompatible",
        "tile_size": "Tile size", "default_tile_size": "Default tile size", "apply_to_all": "Apply to all", "tileset_grid": "Resulting grid",
        "manage_tileset": "Manage tileset...", "tileset_properties_saved": "Tileset properties updated",
        "tileset_folder": "Tileset folder", "tileset_no_folder": "No folder", "tileset_folder_memberships": "Organization folders", "tileset_folder_memberships_hint": "A tileset can belong to more than one folder.", "add_to_tileset_folder": "Add to folder", "remove_from_tileset_folder": "Remove from this folder", "rename_tileset_folder": "Rename folder...", "delete_tileset_folder": "Delete folder", "tileset_folder_name": "Folder name", "tileset_folder_exists": "A folder with that name already exists.", "tileset_folder_delete_confirm": "Delete folder {folder}? Its tilesets will not be deleted.",
        "scan_subfolders": "Include subfolders", "batch_tileset_import": "Import Multiple Tilesets",
        "tileset_asset_root_note": "External images will be copied into assets/tilesets.",
        "tileset_asset_root_required": "The project assets directory is unavailable.",
        "smart_terrain": "Smart Terrain", "terrain_family": "Terrain family", "terrain_role": "Role", "terrain_family_cards": "Families — hover to preview the set",
        "terrain_family_floor_behavior": "Floor", "terrain_family_wall_behavior": "Wall",
        "terrain_family_mixed_behavior": "Floor + wall",
        "terrain_family_patterns": "Family patterns (click to place the whole stamp)",
        "terrain_pattern_active": "Smart Terrain pattern active: {name}",
        "terrain_seed": "Deterministic seed", "floor": "Floor", "wall": "Wall", "room_brush": "Room / Area Brush",
        "terrain_active": "Terrain active: {family} / {role}", "terrain_active_walkable": "Active terrain: {family} — no collision", "terrain_active_solid": "Active terrain: {family} — with collision", "no_terrain_family": "No semantic family available",
        "semantic_editor": "Tile Semantic Editor", "semantic_id": "Semantic ID", "tile_name_id": "Tile name / ID", "family": "Family",
        "role": "Role", "topology": "Topology", "preferred_layer": "Preferred layer", "north": "North",
        "east": "East", "south": "South", "west": "West", "flip_x_allowed": "Allow X flip",
        "save_semantic": "Save semantic", "edit_tile_semantic": "Edit Tile", "edit_tile_name_family": "Edit tile name and family...",
        "define_tile_pixel_collision": "Define pixel collision...", "edit_tile_pixel_collision": "Edit pixel collision...", "remove_tile_pixel_collision": "Remove pixel collision",
        "tile_collision_editor_title": "Tile Pixel Collision", "tile_collision_saved": "Tile pixel collision saved", "tile_collision_removed": "Tile pixel collision removed",
        "room_preview": "Room preview", "terrain_rectangle_preview": "Terrain rectangle preview", "erase_rectangle_preview": "Drag to select the area to erase",
        "semantic_saved": "Tile semantic saved",
        "terrain_missing_candidate": "No compatible semantic tile was found",
        "configure_terrain_rule": "Manage Smart Terrain logic...", "terrain_rule_editor": "Smart Terrain Manager",
        "terrain_rule_family": "Rule family", "terrain_rule_role": "Rule role", "terrain_rule_collision": "Family collision", "terrain_rule_slots": "3 × 3 visual slots",
        "terrain_rule_slots_hint": "Select a grid position and click the corresponding atlas tile. The position defines the neighbourhood used by autotiling.",
        "terrain_rule_click_atlas": "select a tile in the atlas", "terrain_rule_save": "Save logic", "terrain_rule_saved": "Smart Terrain logic saved",
        "terrain_rule_existing": "Existing logic", "terrain_rule_none": "New logic", "terrain_rule_new": "New",
        "terrain_rule_delete": "Delete logic",
        "terrain_rule_delete_confirm": "Delete the {family} / {role} logic? Its generated semantic definitions will be removed.",
        "terrain_rule_variants": "Floor variations",
        "terrain_rule_floor_hint": "Click an atlas tile to add it as a 1 × 1 variant and adjust its relative weight. Example: smooth 8 + cracked 2 = approximately 80% / 20%. The list is not limited to nine variants.",
        "terrain_rule_weight": "Variation frequency", "terrain_rule_weight_suffix": " weight",
        "terrain_rule_clear_slot": "Remove variation", "terrain_rule_variant_tooltip": "Tile #{index} — weight {weight}, approximately {percent}%",
        "terrain_rule_variant_item": "Tile #{index} — weight {weight} ({percent}%)",
        "terrain_rule_variant_empty": "No variants yet. Select an atlas tile and click “Add variant”.",
        "terrain_rule_add_variant": "Add variant",
        "terrain_rule_tab_variants": "Variants", "terrain_rule_tab_boundary": "Connectivity", "terrain_rule_tab_patterns": "Patterns",
        "terrain_rule_patterns_hint": "These stamps have every cell classified in this family and are accepted as atomic Smart Terrain NxM patterns.",
        "terrain_rule_patterns_empty": "No stamps for this family yet. Create stamps whose cells point at semantics of this family to use them as patterns.",
        "terrain_rule_pattern_tooltip": "Pattern {name} — {width}×{height}, {count} cells",
        "terrain_collision": "Collision while painting", "preserve_collision": "Preserve current collision", "collision_on": "Enable collision", "collision_off": "Disable collision",
    },
}


class Translator:
    def __init__(self, language: str = "pt-BR") -> None:
        self.language = language if language in TRANSLATIONS else "pt-BR"

    def set_language(self, language: str) -> None:
        if language not in TRANSLATIONS:
            raise ValueError(language)
        self.language = language

    def has(self, key: str) -> bool:
        return key in TRANSLATIONS[self.language]

    def __call__(self, key: str, **values: object) -> str:
        text = TRANSLATIONS[self.language].get(key, key)
        return text.format(**values) if values else text

# depth-occlusion-localization-v1
TRANSLATIONS["pt-BR"].update({
    "object_depth_group": "Profundidade / Oclusão",
    "object_occlusion_enabled": "Possui máscara de oclusão",
    "edit_depth_occlusion": "Editar profundidade / oclusão...",
    "object_depth_help": (
        "O ponto de profundidade decide a ordem por Y e funciona mesmo sem máscara. "
        "A máscara de oclusão começa vazia: pinte somente os pixels que devem cobrir o player. "
        "Gerar pelo Alpha é apenas uma ferramenta opcional e não altera a colisão."
    ),
    "occlusion_editor_title": "Editor de Profundidade / Oclusão",
    "mask_tool_depth": "Ponto de profundidade",
})
for _english_key in ("en-US", "en"):
    if _english_key in TRANSLATIONS:
        TRANSLATIONS[_english_key].update({
            "object_depth_group": "Depth / Occlusion",
            "object_occlusion_enabled": "Has occlusion mask",
            "edit_depth_occlusion": "Edit depth / occlusion...",
            "object_depth_help": (
                "The depth point controls Y sorting even without a mask. The occlusion "
                "mask starts empty: paint only pixels that should cover the player. "
                "Generate from Alpha is optional and never changes collision."
            ),
            "occlusion_editor_title": "Depth / Occlusion Editor",
            "mask_tool_depth": "Depth point",
        })


# item-authoring-localization-v1
TRANSLATIONS["pt-BR"].update({
    "items_tab": "Itens",
    "search_items": "Procurar itens",
    "create_item": "Novo Item...",
    "configure_item": "Editar Item...",
    "delete_item": "Excluir Item",
    "place_item": "Colocar no mapa",
    "item_name": "Nome",
    "item_id": "ID do item",
    "item_visual": "Sprite / visual",
    "item_visual_none": "Nenhum visual selecionado",
    "item_choose_visual": "Selecionar...",
    "item_category": "Categoria",
    "item_stack_limit": "Stack máximo",
    "item_category_consumable": "Consumível",
    "item_category_equipment": "Equipamento",
    "item_category_key": "Chave",
    "item_category_misc": "Diversos",
    "item_creation_help": (
        "Todo Item gera automaticamente um Pickup correspondente. "
        "Equipamentos sempre usam stack 1. Itens acumuláveis começam em 66."
    ),
    "item_id_before_visual": (
        "Defina primeiro um ID no formato item.* antes de selecionar o visual."
    ),
    "item_needs_visual": (
        "Importe ou crie primeiro um StaticSprite ou uma animação."
    ),
    "item_created": "Item criado",
    "item_configured": "Item atualizado",
    "item_deleted": "Item excluído",
    "item_delete_confirm": "Excluir o Item {item} e seu Pickup gerado?",
    "item_delete_map_usage": (
        "O Item ou seu Pickup ainda é usado no(s) mapa(s): {maps}"
    ),
    "item_missing_pickup": "Pickup do Item não encontrado",
    "item_stack_summary": "Stack máximo: {count}",
    "item_pickup_summary": "Pickup: {pickup}",
    "no_items": "Nenhum Item criado",
})

TRANSLATIONS["en-US"].update({
    "items_tab": "Items",
    "search_items": "Search items",
    "create_item": "New Item...",
    "configure_item": "Edit Item...",
    "delete_item": "Delete Item",
    "place_item": "Place in map",
    "item_name": "Name",
    "item_id": "Item ID",
    "item_visual": "Sprite / visual",
    "item_visual_none": "No visual selected",
    "item_choose_visual": "Select...",
    "item_category": "Category",
    "item_stack_limit": "Maximum stack",
    "item_category_consumable": "Consumable",
    "item_category_equipment": "Equipment",
    "item_category_key": "Key",
    "item_category_misc": "Misc",
    "item_creation_help": (
        "Every Item automatically owns a matching Pickup. "
        "Equipment always uses stack 1. Stackable Items start at 66."
    ),
    "item_id_before_visual": (
        "Set an item.* ID before selecting the visual."
    ),
    "item_needs_visual": (
        "Import or create a StaticSprite or animation first."
    ),
    "item_created": "Item created",
    "item_configured": "Item updated",
    "item_deleted": "Item deleted",
    "item_delete_confirm": "Delete Item {item} and its generated Pickup?",
    "item_delete_map_usage": (
        "The Item or its Pickup is still used in map(s): {maps}"
    ),
    "item_missing_pickup": "Item Pickup was not found",
    "item_stack_summary": "Maximum stack: {count}",
    "item_pickup_summary": "Pickup: {pickup}",
    "no_items": "No Items created",
})


# door-library-localization-v1
TRANSLATIONS["pt-BR"].update({
    "doors_tab": "Portas",
    "search_doors": "Procurar portas",
    "door_place_button": "Colocar no mapa",
    "door_edit_animated_collision": "Definir colisão por frame...",
    "door_animated_collision_selected": (
        "Editor de colisão por frame: {definition_id}"
    ),
    "animated_collision_editor_title": "Colisão por frame",
    "animated_collision_animation": "Animação: {animation}",
    "animated_collision_frame": "Frame {current} de {total}",
    "animated_collision_previous": "Frame anterior",
    "animated_collision_next": "Próximo frame",
    "animated_collision_edit_frame": "Editar colisão deste frame...",
    "animated_collision_clear_frame": "Limpar colisão deste frame",
    "animated_collision_no_draft": "Nenhuma colisão definida neste frame.",
    "animated_collision_draft_summary": (
        "Colisão em memória: {cells} pixels bloqueados"
    ),
    "animated_collision_memory_only": (
        "As alterações são salvas na animação usando máscaras por frame."
    ),
    "animated_collision_persistence_help": (
        "A colisão pertence à animação do objeto. Frames sem um novo "
        "keyframe herdam o estado anterior no editor; ao salvar, o estado "
        "efetivo é materializado nas máscaras dos frames."
    ),
    "animated_collision_set_none": "Sem colisão a partir deste frame",
    "animated_collision_inherit_previous": "Herdar frame anterior",
    "animated_collision_status_defined": "Colisão definida neste frame.",
    "animated_collision_status_none": "Sem colisão.",
    "animated_collision_status_none_keyframe": (
        "Keyframe: sem colisão a partir deste frame."
    ),
    "animated_collision_status_inherited": (
        "Colisão herdada do frame {frame}."
    ),
    "animated_collision_status_inherited_none": (
        "Sem colisão, herdado do frame {frame}."
    ),
    "animated_collision_saved": (
        "Colisão por frame salva para {definition_id}."
    ),
    "animated_collision_frame_mask_title": "Colisão do frame",
    "animated_collision_animation_missing": (
        "Animação não encontrada: {animation}"
    ),
    "animated_collision_frames_missing": (
        "A animação não possui frames editáveis: {animation}"
    ),
    "animated_collision_image_missing": (
        "Imagem da animação indisponível: {animation}"
    ),
    "door_placement_active": (
        "Colocacao de porta ativa: {definition_id}. "
        "Passe o mouse sobre uma parede e clique para colocar. "
        "Escape cancela."
    ),
    "door_placed": "Porta colocada: {definition_id}",
    "door_workspace_required": (
        "A colocacao de portas requer um workspace de conteudo."
    ),
    "door_definition_invalid": (
        "Definicao de porta invalida: {definition_id}"
    ),
    "no_doors": "Nenhuma porta criada",
    "door_placement_wall": "Encaixe: parede",
    "door_span_summary": "Área de encaixe: {span} × {thickness} tiles",
    "door_anchor_summary": "Âncora de encaixe: {anchor}",
    "door_orientation_summary": "Orientação: {orientations}",
    "door_orientation_horizontal": "horizontal",
    "door_orientation_vertical": "vertical",
    "door_animation_summary": "Animação: {animation}",
    "door_library_help": (
        "Portas continuam sendo WorldObjects com capability door. "
        "Esta biblioteca mostra o encaixe na parede sem usar o pivot "
        "genérico dos objetos. A colocação no mapa usa uma operação "
        "especializada de parede."
    ),
    "door_family_filter": "Família",
    "door_family_all": "Todas as famílias",
    "door_family_uncategorized": "Sem família",
    "door_family_label": "Família: {name}",
    "door_open_attack": "Abre com ataque",
    "door_open_encounter": "Abre quando encounter completar",
    "door_open_condition_none": "Nenhuma",
    "door_open_attack_sword": "Espada do jogador",
    "door_open_attack_bow": "Arco do jogador",
})

TRANSLATIONS["en-US"].update({
    "doors_tab": "Doors",
    "search_doors": "Search doors",
    "door_place_button": "Place on map",
    "door_edit_animated_collision": "Define collision by frame...",
    "door_animated_collision_selected": (
        "Frame collision editor: {definition_id}"
    ),
    "animated_collision_editor_title": "Collision by frame",
    "animated_collision_animation": "Animation: {animation}",
    "animated_collision_frame": "Frame {current} of {total}",
    "animated_collision_previous": "Previous frame",
    "animated_collision_next": "Next frame",
    "animated_collision_edit_frame": "Edit collision for this frame...",
    "animated_collision_clear_frame": "Clear collision for this frame",
    "animated_collision_no_draft": "No collision defined for this frame.",
    "animated_collision_draft_summary": (
        "In-memory collision: {cells} blocked pixels"
    ),
    "animated_collision_memory_only": (
        "Changes are saved on the animation using per-frame masks."
    ),
    "animated_collision_persistence_help": (
        "Collision belongs to the object's animation. Frames without a new "
        "keyframe inherit the previous state in the editor; on save, the "
        "effective state is materialized into frame masks."
    ),
    "animated_collision_set_none": "No collision from this frame",
    "animated_collision_inherit_previous": "Inherit previous frame",
    "animated_collision_status_defined": "Collision defined on this frame.",
    "animated_collision_status_none": "No collision.",
    "animated_collision_status_none_keyframe": (
        "Keyframe: no collision from this frame."
    ),
    "animated_collision_status_inherited": (
        "Collision inherited from frame {frame}."
    ),
    "animated_collision_status_inherited_none": (
        "No collision, inherited from frame {frame}."
    ),
    "animated_collision_saved": (
        "Frame collision saved for {definition_id}."
    ),
    "animated_collision_frame_mask_title": "Frame collision",
    "animated_collision_animation_missing": (
        "Animation not found: {animation}"
    ),
    "animated_collision_frames_missing": (
        "Animation has no editable frames: {animation}"
    ),
    "animated_collision_image_missing": (
        "Animation image unavailable: {animation}"
    ),
    "door_placement_active": (
        "Door placement active: {definition_id}. "
        "Move over a wall and click to place. "
        "Escape cancels."
    ),
    "door_placed": "Door placed: {definition_id}",
    "door_workspace_required": (
        "Door placement requires a content workspace."
    ),
    "door_definition_invalid": (
        "Invalid door definition: {definition_id}"
    ),
    "no_doors": "No doors created",
    "door_placement_wall": "Placement: wall",
    "door_span_summary": "Placement footprint: {span} × {thickness} tiles",
    "door_anchor_summary": "Placement anchor: {anchor}",
    "door_orientation_summary": "Orientation: {orientations}",
    "door_orientation_horizontal": "horizontal",
    "door_orientation_vertical": "vertical",
    "door_animation_summary": "Animation: {animation}",
    "door_library_help": (
        "Doors remain WorldObjects with the door capability. "
        "This library exposes wall placement metadata instead of using "
        "the generic object pivot. Map placement uses a specialized "
        "wall operation."
    ),
    "door_family_filter": "Family",
    "door_family_all": "All families",
    "door_family_uncategorized": "No family",
    "door_family_label": "Family: {name}",
    "door_open_attack": "Opens with attack",
    "door_open_encounter": "Opens when encounter completes",
    "door_open_condition_none": "None",
    "door_open_attack_sword": "Player sword",
    "door_open_attack_bow": "Player bow",
})


# door-instance-editor-localization-v1
TRANSLATIONS["pt-BR"].update({
    "door_instance_title": "Configuração da porta",
    "door_instance_mode": "Comportamento",
    "door_instance_defaults": "Usar padrão da definição",
    "door_instance_custom": "Personalizar esta porta",
    "door_initial_state": "Estado inicial",
    "door_state_closed": "Fechada",
    "door_state_locked": "Trancada",
    "door_state_open": "Aberta",
    "door_required_key": "Chave necessária",
    "door_no_required_key": "Sem chave / desbloqueio por script",
    "door_consume_key": "Consumir chave",
    "door_persistence": "Persistência",
    "door_persistence_persistent": "Persistente",
    "door_persistence_reset": "Reiniciar ao entrar no mapa",
    "door_apply_configuration": "Aplicar configuração",
    "door_configuration_saved": "Configuração da porta atualizada",
    "door_instance_help": (
        "A porta continua sendo um WorldObject. "
        "Personalize somente o comportamento desta instância. "
        "Uma porta trancada sem chave pode ser liberada por script."
    ),
})

TRANSLATIONS["en-US"].update({
    "door_instance_title": "Door configuration",
    "door_instance_mode": "Behavior",
    "door_instance_defaults": "Use definition defaults",
    "door_instance_custom": "Customize this door",
    "door_initial_state": "Initial state",
    "door_state_closed": "Closed",
    "door_state_locked": "Locked",
    "door_state_open": "Open",
    "door_required_key": "Required key",
    "door_no_required_key": "No key / script unlock",
    "door_consume_key": "Consume key",
    "door_persistence": "Persistence",
    "door_persistence_persistent": "Persistent",
    "door_persistence_reset": "Reset on map enter",
    "door_apply_configuration": "Apply configuration",
    "door_configuration_saved": "Door configuration updated",
    "door_instance_help": (
        "The door remains a WorldObject. "
        "Customize only this placement behavior. "
        "A locked door without a key can still be unlocked by script."
    ),
})

# object-transition-editor-localization-v1
TRANSLATIONS["pt-BR"].update({
    "object_transition_title": "Transição",
    "object_transition_enabled": "Ativar transição neste objeto",
    "object_transition_target_map": "Mapa de destino",
    "object_transition_target_spawn": "Spawn de destino",
    "object_transition_apply": "Aplicar transição",
    "object_transition_help": (
        "Transição é uma capacidade genérica da instância. "
        "Ela pode ser usada por portas, portais, escadas e outros objetos."
    ),
    "object_transition_saved": "Transição do objeto atualizada",
})

TRANSLATIONS["en-US"].update({
    "object_transition_title": "Transition",
    "object_transition_enabled": "Enable transition on this object",
    "object_transition_target_map": "Target map",
    "object_transition_target_spawn": "Target spawn",
    "object_transition_apply": "Apply transition",
    "object_transition_help": (
        "Transition is a generic placement capability. "
        "It can be used by doors, portals, stairs, and other objects."
    ),
    "object_transition_saved": "Object transition updated",
})


# player-attack-context-menu-localization-v1
TRANSLATIONS["pt-BR"].update({
    "player_configure": "Configurar Player...",
    "player_attacks_manage": "Ataques...",
})

TRANSLATIONS["en-US"].update({
    "player_configure": "Configure player...",
    "player_attacks_manage": "Attacks...",
})


# attack-library-localization-v1
TRANSLATIONS["pt-BR"].update({
    "attacks_tab": "Ataques",
    "attack_search": "Procurar ataques...",
    "attack_new": "Novo ataque",
    "attack_edit": "Editar",
    "attack_delete": "Excluir",
    "attack_new_title": "Novo ataque",
    "attack_id": "Id",
    "attack_kind": "Tipo",
    "attack_damage": "Dano",
    "attack_knockback": "Knockback",
    "attack_total_ticks": "Duração (ticks)",
    "attack_cooldown": "Cooldown (ticks)",
    "attack_min_range": "Alcance mínimo",
    "attack_max_range": "Alcance máximo",
    "attack_visual_action": "Visual action",
    "attack_kind_info": (
        "Corpo a corpo usa hitbox em cada direção. "
        "Projétil cria um projétil durante o ataque."
    ),
    "attack_damage_info": (
        "Dano aplicado quando a hitbox ou o projétil acerta um alvo válido."
    ),
    "attack_knockback_info": (
        "Distância do empurrão em pixels. A seta amarela no preview mostra a direção."
    ),
    "attack_total_ticks_info": (
        "Duração do ataque em ticks. O jogo roda a 60 ticks por segundo."
    ),
    "attack_cooldown_info": (
        "Ticks de espera após o ataque antes de outro ataque poder começar."
    ),
    "attack_min_range_info": (
        "Alcance mínimo útil em pixels. Serve como guia de conteúdo e seleção da IA."
    ),
    "attack_max_range_info": (
        "Alcance máximo em pixels. A linha azul tracejada mostra esse limite."
    ),
    "attack_visual_action_info": (
        "Id que conecta o ataque à animação de ação do player ou inimigo."
    ),
    "attack_facing_info": (
        "Direção usada no preview e na caixa de hitbox exibida."
    ),
    "attack_melee_hitboxes_info": (
        "Offsets são relativos ao ponto dos pés. Largura e altura precisam ser maiores que zero."
    ),
    "attack_projectile_info": (
        "Projétil criado no evento spawnProjectile da linha do tempo."
    ),
    "attack_ammo": "Consome item",
    "attack_ammo_none": "Nenhum",
    "attack_ammo_amount": "Qtd",
    "attack_ammo_info": (
        "Item removido do inventário do Player quando o ataque começa. "
        "Sem o item no inventário, o ataque não dispara."
    ),
    "attack_timeline_info": (
        "Cada faixa é um frame da animação e os círculos são eventos. Clique para inspecionar um tick."
    ),
    "attack_melee_hitboxes": "Hitboxes corpo a corpo",
    "attack_projectile": "Projétil",
    "projectile_new_from_animation": "Novo...",
    "projectile_new_title": "Novo projétil",
    "projectile_new_id": "Id do projétil",
    "projectile_new_animation": "Spritesheet/animação",
    "projectile_no_animations": "Nenhuma animação está disponível.",
    "projectile_spawn_offsets": "Posição do projétil por direção",
    "projectile_spawn_editor_title": "Posições do projétil",
    "projectile_edit_positions": "Editar...",
    "projectile_direction": "Direção",
    "projectile_group_flight": "Projétil",
    "projectile_group_impact": "Animação de colisão",
    "projectile_group_expire": "Animação de limite de distância",
    "projectile_animation": "Animação",
    "projectile_loop": "Loop",
    "projectile_flight_loop_help": (
        "Repetir a animação de voo. Desmarcado, a animação toca uma vez "
        "e o último frame fica fixo até o projétil terminar."
    ),
    "projectile_end_loop_help": (
        "Animações de colisão e de limite não podem repetir. Se a animação "
        "selecionada tiver loop, desmarque para remover o loop ao salvar."
    ),
    "projectile_drop_group": "Item ao cair",
    "projectile_drop_expire": "Ao expirar (limite)",
    "projectile_drop_impact": "Ao colidir",
    "projectile_drop_none": "Nenhum",
    "projectile_drop_help": (
        "Pickup coletável deixado no chão onde o projétil terminar. "
        "Ao expirar cobre o limite de distância/tempo de vida; ao colidir "
        "cobrir parede, alvo e objetos. A chance é a probabilidade de drop."
    ),
    "projectile_canonical_facing": (
        "Orientação base da imagem"
    ),
    "projectile_render_layer": "Camada (direção selecionada)",
        "projectile_maximum_distance": "Limite de distância (px)",
        "projectile_distance_unlimited": "Sem limite",
        "projectile_impact_animation": "Animação de colisão",
        "projectile_expire_animation": "Animação de limite de alcance (direção selecionada)",
        "projectile_end_facing": "Orientação base",
        "projectile_flip_x": "Espelhar",
        "projectile_end_animation_none": "Nenhuma",
    "projectile_render_layer_actor": (
        "Na frente do player"
    ),
    "projectile_render_layer_world": (
        "Atrás do player"
    ),
    "projectile_spawn_offsets_info": (
        "Offsets relativos ao ponto dos pés do Player no momento do "
        "evento spawnProjectile. Espelhar, camada e as animações de "
        "colisão/limite são salvos para a direção selecionada."
    ),
    "attack_timeline": "Linha de tempo",
    "attack_timeline_play": "Reproduzir",
    "attack_timeline_pause": "Pausar",
    "attack_facing": "Direção",
    "attack_timeline_hint": (
        "Clique na linha do tempo para ver o frame daquele tick. "
        "Cada faixa é um frame da animação e os círculos são eventos de ataque."
    ),
    "attack_event_tick": "Tick",
    "attack_event_add": "Adicionar",
    "attack_event_remove": "Remover",
    "attack_save": "Salvar",
    "attack_cancel": "Cancelar",
    "attack_offsetX": "offsetX",
    "attack_offsetY": "offsetY",
    "attack_width": "largura",
    "attack_height": "altura",
    "attack_status_builtin": "builtin",
    "attack_status_override": "sobrescrito",
    "attack_status_authored": "autoral",
    "attack_saved": "Ataque salvo: {definition_id}",
    "attack_deleted": "Ataque excluído: {definition_id}",
    "attack_library_help": (
        "Ataques builtin mostram os valores do runtime e podem ser "
        "sobrescritos com o mesmo id. Excluir um override volta ao "
        "builtin; excluir um ataque referenciado por inimigos é bloqueado."
    ),
})

TRANSLATIONS["en-US"].update({
    "attacks_tab": "Attacks",
    "attack_search": "Search attacks...",
    "attack_new": "New attack",
    "attack_edit": "Edit",
    "attack_delete": "Delete",
    "attack_new_title": "New attack",
    "attack_id": "Id",
    "attack_kind": "Kind",
    "attack_damage": "Damage",
    "attack_knockback": "Knockback",
    "attack_total_ticks": "Duration (ticks)",
    "attack_cooldown": "Cooldown (ticks)",
    "attack_min_range": "Minimum range",
    "attack_max_range": "Maximum range",
    "attack_visual_action": "Visual action",
    "attack_kind_info": (
        "Melee uses a hitbox per facing. Projectile spawns a projectile during the attack."
    ),
    "attack_damage_info": (
        "Damage applied when the hitbox or projectile hits a valid target."
    ),
    "attack_knockback_info": (
        "Push distance in pixels. The yellow arrow in the preview shows the direction."
    ),
    "attack_total_ticks_info": (
        "Attack duration in ticks. The game runs at 60 ticks per second."
    ),
    "attack_cooldown_info": (
        "Waiting ticks after an attack before another attack may start."
    ),
    "attack_min_range_info": (
        "Minimum useful range in pixels. It guides content authoring and AI selection."
    ),
    "attack_max_range_info": (
        "Maximum range in pixels. The dashed blue line shows this limit."
    ),
    "attack_visual_action_info": (
        "ID linking the attack to a player or enemy action animation."
    ),
    "attack_facing_info": (
        "Facing used by the preview and by the displayed hitbox."
    ),
    "attack_melee_hitboxes_info": (
        "Offsets are relative to the feet point. Width and height must be greater than zero."
    ),
    "attack_projectile_info": (
        "Projectile created by the timeline's spawnProjectile event."
    ),
    "attack_ammo": "Consumes item",
    "attack_ammo_none": "None",
    "attack_ammo_amount": "Amount",
    "attack_ammo_info": (
        "Item removed from the player inventory when the attack starts. "
        "Without the item in stock the attack never fires."
    ),
    "attack_timeline_info": (
        "Each span is an animation frame and circles are events. Click to inspect a tick."
    ),
    "attack_melee_hitboxes": "Melee hitboxes",
    "attack_projectile": "Projectile",
    "projectile_new_from_animation": "New...",
    "projectile_new_title": "New projectile",
    "projectile_new_id": "Projectile id",
    "projectile_new_animation": "Spritesheet/animation",
    "projectile_no_animations": "No animations are available.",
    "projectile_spawn_offsets": "Projectile position per direction",
    "projectile_spawn_editor_title": "Projectile positions",
    "projectile_edit_positions": "Edit...",
    "projectile_direction": "Direction",
    "projectile_group_flight": "Projectile",
    "projectile_group_impact": "Impact animation",
    "projectile_group_expire": "Range limit animation",
    "projectile_animation": "Animation",
    "projectile_loop": "Loop",
    "projectile_flight_loop_help": (
        "Repeat the flight animation. Unchecked, it plays once and the "
        "last frame is held until the projectile ends."
    ),
    "projectile_end_loop_help": (
        "Impact and range-limit animations must not repeat. If the selected "
        "animation loops, uncheck this to remove the loop on save."
    ),
    "projectile_drop_group": "Dropped item",
    "projectile_drop_expire": "On expire (range limit)",
    "projectile_drop_impact": "On impact",
    "projectile_drop_none": "None",
    "projectile_drop_help": (
        "Collectible pickup left on the ground where the projectile ends. "
        "On expire covers the distance/lifetime limit; on impact covers "
        "walls, targets and objects. Chance is the drop probability."
    ),
    "projectile_canonical_facing": (
        "Base image orientation"
    ),
    "projectile_render_layer": "Layer (selected direction)",
        "projectile_maximum_distance": "Distance limit (px)",
        "projectile_distance_unlimited": "Unlimited",
        "projectile_impact_animation": "Impact animation",
        "projectile_expire_animation": "Range limit animation (selected direction)",
        "projectile_end_facing": "Base orientation",
        "projectile_flip_x": "Mirror",
        "projectile_end_animation_none": "None",
    "projectile_render_layer_actor": (
        "In front of player"
    ),
    "projectile_render_layer_world": (
        "Behind player"
    ),
    "projectile_spawn_offsets_info": (
        "Offsets relative to the player's feet when the spawnProjectile "
        "timeline event fires. Mirror, layer and the impact/range-limit "
        "animations are stored for the selected direction."
    ),
    "attack_timeline": "Timeline",
    "attack_timeline_play": "Play",
    "attack_timeline_pause": "Pause",
    "attack_facing": "Facing",
    "attack_timeline_hint": (
        "Click the timeline to preview the frame at that tick. "
        "Each span is one animation frame; circles are attack events."
    ),
    "attack_event_tick": "Tick",
    "attack_event_add": "Add",
    "attack_event_remove": "Remove",
    "attack_save": "Save",
    "attack_cancel": "Cancel",
    "attack_offsetX": "offsetX",
    "attack_offsetY": "offsetY",
    "attack_width": "width",
    "attack_height": "height",
    "attack_status_builtin": "builtin",
    "attack_status_override": "overridden",
    "attack_status_authored": "authored",
    "attack_saved": "Attack saved: {definition_id}",
    "attack_deleted": "Attack deleted: {definition_id}",
    "attack_library_help": (
        "Builtin attacks show runtime values and can be overridden with "
        "the same id. Deleting an override reverts to the builtin; "
        "deleting an attack referenced by enemies is blocked."
    ),
})


# uiux-audit-g1-global-translation-v1
TRANSLATIONS["pt-BR"].update({
    "place_in_map": "Colocar no mapa",
    "find_usages": "Localizar usos",
    "back": "Voltar",
    "search_definitions": "Procurar nome exibido / definitionId",
    "delete_item": "Excluir item",
    "remove_last": "Remover último",
    "empty_collection": "(coleção vazia)",
    "move_up": "Mover para cima",
    "move_down": "Mover para baixo",
    "hide_show": "Ocultar/Mostrar",
    "delete_definition_confirm": "Excluir {definition_id}?",
    "no_diagnostics": "Sem diagnósticos",
    "tool_status": "Ferramenta: {tool}",
    "tool_none": "Nenhuma",
    "saved": "Salvo",
    "new_blank_project": "Novo projeto em branco",
    "validation_passed": "Validação passou",
    "validation_failed": "Validação falhou",
    "playtest_started": "Playtest iniciado",
    "playtest_failed": "Playtest falhou",
    "playtest_stopped": "Playtest parado",
    "definition_edited": "Definição editada",
    "collection_updated": "Coleção atualizada",
    "asset_assigned": "Asset atribuído",
    "no_usages_found": "Nenhum uso encontrado",
    "no_previous_definition": "Nenhuma definição anterior",
    "no_asset_selected": "Nenhum asset selecionado",
    "builtin_read_only": "Definições builtin são somente leitura",
    "autosave_written": "Autosave gravado ({count} arquivo(s))",
    "map_remove_confirm": "Excluir o mapa \"{map_id}\" do projeto?",
    "layer_remove_confirm": "Excluir a camada \"{name}\"?",
    "scene_delete_confirm": "Excluir a cena \"{scene_id}\"?",
    "no_scene_selected": "Nenhuma cena selecionada",
    "new_scene": "Nova cena",
    "actors": "Atores",
    "add_actor": "Adicionar ator",
    "remove_actor": "Remover ator",
    "timeline": "Linha do tempo",
    "add_clip": "Adicionar clipe",
    "duplicate_clip": "Duplicar clipe",
    "remove_clip": "Remover clipe",
    "add_track": "Adicionar trilha",
    "play": "Reproduzir",
    "pause": "Pausar",
    "restart": "Reiniciar",
    "timeline_zoom": "Zoom da linha do tempo",
    "playhead": "Posição de reprodução",
    "markers": "Marcadores",
    "add_marker": "Adicionar marcador",
    "rename_marker": "Renomear marcador",
    "remove_marker": "Remover marcador",
    "fit_duration": "Ajustar duração",
    "add_activation": "Adicionar ativação",
    "preview_ticks": "Prévia: {ticks} ticks",
    "scene_status": "Cena {scene} — {count} diagnóstico(s)",
    "scene_actor_slot": "ID do slot",
    "scene_kind": "Tipo",
    "scene_actor_instance": "Instância",
    "add_scene_actor": "Adicionar ator à cena",
    "add_scene_track": "Adicionar trilha",
    "scene_start_tick": "Tick inicial",
    "scene_duration_ticks": "Duração (ticks)",
    "scene_marker_name": "Nome do marcador",
    "scene_actor_requires_instance": "Coloque uma instância de {kind} no mapa primeiro",
    "scene_actor_track_requires_actor": "Adicione um ator à cena antes de criar a trilha",
    "scene_world_rules_invalid": "World rules do mapa não é um array",
    "scene_activation_added": "Ativação adicionada para {scene}",
    "export_completed": "Exportação DMAP concluída",
    "export_failed": "Exportação DMAP falhou",
})

TRANSLATIONS["en-US"].update({
    "place_in_map": "Place in Map",
    "find_usages": "Find Usages",
    "back": "Back",
    "search_definitions": "Search display name / definitionId",
    "delete_item": "Delete item",
    "remove_last": "Remove Last",
    "empty_collection": "(empty collection)",
    "move_up": "Move Up",
    "move_down": "Move Down",
    "hide_show": "Hide/Show",
    "delete_definition_confirm": "Delete {definition_id}?",
    "no_diagnostics": "No diagnostics",
    "tool_status": "Tool: {tool}",
    "tool_none": "None",
    "saved": "Saved",
    "new_blank_project": "New blank project",
    "validation_passed": "Validation passed",
    "validation_failed": "Validation failed",
    "playtest_started": "Playtest started",
    "playtest_failed": "Playtest failed",
    "playtest_stopped": "Playtest stopped",
    "definition_edited": "Definition edited",
    "collection_updated": "Collection updated",
    "asset_assigned": "Asset assigned",
    "no_usages_found": "No usages found",
    "no_previous_definition": "No previous definition",
    "no_asset_selected": "No asset selected",
    "builtin_read_only": "Builtin definitions are read-only",
    "autosave_written": "Autosave written ({count} file(s))",
    "map_remove_confirm": "Remove map \"{map_id}\" from the project?",
    "layer_remove_confirm": "Remove layer \"{name}\"?",
    "scene_delete_confirm": "Delete scene \"{scene_id}\"?",
    "no_scene_selected": "No scene selected",
    "new_scene": "New Scene",
    "actors": "Actors",
    "add_actor": "Add Actor",
    "remove_actor": "Remove Actor",
    "timeline": "Timeline",
    "add_clip": "Add Clip",
    "duplicate_clip": "Duplicate Clip",
    "remove_clip": "Remove Clip",
    "add_track": "Add Track",
    "play": "Play",
    "pause": "Pause",
    "restart": "Restart",
    "timeline_zoom": "Timeline zoom",
    "playhead": "Playhead",
    "markers": "Markers",
    "add_marker": "Add Marker",
    "rename_marker": "Rename Marker",
    "remove_marker": "Remove Marker",
    "fit_duration": "Fit Duration",
    "add_activation": "Add Activation",
    "preview_ticks": "Preview: {ticks} ticks",
    "scene_status": "Scene {scene} — {count} diagnostic(s)",
    "scene_actor_slot": "Slot ID",
    "scene_kind": "Kind",
    "scene_actor_instance": "Instance",
    "add_scene_actor": "Add Scene Actor",
    "add_scene_track": "Add Scene Track",
    "scene_start_tick": "Start tick",
    "scene_duration_ticks": "Duration ticks",
    "scene_marker_name": "Marker name",
    "scene_actor_requires_instance": "Place a {kind} instance on the map first",
    "scene_actor_track_requires_actor": "Add a scene actor before adding an actor track",
    "scene_world_rules_invalid": "Map world rules are not an array",
    "scene_activation_added": "Activation added for {scene}",
    "export_completed": "DMAP export completed",
    "export_failed": "DMAP export failed",
})

TRANSLATIONS["pt-BR"].update({
    "no_tileset_selected": "Nenhum tileset selecionado",
    "tileset_label": "Tileset",
    "semantic_tiles": "Tiles semânticos",
    "stamps": "Stamps",
    "semantics": "Semântica",
    "all": "Todas",
    "search_assets": "Procurar assets",
    "assign_to_definition": "Atribuir à definição selecionada",
})

TRANSLATIONS["en-US"].update({
    "no_tileset_selected": "No tileset selected",
    "tileset_label": "Tileset",
    "semantic_tiles": "Semantic Tiles",
    "stamps": "Stamps",
    "semantics": "Semantics",
    "all": "All",
    "search_assets": "Search assets",
    "assign_to_definition": "Assign to selected definition",
})

TRANSLATIONS["pt-BR"].update({
    "select_visual_or_tileset": "Selecione uma Visual Image ou Tileset para atribuir o asset",
    "content_directory_unavailable": "O diretório de conteúdo do repositório está indisponível",
    "workspace_required_export": "Abra um workspace de conteúdo do projeto antes de exportar",
    "map_not_found": "mapa não encontrado",
    "placement_active": "Colocação ativa: {definition_id}. Clique no mapa ou pressione Escape.",
})

TRANSLATIONS["en-US"].update({
    "select_visual_or_tileset": "Select a Visual Image or Tileset definition to assign an asset",
    "content_directory_unavailable": "The repository content directory is unavailable",
    "workspace_required_export": "Open a project content workspace before exporting",
    "map_not_found": "map was not found",
    "placement_active": "Placement active: {definition_id}. Click the map or press Escape.",
})

TRANSLATIONS["pt-BR"].update({
    "inspector_empty_hint": "Selecione um tile, objeto, porta ou região no mapa para editar suas propriedades. As ferramentas Selecionar e Apagar ficam na barra acima do mapa.",
})

TRANSLATIONS["en-US"].update({
    "inspector_empty_hint": "Select a tile, object, door or region on the map to edit its properties. Select and Erase tools live on the toolbar above the map.",
})

# canvas-hud-localization-v1 (audit C1/G7 + G4/C2)
TRANSLATIONS["pt-BR"].update({
    "zoom_in": "Aproximar (Ctrl + +)",
    "zoom_out": "Afastar (Ctrl + -)",
    "fit_map": "Enquadrar mapa (Home)",
    "placement_banner_hint": "clique para posicionar · Esc cancela",
    "placing_tile": "Pincel de tile: {tileset} #{index}",
    "placing_stamp": "Stamp: {name}",
    "placing_door": "Porta: {name}",
    "placing_entity": "Colocando: {name}",
    "placing_element": "Elemento: {name}",
    "placing_terrain": "Smart Terrain ativo",
    "placing_pattern": "Smart Terrain — padrão {name}",
})

TRANSLATIONS["en-US"].update({
    "zoom_in": "Zoom in (Ctrl + +)",
    "zoom_out": "Zoom out (Ctrl + -)",
    "fit_map": "Frame map (Home)",
    "placement_banner_hint": "click to place · Esc cancels",
    "placing_tile": "Tile brush: {tileset} #{index}",
    "placing_stamp": "Stamp: {name}",
    "placing_door": "Door: {name}",
    "placing_entity": "Placing: {name}",
    "placing_element": "Element: {name}",
    "placing_terrain": "Smart Terrain active",
    "placing_pattern": "Smart Terrain — pattern {name}",
})

# inspector-field-labels-v1 (audit IN1)
TRANSLATIONS["pt-BR"].update({
    "field_id": "ID",
    "field_definitionId": "Definição",
    "field_visualSetId": "Visual",
    "field_position": "Posição",
    "field_x": "X",
    "field_y": "Y",
    "field_facing": "Direção",
    "field_canonicalFacing": "Direção canônica",
    "field_initialContents": "Conteúdo inicial",
    "field_persistence": "Persistência",
    "field_door": "Porta",
    "field_transition": "Transição",
    "field_health": "Vida",
    "field_maximumHealth": "Vida máxima",
    "field_attackIds": "Ataques",
    "field_attackId": "Ataque",
    "field_behaviorProfileId": "Comportamento",
    "field_rewardProfileId": "Perfil de drop",
    "field_rewardGrantId": "Recompensa",
    "field_dialogueId": "Diálogo",
    "field_defaultDialogueId": "Diálogo padrão",
    "field_itemId": "Item",
    "field_quantity": "Quantidade",
    "field_enabled": "Ativado",
    "field_once": "Disparar uma vez",
    "field_trigger": "Gatilho",
    "field_conditions": "Condições",
    "field_actions": "Ações",
    "field_durationTicks": "Duração (ticks)",
    "field_startTick": "Início (tick)",
    "field_kind": "Tipo",
    "field_targetMapId": "Mapa de destino",
    "field_targetSpawnId": "Spawn de destino",
    "field_target": "Alvo",
    "field_width": "Largura",
    "field_height": "Altura",
    "field_layer": "Camada",
    "field_name": "Nome",
    "field_displayName": "Nome exibido",
    "field_relativeAssetPath": "Arquivo (assets)",
    "field_relativePath": "Arquivo",
    "field_tileSize": "Tamanho do tile",
    "field_columns": "Colunas",
    "field_rows": "Linhas",
    "field_source": "Recorte",
    "field_anchor": "Âncora",
    "field_drawOffset": "Deslocamento visual",
    "field_frames": "Frames",
    "field_loop": "Repetir",
    "field_markers": "Marcadores",
    "field_imageId": "Imagem",
    "field_visualId": "Visual",
    "field_pickupDefinitionId": "Pickup",
    "field_instanceTarget": "Instância alvo",
    "field_worldAction": "Ação de mundo",
    "field_emote": "Emote",
    "field_heightPixels": "Altura (px)",
    "field_waitForCompletion": "Aguardar conclusão",
    "field_actors": "Atores",
    "field_tracks": "Trilhas",
    "field_scenes": "Cenas",
    "inspector_title": "{label} #{id}",
    "category_objects": "Objeto",
    "category_enemies": "Inimigo",
    "category_npcs": "NPC",
    "category_pickups": "Pickup",
    "category_links": "Link de mapa",
    "category_playerSpawns": "Player Spawn",
    "category_regions": "Região",
    "category_worldRules": "Regra de mundo",
    "category_encounters": "Encounter",
})

TRANSLATIONS["en-US"].update({
    "field_id": "ID",
    "field_definitionId": "Definition",
    "field_visualSetId": "Visual",
    "field_position": "Position",
    "field_x": "X",
    "field_y": "Y",
    "field_facing": "Facing",
    "field_canonicalFacing": "Canonical facing",
    "field_initialContents": "Initial contents",
    "field_persistence": "Persistence",
    "field_door": "Door",
    "field_transition": "Transition",
    "field_health": "Health",
    "field_maximumHealth": "Maximum health",
    "field_attackIds": "Attacks",
    "field_attackId": "Attack",
    "field_behaviorProfileId": "Behavior",
    "field_rewardProfileId": "Drop profile",
    "field_rewardGrantId": "Reward",
    "field_dialogueId": "Dialogue",
    "field_defaultDialogueId": "Default dialogue",
    "field_itemId": "Item",
    "field_quantity": "Quantity",
    "field_enabled": "Enabled",
    "field_once": "Fire once",
    "field_trigger": "Trigger",
    "field_conditions": "Conditions",
    "field_actions": "Actions",
    "field_durationTicks": "Duration (ticks)",
    "field_startTick": "Start (tick)",
    "field_kind": "Kind",
    "field_targetMapId": "Target map",
    "field_targetSpawnId": "Target spawn",
    "field_target": "Target",
    "field_width": "Width",
    "field_height": "Height",
    "field_layer": "Layer",
    "field_name": "Name",
    "field_displayName": "Display name",
    "field_relativeAssetPath": "File (assets)",
    "field_relativePath": "File",
    "field_tileSize": "Tile size",
    "field_columns": "Columns",
    "field_rows": "Rows",
    "field_source": "Source rect",
    "field_anchor": "Anchor",
    "field_drawOffset": "Visual offset",
    "field_frames": "Frames",
    "field_loop": "Loop",
    "field_markers": "Markers",
    "field_imageId": "Image",
    "field_visualId": "Visual",
    "field_pickupDefinitionId": "Pickup",
    "field_instanceTarget": "Target instance",
    "field_worldAction": "World action",
    "field_emote": "Emote",
    "field_heightPixels": "Height (px)",
    "field_waitForCompletion": "Wait for completion",
    "field_actors": "Actors",
    "field_tracks": "Tracks",
    "field_scenes": "Scenes",
    "inspector_title": "{label} #{id}",
    "category_objects": "Object",
    "category_enemies": "Enemy",
    "category_npcs": "NPC",
    "category_pickups": "Pickup",
    "category_links": "Map link",
    "category_playerSpawns": "Player Spawn",
    "category_regions": "Region",
    "category_worldRules": "World rule",
    "category_encounters": "Encounter",
})

# diagnostics-panel-localization-v1 (audit S2)
TRANSLATIONS["pt-BR"].update({
    "diagnostics_title": "Diagnósticos",
    "diagnostics_empty": "Nenhum problema encontrado",
    "diagnostics_counts": "Erros: {error} · Avisos: {warning} · Info: {info}",
    "filter_all": "Todos",
    "filter_errors": "Erros",
    "filter_warnings": "Avisos",
    "filter_info": "Info",
    "definition_opened": "Abrindo {definition_id}",
    "definition_not_found": "Definição não encontrada: {definition_id}",
})

TRANSLATIONS["en-US"].update({
    "diagnostics_title": "Diagnostics",
    "diagnostics_empty": "No problems found",
    "diagnostics_counts": "Errors: {error} · Warnings: {warning} · Info: {info}",
    "filter_all": "All",
    "filter_errors": "Errors",
    "filter_warnings": "Warnings",
    "filter_info": "Info",
    "definition_opened": "Opening {definition_id}",
    "definition_not_found": "Definition not found: {definition_id}",
})

# semantic-palette-localization-v1 (audit SS1/ST1-ST3)
TRANSLATIONS["pt-BR"].update({
    "search_semantics": "Procurar semântica ou stamp",
    "terrain_no_selection": "Selecione uma família para pintar com o Smart Terrain.",
    "terrain_seed_tip": "Semente determinística: mesma semente e família geram sempre a mesma variação de tiles.",
    "room_brush_tip": "Pinta piso e paredes de uma sala inteira. Requer família com papéis de piso e parede.",
    "role_floor": "Piso",
    "role_wall": "Parede",
    "role_decoration": "Decoração",
    "role_water": "Água",
    "role_unknown": "Indefinido",
    "topology_interior": "Interno",
    "topology_north": "Norte",
    "topology_east": "Leste",
    "topology_south": "Sul",
    "topology_west": "Oeste",
    "topology_corner": "Canto",
    "topology_unknown": "Indefinido",
})

TRANSLATIONS["en-US"].update({
    "search_semantics": "Search semantics or stamps",
    "terrain_no_selection": "Select a family to paint with Smart Terrain.",
    "terrain_seed_tip": "Deterministic seed: the same seed and family always produce the same tile variation.",
    "room_brush_tip": "Paints the floor and walls of a whole room. Requires a family with floor and wall roles.",
    "role_floor": "Floor",
    "role_wall": "Wall",
    "role_decoration": "Decoration",
    "role_water": "Water",
    "role_unknown": "Undefined",
    "topology_interior": "Interior",
    "topology_north": "North",
    "topology_east": "East",
    "topology_south": "South",
    "topology_west": "West",
    "topology_corner": "Corner",
    "topology_unknown": "Undefined",
})

TRANSLATIONS["pt-BR"].update({
    "topology_outerCorner": "Canto externo",
    "topology_straightHorizontal": "Horizontal",
    "topology_straightVertical": "Vertical",
})

TRANSLATIONS["en-US"].update({
    "topology_outerCorner": "Outer corner",
    "topology_straightHorizontal": "Horizontal",
    "topology_straightVertical": "Vertical",
})

TRANSLATIONS["pt-BR"].update({
    "semantic_edges": "Vizinhos (arestas)",
    "semantic_tile_header": "{tileset} · tile #{index}",
})

TRANSLATIONS["en-US"].update({
    "semantic_edges": "Neighbors (edges)",
    "semantic_tile_header": "{tileset} · tile #{index}",
})

TRANSLATIONS["pt-BR"].update({
    "map_entry_badge": "Mapa de entrada do jogo",
})

TRANSLATIONS["en-US"].update({
    "map_entry_badge": "Game entry map",
})

TRANSLATIONS["pt-BR"].update({
    "atlas_used_tiles": "{used} de {total} com desenho",
})

TRANSLATIONS["en-US"].update({
    "atlas_used_tiles": "{used} of {total} drawn",
})

# recent-menu + settings dialog localization (audit G10/G11)
TRANSLATIONS["pt-BR"].update({
    "open_recent": "Abrir Recente",
    "no_recent_projects": "Nenhum projeto recente",
    "recent_project_missing": "Projeto não encontrado: {path}",
    "settings": "Preferências...",
    "settings_title": "Preferências do Studio",
    "settings_language": "Idioma",
    "settings_theme": "Tema",
    "settings_asset_root": "Raiz de assets",
    "settings_browse": "Procurar...",
    "settings_saved": "Preferências salvas",
    "map_preset": "Tamanho predefinido",
    "map_preset_custom": "Personalizado",
    "map_preset_hint": "Escolha um tamanho predefinido ou informe dimensões personalizadas.",
})

TRANSLATIONS["en-US"].update({
    "open_recent": "Open Recent",
    "no_recent_projects": "No recent projects",
    "recent_project_missing": "Project not found: {path}",
    "settings": "Preferences...",
    "settings_title": "Studio Preferences",
    "settings_language": "Language",
    "settings_theme": "Theme",
    "settings_asset_root": "Asset root",
    "settings_browse": "Browse...",
    "settings_saved": "Preferences saved",
    "map_preset": "Size preset",
    "map_preset_custom": "Custom",
    "map_preset_hint": "Pick a size preset or enter custom dimensions.",
})

# crafting-library-localization-v1
TRANSLATIONS["pt-BR"].update({
    "crafting_tab": "Crafting",
    "search_recipes": "Procurar receitas",
    "create_recipe": "Nova Receita...",
    "configure_recipe": "Editar Receita...",
    "delete_recipe": "Excluir Receita",
    "recipe_name": "Nome",
    "recipe_id": "ID da receita",
    "recipe_inputs": "Ingredientes",
    "recipe_outputs": "Resultados",
    "recipe_add_input": "Ingrediente",
    "recipe_add_output": "Resultado",
    "recipe_item": "Item",
    "recipe_quantity": "Quantidade",
    "recipe_remove": "Remover",
    "recipe_preview": "Pré-visualização",
    "recipe_needs_inputs": "Receitas precisam de 2 a 4 ingredientes distintos.",
    "recipe_needs_outputs": "Receitas precisam de pelo menos 1 resultado.",
    "recipe_creation_help": (
        "Cada receita combina de 2 a 4 itens distintos e produz de 1 a 4 itens. "
        "O ID usa o namespace recipe.* e os Itens já devem existir."
    ),
    "recipe_created": "Receita criada",
    "recipe_configured": "Receita atualizada",
    "recipe_deleted": "Receita excluída",
    "recipe_delete_confirm": "Excluir a receita {recipe}?",
    "no_recipes": "Nenhuma receita criada",
})

TRANSLATIONS["en-US"].update({
    "crafting_tab": "Crafting",
    "search_recipes": "Search recipes",
    "create_recipe": "New Recipe...",
    "configure_recipe": "Edit Recipe...",
    "delete_recipe": "Delete Recipe",
    "recipe_name": "Name",
    "recipe_id": "Recipe ID",
    "recipe_inputs": "Ingredients",
    "recipe_outputs": "Results",
    "recipe_add_input": "Ingredient",
    "recipe_add_output": "Result",
    "recipe_item": "Item",
    "recipe_quantity": "Quantity",
    "recipe_remove": "Remove",
    "recipe_preview": "Preview",
    "recipe_needs_inputs": "Recipes need 2 to 4 distinct ingredients.",
    "recipe_needs_outputs": "Recipes need at least 1 result.",
    "recipe_creation_help": (
        "Each recipe combines 2 to 4 distinct items and produces 1 to 4 items. "
        "IDs use the recipe.* namespace and Items must already exist."
    ),
    "recipe_created": "Recipe created",
    "recipe_configured": "Recipe updated",
    "recipe_deleted": "Recipe deleted",
    "recipe_delete_confirm": "Delete recipe {recipe}?",
    "no_recipes": "No recipes created",
})
