#include "editor/editor_localization.h"

#include <array>
#include <utility>

namespace underworld::editor {
namespace {
struct Entry final { EditorTextId id; std::string_view english; std::string_view portuguese; };

constexpr std::array entries{
    Entry{EditorTextId::file,"File","Arquivo"}, Entry{EditorTextId::edit,"Edit","Editar"},
    Entry{EditorTextId::view,"View","Visualizar"}, Entry{EditorTextId::settings,"Settings","Configurações"},
    Entry{EditorTextId::language,"Language","Idioma"}, Entry{EditorTextId::newMap,"New","Novo"},
    Entry{EditorTextId::openMap,"Open Map...","Abrir Mapa..."}, Entry{EditorTextId::save,"Save","Salvar"},
    Entry{EditorTextId::saveAs,"Save As...","Salvar Como..."}, Entry{EditorTextId::saveAll,"Save All","Salvar Tudo"},
    Entry{EditorTextId::exit,"Exit","Sair"}, Entry{EditorTextId::undo,"Undo","Desfazer"},
    Entry{EditorTextId::redo,"Redo","Refazer"}, Entry{EditorTextId::mapMode,"Map Mode","Modo Mapa"},
    Entry{EditorTextId::contentMode,"Content Mode","Modo Conteúdo"}, Entry{EditorTextId::grid,"Grid","Grade"},
    Entry{EditorTextId::frameMap,"Frame Map","Enquadrar Mapa"}, Entry{EditorTextId::validateWorkspace,"Validate Workspace","Validar Workspace"},
    Entry{EditorTextId::tiles,"TILES","TILES"}, Entry{EditorTextId::semantics,"SEMANTICS","SEMÂNTICA"},
    Entry{EditorTextId::stamps,"STAMPS","CARIMBOS"}, Entry{EditorTextId::entities,"ENTITIES","ENTIDADES"},
    Entry{EditorTextId::layers,"LAYERS","CAMADAS"}, Entry{EditorTextId::rules,"RULES","REGRAS"},
    Entry{EditorTextId::encounters,"ENCOUNTERS","ENCONTROS"}, Entry{EditorTextId::select,"SELECT","SELECIONAR"},
    Entry{EditorTextId::pencil,"PENCIL","LÁPIS"}, Entry{EditorTextId::erase,"ERASE","APAGAR"},
    Entry{EditorTextId::rectangle,"RECT","RETÂNGULO"}, Entry{EditorTextId::fill,"FILL","PREENCHER"},
    Entry{EditorTextId::eyedropper,"EYEDROPPER","CONTA-GOTAS"}, Entry{EditorTextId::collision,"COLLISION","COLISÃO"},
    Entry{EditorTextId::region,"REGION","REGIÃO"}, Entry{EditorTextId::properties,"PROPERTIES","PROPRIEDADES"},
    Entry{EditorTextId::categories,"CATEGORIES","CATEGORIAS"}, Entry{EditorTextId::definitions,"DEFINITIONS","DEFINIÇÕES"},
    Entry{EditorTextId::inspector,"INSPECTOR","INSPETOR"}, Entry{EditorTextId::status,"STATUS","STATUS"},
    Entry{EditorTextId::add,"ADD","ADICIONAR"}, Entry{EditorTextId::remove,"REMOVE","REMOVER"},
    Entry{EditorTextId::moveUp,"MOVE UP","MOVER PARA CIMA"}, Entry{EditorTextId::moveDown,"MOVE DOWN","MOVER PARA BAIXO"},
    Entry{EditorTextId::open,"OPEN","ABRIR"}, Entry{EditorTextId::create,"CREATE","CRIAR"},
    Entry{EditorTextId::update,"UPDATE","ATUALIZAR"}, Entry{EditorTextId::clear,"CLEAR","LIMPAR"},
    Entry{EditorTextId::set,"SET","DEFINIR"}, Entry{EditorTextId::placeInMap,"PLACE IN MAP","COLOCAR NO MAPA"},
    Entry{EditorTextId::findInMap,"FIND IN MAP","ENCONTRAR NO MAPA"},
    Entry{EditorTextId::saveChangesBeforeContinuing,"Save changes before continuing?","Salvar alterações antes de continuar?"},
    Entry{EditorTextId::contentStudio,"Content Studio","Estúdio de Conteúdo"}, Entry{EditorTextId::mapMaker,"Map Maker","Editor de Mapas"},
    Entry{EditorTextId::builtinContent,"Builtin content","Conteúdo integrado"}, Entry{EditorTextId::contentWorkspace,"Content workspace","Workspace de conteúdo"},
    Entry{EditorTextId::workspaceInvalid,"Content workspace invalid","Workspace de conteúdo inválido"},
    Entry{EditorTextId::compileUnavailable,"Map compile unavailable","Compilação do mapa indisponível"},
    Entry{EditorTextId::playtestUnavailable,"Playtest unavailable","Playtest indisponível"},
    Entry{EditorTextId::definitionCreated,"Definition created","Definição criada"},
    Entry{EditorTextId::definitionUpdated,"Definition updated","Definição atualizada"},
    Entry{EditorTextId::definitionDeleted,"Definition deleted","Definição removida"},
    Entry{EditorTextId::workspaceValidated,"Workspace validated","Workspace validado"},
    Entry{EditorTextId::id,"ID","ID"}, Entry{EditorTextId::displayName,"Display Name","Nome de Exibição"},
    Entry{EditorTextId::visual,"Visual","Visual"}, Entry{EditorTextId::behavior,"Behavior","Comportamento"},
    Entry{EditorTextId::faction,"Faction","Facção"}, Entry{EditorTextId::health,"Health","Vida"},
    Entry{EditorTextId::movementSpeed,"Movement Speed","Velocidade de Movimento"},
    Entry{EditorTextId::collisionBox,"Collision","Colisão"}, Entry{EditorTextId::hurtbox,"Hurtbox","Área de Dano"},
    Entry{EditorTextId::reward,"Reward","Recompensa"}, Entry{EditorTextId::attacks,"Attacks","Ataques"},
    Entry{EditorTextId::stackLimit,"Stack Limit","Limite da Pilha"}, Entry{EditorTextId::category,"Category","Categoria"},
    Entry{EditorTextId::quantity,"Quantity","Quantidade"}, Entry{EditorTextId::dialogue,"Dialogue","Diálogo"},
    Entry{EditorTextId::quest,"Quest","Missão"}, Entry{EditorTextId::objective,"Objective","Objetivo"},
    Entry{EditorTextId::gold,"Gold","Ouro"}, Entry{EditorTextId::experience,"Experience","Experiência"},
    Entry{EditorTextId::buyPrice,"Buy Price","Preço de Compra"}, Entry{EditorTextId::sellPrice,"Sell Price","Preço de Venda"},
    Entry{EditorTextId::duration,"Duration","Duração"}, Entry{EditorTextId::priority,"Priority","Prioridade"},
    Entry{EditorTextId::animation,"Animation","Animação"}, Entry{EditorTextId::source,"Source","Origem"},
    Entry{EditorTextId::anchor,"Anchor","Âncora"}, Entry{EditorTextId::frame,"Frame","Quadro"},
    Entry{EditorTextId::loop,"Loop","Repetir"}, Entry{EditorTextId::image,"Image","Imagem"},
    Entry{EditorTextId::tilesets,"Tilesets","Conjuntos de Tiles"}, Entry{EditorTextId::projectiles,"Projectiles","Projéteis"},
    Entry{EditorTextId::attacksCategory,"Attacks","Ataques"}, Entry{EditorTextId::behaviors,"Behaviors","Comportamentos"},
    Entry{EditorTextId::enemies,"Enemies","Inimigos"}, Entry{EditorTextId::items,"Items","Itens"},
    Entry{EditorTextId::objects,"Objects","Objetos"}, Entry{EditorTextId::pickups,"Pickups","Coletáveis"},
    Entry{EditorTextId::npcVisuals,"NPC Visuals","Visuais de NPC"}, Entry{EditorTextId::npcs,"NPCs","NPCs"},
    Entry{EditorTextId::dialogues,"Dialogues","Diálogos"}, Entry{EditorTextId::quests,"Quests","Missões"},
    Entry{EditorTextId::playerProgressions,"Player Progressions","Progressão do Jogador"},
    Entry{EditorTextId::rewardProfiles,"Reward Profiles","Perfis de Recompensa"},
    Entry{EditorTextId::rewardGrants,"Reward Grants","Recompensas Garantidas"}, Entry{EditorTextId::shops,"Shops","Lojas"},
    Entry{EditorTextId::authoringDescriptors,"Authoring Descriptors","Descritores de Autoria"},
    Entry{EditorTextId::tileSemantics,"Tile Semantics","Semântica de Tiles"}, Entry{EditorTextId::visualImages,"Visual Images","Imagens Visuais"},
    Entry{EditorTextId::staticSprites,"Static Sprites","Sprites Estáticos"}, Entry{EditorTextId::animations,"Animations","Animações"},
    Entry{EditorTextId::enemyVisuals,"Enemy Visuals","Visuais de Inimigos"}, Entry{EditorTextId::objectVisuals,"Object Visuals","Visuais de Objetos"},
    Entry{EditorTextId::missingDefinition,"MISSING","AUSENTE"}, Entry{EditorTextId::readOnlyCategory,"Read-only category","Categoria somente leitura"},
    Entry{EditorTextId::fontUnavailable,"Font unavailable; authoring shell remains active: ","Fonte indisponível; o editor continuará ativo: "},
    Entry{EditorTextId::initializationError,"Map Maker initialization error","Erro de inicialização do Editor de Mapas"},
    Entry{EditorTextId::languagePortugueseBrazil,"Português (Brasil)","Português (Brasil)"},
    Entry{EditorTextId::languageEnglish,"English","English"}, Entry{EditorTextId::unknown,"",""},
    Entry{EditorTextId::maps,"Maps","MAPAS"}, Entry{EditorTextId::newProject,"New Project","Novo Projeto"},
    Entry{EditorTextId::importMap,"Import Map","Importar Mapa"}, Entry{EditorTextId::removeMap,"Remove Map","Remover Mapa"},
    Entry{EditorTextId::entryMap,"Entry Map","Mapa de Entrada"}, Entry{EditorTextId::setEntry,"Set Entry","Definir Entrada"},
    Entry{EditorTextId::targetMap,"Target Map","Mapa Alvo"}, Entry{EditorTextId::targetSpawn,"Target Spawn","Spawn Alvo"},
    Entry{EditorTextId::goToTarget,"Go To Target","Ir para o Alvo"}, Entry{EditorTextId::openProject,"Open Project...","Abrir Projeto..."},
    Entry{EditorTextId::saveProject,"Save Project","Salvar Projeto"}, Entry{EditorTextId::saveProjectAs,"Save Project As...","Salvar Projeto Como..."},
    Entry{EditorTextId::validateProject,"Validate Project","Validar Projeto"}, Entry{EditorTextId::projectValid,"Project valid","Projeto válido"},
    Entry{EditorTextId::projectInvalid,"Project invalid","Projeto inválido"}, Entry{EditorTextId::findInProject,"FIND IN PROJECT","ENCONTRAR NO PROJETO"},
};

const Entry* find(EditorTextId id) noexcept {
    for (const auto& entry : entries) if (entry.id == id) return &entry;
    return nullptr;
}
const Entry* find(std::string_view english) noexcept {
    for (const auto& entry : entries) if (entry.english == english) return &entry;
    return nullptr;
}

} // namespace

std::string_view editorLanguageCode(EditorLanguage language) noexcept {
    return language == EditorLanguage::englishUnitedStates ? "en-US" : "pt-BR";
}

std::string_view editorLanguageNativeName(EditorLanguage language) noexcept {
    return language == EditorLanguage::englishUnitedStates ? "English" : "Português (Brasil)";
}

std::string_view EditorLocalization::text(EditorTextId id) const noexcept {
    const auto* entry = find(id);
    if (!entry) return {};
    if (language_ == EditorLanguage::englishUnitedStates || entry->portuguese.empty()) {
        return entry->english;
    }
    return entry->portuguese;
}

std::string EditorLocalization::localize(std::string_view englishText) const {
    if (const auto* entry = find(englishText)) return std::string(text(entry->id));
    if (englishText == "tilesets") return language_ == EditorLanguage::englishUnitedStates ? "Tilesets" : "Conjuntos de Tiles";
    if (englishText == "projectiles") return language_ == EditorLanguage::englishUnitedStates ? "Projectiles" : "Projéteis";
    if (englishText == "attacks") return language_ == EditorLanguage::englishUnitedStates ? "Attacks" : "Ataques";
    if (englishText == "behaviors") return language_ == EditorLanguage::englishUnitedStates ? "Behaviors" : "Comportamentos";
    if (englishText == "enemies") return language_ == EditorLanguage::englishUnitedStates ? "Enemies" : "Inimigos";
    if (englishText == "items") return language_ == EditorLanguage::englishUnitedStates ? "Items" : "Itens";
    if (englishText == "objects") return language_ == EditorLanguage::englishUnitedStates ? "Objects" : "Objetos";
    if (englishText == "pickups") return language_ == EditorLanguage::englishUnitedStates ? "Pickups" : "Coletáveis";
    if (englishText == "npcVisuals") return language_ == EditorLanguage::englishUnitedStates ? "NPC Visuals" : "Visuais de NPC";
    if (englishText == "npcs") return "NPCs";
    if (englishText == "dialogues") return language_ == EditorLanguage::englishUnitedStates ? "Dialogues" : "Diálogos";
    if (englishText == "quests") return language_ == EditorLanguage::englishUnitedStates ? "Quests" : "Missões";
    if (englishText == "playerProgressions") return language_ == EditorLanguage::englishUnitedStates ? "Player Progressions" : "Progressão do Jogador";
    if (englishText == "rewardProfiles") return language_ == EditorLanguage::englishUnitedStates ? "Reward Profiles" : "Perfis de Recompensa";
    if (englishText == "rewardGrants") return language_ == EditorLanguage::englishUnitedStates ? "Reward Grants" : "Recompensas Garantidas";
    if (englishText == "shops") return language_ == EditorLanguage::englishUnitedStates ? "Shops" : "Lojas";
    if (englishText == "authoringDescriptors") return language_ == EditorLanguage::englishUnitedStates ? "Authoring Descriptors" : "Descritores de Autoria";
    if (englishText == "tileSemantics") return language_ == EditorLanguage::englishUnitedStates ? "Tile Semantics" : "Semântica de Tiles";
    if (englishText == "stamps") return language_ == EditorLanguage::englishUnitedStates ? "Stamps" : "Carimbos";
    if (englishText == "presentationEffects") return language_ == EditorLanguage::englishUnitedStates ? "Presentation Effects" : "Efeitos de Apresentação";
    if (englishText == "visualImages") return language_ == EditorLanguage::englishUnitedStates ? "Visual Images" : "Imagens Visuais";
    if (englishText == "staticSprites") return language_ == EditorLanguage::englishUnitedStates ? "Static Sprites" : "Sprites Estáticos";
    if (englishText == "animations") return language_ == EditorLanguage::englishUnitedStates ? "Animations" : "Animações";
    if (englishText == "enemyVisuals") return language_ == EditorLanguage::englishUnitedStates ? "Enemy Visuals" : "Visuais de Inimigos";
    if (englishText == "objectVisuals") return language_ == EditorLanguage::englishUnitedStates ? "Object Visuals" : "Visuais de Objetos";
    if (englishText == "meleeHitbox") return language_ == EditorLanguage::englishUnitedStates ? "Melee" : "Corpo a corpo";
    if (englishText == "projectile") return language_ == EditorLanguage::englishUnitedStates ? "Projectile" : "Projétil";
    if (englishText == "interactToggle") return language_ == EditorLanguage::englishUnitedStates ? "Interact toggle" : "Alternar ao interagir";
    if (englishText == "playerPressure") return language_ == EditorLanguage::englishUnitedStates ? "Player pressure" : "Pressão do jogador";
    if (englishText == "flagSet") return language_ == EditorLanguage::englishUnitedStates ? "Flag set" : "Flag definida";
    if (englishText == "flagNotSet") return language_ == EditorLanguage::englishUnitedStates ? "Flag not set" : "Flag não definida";
    if (englishText == "setFlag") return language_ == EditorLanguage::englishUnitedStates ? "Set flag" : "Definir flag";
    if (englishText == "clearFlag") return language_ == EditorLanguage::englishUnitedStates ? "Clear flag" : "Limpar flag";
    if (englishText == "startQuest") return language_ == EditorLanguage::englishUnitedStates ? "Start quest" : "Iniciar missão";
    if (englishText == "openShop") return language_ == EditorLanguage::englishUnitedStates ? "Open shop" : "Abrir loja";
    if (englishText == "player") return language_ == EditorLanguage::englishUnitedStates ? "Player" : "Jogador";
    if (englishText == "enemy") return language_ == EditorLanguage::englishUnitedStates ? "Enemy" : "Inimigo";
    if (englishText == "environment") return language_ == EditorLanguage::englishUnitedStates ? "Environment" : "Ambiente";
    if (englishText == "neutral") return language_ == EditorLanguage::englishUnitedStates ? "Neutral" : "Neutro";
    if (language_ == EditorLanguage::englishUnitedStates) {
        static constexpr std::array extra{
            std::pair{"relativePath", "Relative Path"}, std::pair{"relativeAssetPath", "Relative Asset Path"},
            std::pair{"imageId", "Image ID"}, std::pair{"visualId", "Visual ID"},
            std::pair{"visualSetId", "Visual Set ID"}, std::pair{"behaviorProfileId", "Behavior Profile ID"},
            std::pair{"rewardProfileId (optional)", "Reward Profile ID (optional)"},
            std::pair{"rewardGrantId", "Reward Grant ID"}, std::pair{"projectileDefinitionId", "Projectile Definition ID"},
            std::pair{"defaultDialogueId", "Default Dialogue ID"}, std::pair{"entryNodeId", "Entry Node ID"},
            std::pair{"nextNodeId (local)", "Next Node ID (local)"}, std::pair{"source rectangle", "Source Rectangle"},
            std::pair{"drawOffset", "Draw Offset"}, std::pair{"durationTicks", "Duration Ticks"},
            std::pair{"cooldownTicks", "Cooldown Ticks"}, std::pair{"totalTicks", "Total Ticks"},
            std::pair{"lifetimeTicks", "Lifetime Ticks"}, std::pair{"speedPixelsPerTick", "Speed (pixels/tick)"},
            std::pair{"movementSpeedSubpixelsPerTick", "Movement Speed (subpixels/tick)"},
            std::pair{"detectionRangePixels", "Detection Range (pixels)"},
            std::pair{"disengageRangePixels", "Disengage Range (pixels)"},
            std::pair{"idleDurationTicks", "Idle Duration (ticks)"},
            std::pair{"wanderDurationTicks", "Wander Duration (ticks)"},
            std::pair{"maximumHealth", "Maximum Health"}, std::pair{"maximumHealthBonus", "Maximum Health Bonus"},
            std::pair{"playerAttackDamageBonus", "Player Attack Damage Bonus"},
            std::pair{"collectionBounds", "Collection Bounds"}, std::pair{"interactable bounds", "Interactable Bounds"},
            std::pair{"container capacity", "Container Capacity"}, std::pair{"destructible health", "Destructible Health"},
            std::pair{"destructible hurtbox", "Destructible Hurtbox"}, std::pair{"destruction duration", "Destruction Duration"},
            std::pair{"door initial state", "Door Initial State"}, std::pair{"door blocking x/y/w/h", "Door Blocking X/Y/W/H"},
            std::pair{"activation mode", "Activation Mode"}, std::pair{"activation bounds", "Activation Bounds"},
            std::pair{"canonicalFacing", "Canonical Facing"}, std::pair{"hitbox", "Hitbox"},
            std::pair{"damage", "Damage"}, std::pair{"knockbackPixels", "Knockback (pixels)"},
            std::pair{"minimumRangePixels", "Minimum Range (pixels)"}, std::pair{"maximumRangePixels", "Maximum Range (pixels)"},
            std::pair{"visualActionId", "Visual Action ID"}, std::pair{"kind", "Kind"},
            std::pair{"consumable", "Consumable"}, std::pair{"equipment", "Equipment"},
            std::pair{"key", "Key"}, std::pair{"misc", "Miscellaneous"}, std::pair{"armor", "Armor"},
            std::pair{"accessory", "Accessory"}, std::pair{"health", "Health"}, std::pair{"currency", "Currency"},
            std::pair{"item", "Item"}, std::pair{"locked", "Locked"}, std::pair{"closed", "Closed"},
            std::pair{"open", "Open"}, std::pair{"talk", "Talk"}, std::pair{"kill", "Kill"},
            std::pair{"pickup", "Pickup"}, std::pair{"enter", "Enter"}, std::pair{"deliver", "Deliver"},
            std::pair{"transient", "Transient"}, std::pair{"persistent", "Persistent"}, std::pair{"down", "Down"},
            std::pair{"up", "Up"}, std::pair{"left", "Left"}, std::pair{"right", "Right"},
            std::pair{"default", "Default"}, std::pair{"side", "Side"}, std::pair{"unknown", "Unknown"},
            std::pair{"unverified", "Unverified"}, std::pair{"unclassified", "Unclassified"},
            std::pair{"itemId", "Item ID"}, std::pair{"payload", "Payload"},
            std::pair{"health/currency amount", "Health/currency Amount"}, std::pair{"equipment slot", "Equipment Slot"},
            std::pair{"chanceBasisPoints", "Chance (basis points)"}, std::pair{"minimumCount", "Minimum Count"},
            std::pair{"maximumCount", "Maximum Count"}, std::pair{"playerBuyPrice optional", "Player Buy Price (optional)"},
            std::pair{"playerSellPrice optional", "Player Sell Price (optional)"},
            std::pair{"cumulativeExperienceThresholds", "Cumulative Experience Thresholds"},
            std::pair{"baseStats.maximumHealth", "Base Stats Maximum Health"}, std::pair{"title", "Title"},
            std::pair{"description", "Description"}, std::pair{"requiredCount", "Required Count"},
            std::pair{"targetId", "Target ID"}, std::pair{"targetId (map scoped)", "Target ID (map scoped)"},
            std::pair{"choice label", "Choice Label"}, std::pair{"choice target", "Choice Target"},
            std::pair{"speaker", "Speaker"}, std::pair{"condition flag", "Condition Flag"},
            std::pair{"condition kind", "Condition Kind"}, std::pair{"action kind", "Action Kind"},
            std::pair{"action target", "Action Target"}, std::pair{"tag", "Tag"},
            std::pair{"tag (selected/new)", "Tag (selected/new)"}, std::pair{"family", "Family"},
            std::pair{"preferredLayer", "Preferred Layer"}, std::pair{"sourceIndex", "Source Index"},
            std::pair{"tilesetId", "Tileset ID"}, std::pair{"tileSize", "Tile Size"}, std::pair{"columns", "Columns"},
            std::pair{"rows", "Rows"}, std::pair{"role", "Role"}, std::pair{"topology", "Topology"},
            std::pair{"confidence", "Confidence"}, std::pair{"overlay mode", "Overlay Mode"},
            std::pair{"overlay layer", "Overlay Layer"}, std::pair{"overlay pulsePeriodTicks", "Overlay Pulse Period (ticks)"},
            std::pair{"shake amplitude", "Shake Amplitude"},
        };
        for (const auto& [source, translated] : extra) if (englishText == source) return translated;
    }
    if (language_ == EditorLanguage::portugueseBrazil) {
        static constexpr std::array extra{
            std::pair{"tilesets", "Conjuntos de Tiles"}, std::pair{"projectiles", "Projéteis"},
            std::pair{"attacks", "Ataques"}, std::pair{"behaviors", "Comportamentos"},
            std::pair{"enemies", "Inimigos"}, std::pair{"items", "Itens"}, std::pair{"objects", "Objetos"},
            std::pair{"pickups", "Coletáveis"}, std::pair{"npcs", "NPCs"}, std::pair{"npcVisuals", "Visuais de NPC"},
            std::pair{"dialogues", "Diálogos"}, std::pair{"quests", "Missões"},
            std::pair{"playerProgressions", "Progressão do Jogador"}, std::pair{"rewardProfiles", "Perfis de Recompensa"},
            std::pair{"rewardGrants", "Recompensas Garantidas"}, std::pair{"shops", "Lojas"},
            std::pair{"authoringDescriptors", "Descritores de Autoria"}, std::pair{"tileSemantics", "Semântica de Tiles"},
            std::pair{"stamps", "Carimbos"}, std::pair{"presentationEffects", "Efeitos de Apresentação"},
            std::pair{"visualImages", "Imagens Visuais"}, std::pair{"staticSprites", "Sprites Estáticos"},
            std::pair{"animations", "Animações"}, std::pair{"enemyVisuals", "Visuais de Inimigos"},
            std::pair{"objectVisuals", "Visuais de Objetos"},
            std::pair{"CONTENT", "CONTEÚDO"}, std::pair{"MAP", "MAPA"}, std::pair{"CONTENT MODE", "MODO CONTEÚDO"},
            std::pair{"MAP MODE", "MODO MAPA"}, std::pair{"NEW", "NOVO"}, std::pair{"DELETE", "REMOVER"},
            std::pair{"MAPS", "MAPAS"}, std::pair{"SET ENTRY", "DEFINIR ENTRADA"},
            std::pair{"REMOVE MAP", "REMOVER MAPA"}, std::pair{"CYCLE TARGET MAP", "ALTERNAR MAPA ALVO"},
            std::pair{"CYCLE TARGET SPAWN", "ALTERNAR SPAWN ALVO"}, std::pair{"GO TO TARGET", "IR PARA O ALVO"},
            std::pair{"DEL", "REMOVER"}, std::pair{"RECTANGLE", "RETÂNGULO"}, std::pair{"RECT", "RETÂNGULO"},
            std::pair{"SEM", "SEM"}, std::pair{"ENT", "ENT"}, std::pair{"TILE", "TILE"},
            std::pair{"PICK", "CONTA-GOTAS"}, std::pair{"COLL +", "COLISÃO +"}, std::pair{"COLL -", "COLISÃO -"},
            std::pair{"COLL R+", "COLISÃO RET +"}, std::pair{"COLL R-", "COLISÃO RET -"},
            std::pair{"COLL F+", "COLISÃO PREENCHER +"}, std::pair{"COLL F-", "COLISÃO PREENCHER -"},
            std::pair{"ENTITY", "ENTIDADE"}, std::pair{"STAMP", "CARIMBO"}, std::pair{"SELECT TILES", "SELECIONAR TILES"},
            std::pair{"Player Spawn", "Spawn do Jogador"}, std::pair{"Map Link", "Link de Mapa"},
            std::pair{"Region", "Região"}, std::pair{"STOP PLAYTEST", "PARAR PLAYTEST"}, std::pair{"PLAYTEST", "PLAYTEST"},
            std::pair{"SEMANTIC TILES", "TILES SEMÂNTICOS"}, std::pair{"RAW TILES", "TILES BRUTOS"},
            std::pair{"TILESET", "CONJUNTO DE TILES"}, std::pair{"SOURCE ON", "ORIGEM ATIVADA"},
            std::pair{"SOURCE OFF", "ORIGEM DESATIVADA"}, std::pair{"MARKERS", "MARCADORES"},
            std::pair{"FRAMES (authored order)", "QUADROS (ordem autoral)"}, std::pair{"ACTIONS", "AÇÕES"},
            std::pair{"SELECTED ACTION", "AÇÃO SELECIONADA"}, std::pair{"SELECTED OPTIONAL STATE", "ESTADO OPCIONAL SELECIONADO"},
            std::pair{"IDLE (required fallback)", "PARADO (fallback obrigatório)"}, std::pair{"IDLE directional (optional)", "PARADO direcional (opcional)"},
            std::pair{"OPTIONAL STATES", "ESTADOS OPCIONAIS"}, std::pair{"NODES", "NÓS"}, std::pair{"PAGES", "PÁGINAS"},
            std::pair{"CHOICES", "ESCOLHAS"}, std::pair{"CONDITIONS", "CONDIÇÕES"}, std::pair{"OBJECTIVES", "OBJETIVOS"},
            std::pair{"TAGS", "TAGS"}, std::pair{"TIMELINE (fixed gameplay ticks)", "LINHA DO TEMPO (ticks de gameplay)"},
            std::pair{"WORLD RULES", "REGRAS DO MUNDO"}, std::pair{"TRIGGER", "GATILHO"},
            std::pair{"VALIDATION", "VALIDAÇÃO"}, std::pair{"SELECTED EVENT TICK", "TICK DO EVENTO SELECIONADO"},
            std::pair{"SELECTED EVENT KIND", "TIPO DO EVENTO SELECIONADO"}, std::pair{"NEW DEFINITION", "NOVA DEFINIÇÃO"},
            std::pair{"NEW MAP", "NOVO MAPA"}, std::pair{"CREATE DEFINITION", "CRIAR DEFINIÇÃO"},
            std::pair{"CREATE CONTENT FILE", "CRIAR ARQUIVO DE CONTEÚDO"}, std::pair{"OPEN DEFINITION", "ABRIR DEFINIÇÃO"},
            std::pair{"OPEN EFFECT", "ABRIR EFEITO"}, std::pair{"OPEN SEMANTIC", "ABRIR SEMÂNTICA"},
            std::pair{"SHOW IN MAP PALETTE", "MOSTRAR NA PALETA DO MAPA"}, std::pair{"USE SEMANTIC IN MAP", "USAR SEMÂNTICA NO MAPA"},
            std::pair{"USE STAMP IN MAP", "USAR CARIMBO NO MAPA"}, std::pair{"SET ENVIRONMENT EFFECT", "DEFINIR EFEITO DE AMBIENTE"},
            std::pair{"CLEAR ENVIRONMENT EFFECT", "LIMPAR EFEITO DE AMBIENTE"}, std::pair{"CYCLE ENVIRONMENT EFFECT", "ALTERNAR EFEITO DE AMBIENTE"},
            std::pair{"SET REWARD", "DEFINIR RECOMPENSA"}, std::pair{"CYCLE REWARD", "ALTERNAR RECOMPENSA"},
            std::pair{"ADD ACT", "ADICIONAR AÇÃO"}, std::pair{"ADD COND", "ADICIONAR CONDIÇÃO"}, std::pair{"ADD CELL", "ADICIONAR CÉLULA"},
            std::pair{"ADD CELLS", "ADICIONAR CÉLULAS"}, std::pair{"ADD ITEM GRANT", "ADICIONAR ITEM DE RECOMPENSA"},
            std::pair{"REMOVE ITEM", "REMOVER ITEM"}, std::pair{"REMOVE LOOT", "REMOVER DROP"}, std::pair{"REMOVE OFFER", "REMOVER OFERTA"},
            std::pair{"REMOVE TAG", "REMOVER TAG"}, std::pair{"REMOVE THRESHOLD", "REMOVER LIMIAR"},
            std::pair{"EQUIPMENT ENABLED", "EQUIPAMENTO ATIVADO"}, std::pair{"EQUIPMENT DISABLED", "EQUIPAMENTO DESATIVADO"},
            std::pair{"USE ENABLED", "USO ATIVADO"}, std::pair{"USE DISABLED", "USO DESATIVADO"},
            std::pair{"INTERACTION ENABLED", "INTERAÇÃO ATIVADA"}, std::pair{"INTERACTION DISABLED", "INTERAÇÃO DESATIVADA"},
            std::pair{"INTERACTABLE ON", "INTERATIVO ATIVADO"}, std::pair{"INTERACTABLE OFF", "INTERATIVO DESATIVADO"},
            std::pair{"CONTAINER ON", "CONTÊINER ATIVADO"}, std::pair{"CONTAINER OFF", "CONTÊINER DESATIVADO"},
            std::pair{"DESTRUCTIBLE ON", "DESTRUTÍVEL ATIVADO"}, std::pair{"DESTRUCTIBLE OFF", "DESTRUTÍVEL DESATIVADO"},
            std::pair{"DOOR ON", "PORTA ATIVADA"}, std::pair{"DOOR OFF", "PORTA DESATIVADA"},
            std::pair{"ACTIVATION ON", "ATIVAÇÃO ATIVADA"}, std::pair{"ACTIVATION OFF", "ATIVAÇÃO DESATIVADA"},
            std::pair{"BANK ACCESS ON", "ACESSO AO BANCO ATIVADO"}, std::pair{"BANK ACCESS OFF", "ACESSO AO BANCO DESATIVADO"},
            std::pair{"CAMERA SHAKE ON", "TREMOR DE CÂMERA ATIVADO"}, std::pair{"CAMERA SHAKE OFF", "TREMOR DE CÂMERA DESATIVADO"},
            std::pair{"OVERLAY ON", "SOBREPOSIÇÃO ATIVADA"}, std::pair{"OVERLAY OFF", "SOBREPOSIÇÃO DESATIVADA"},
            std::pair{"VISION MASK ON", "MÁSCARA DE VISÃO ATIVADA"}, std::pair{"VISION MASK OFF", "MÁSCARA DE VISÃO DESATIVADA"},
            std::pair{"FADE ON", "ESMAECIMENTO ATIVADO"}, std::pair{"FADE OFF", "ESMAECIMENTO DESATIVADO"},
            std::pair{"APPLY", "APLICAR"}, std::pair{"CANCEL", "CANCELAR"}, std::pair{"OK", "OK"},
            std::pair{"ON", "ATIVADO"}, std::pair{"OFF", "DESATIVADO"}, std::pair{"DOWN", "BAIXO"},
            std::pair{"UP", "CIMA"}, std::pair{"LEFT", "ESQUERDA"}, std::pair{"RIGHT", "DIREITA"},
            std::pair{"IDLE", "PARADO"}, std::pair{"MOVE", "MOVER"}, std::pair{"HURT", "FERIDO"},
            std::pair{"DEATH", "MORTE"}, std::pair{"DEAD", "MORTO"}, std::pair{"OPEN", "ABRIR"},
            std::pair{"CLOSED", "FECHADO"}, std::pair{"OPENED", "ABERTO"}, std::pair{"LOCKED", "TRANCADO"},
            std::pair{"HEIGHT", "ALTURA"}, std::pair{"WIDTH", "LARGURA"}, std::pair{"VALUE", "VALOR"},
            std::pair{"ADD FRAME", "ADICIONAR QUADRO"}, std::pair{"ADD MARKER", "ADICIONAR MARCADOR"},
            std::pair{"ADD ACTION", "ADICIONAR AÇÃO"}, std::pair{"ADD ATTACK", "ADICIONAR ATAQUE"},
            std::pair{"ADD EVENT", "ADICIONAR EVENTO"}, std::pair{"ADD NODE", "ADICIONAR NÓ"},
            std::pair{"ADD PAGE", "ADICIONAR PÁGINA"}, std::pair{"ADD CHOICE", "ADICIONAR ESCOLHA"},
            std::pair{"ADD TAG", "ADICIONAR TAG"}, std::pair{"ADD OFFER", "ADICIONAR OFERTA"},
            std::pair{"ADD LOOT ENTRY", "ADICIONAR DROP"}, std::pair{"ADD THRESHOLD", "ADICIONAR LIMIAR"},
            std::pair{"REMOVE FRAME", "REMOVER QUADRO"}, std::pair{"REMOVE MARKER", "REMOVER MARCADOR"},
            std::pair{"MOVE FIRST DOWN", "MOVER O PRIMEIRO PARA BAIXO"}, std::pair{"MOVE UP", "MOVER PARA CIMA"},
            std::pair{"MOVE DOWN", "MOVER PARA BAIXO"}, std::pair{"LOOP ON", "REPETIÇÃO ATIVADA"},
            std::pair{"LOOP OFF", "REPETIÇÃO DESATIVADA"}, std::pair{"FULL IMAGE", "IMAGEM INTEIRA"},
            std::pair{"GRID ON", "GRADE ATIVADA"}, std::pair{"GRID OFF", "GRADE DESATIVADA"},
            std::pair{"PREVIEW", "PRÉ-VISUALIZAÇÃO"}, std::pair{"PLAY", "REPRODUZIR"}, std::pair{"PAUSE", "PAUSAR"},
            std::pair{"RESTART", "REINICIAR"}, std::pair{"PREVIOUS FRAME", "QUADRO ANTERIOR"},
            std::pair{"NEXT FRAME", "PRÓXIMO QUADRO"}, std::pair{"SOURCE", "ORIGEM"},
            std::pair{"ANCHOR", "ÂNCORA"}, std::pair{"DRAW OFFSET", "DESLOCAMENTO DE DESENHO"},
            std::pair{"DURATION", "DURAÇÃO"}, std::pair{"PRIORITY", "PRIORIDADE"},
            std::pair{"VISUAL", "VISUAL"}, std::pair{"BEHAVIOR", "COMPORTAMENTO"}, std::pair{"FACTION", "FACÇÃO"},
            std::pair{"HEALTH", "VIDA"}, std::pair{"ATTACKS", "ATAQUES"}, std::pair{"REWARD", "RECOMPENSA"},
            std::pair{"DIALOGUE", "DIÁLOGO"}, std::pair{"QUEST", "MISSÃO"}, std::pair{"OBJECTIVE", "OBJETIVO"},
            std::pair{"EXPERIENCE", "EXPERIÊNCIA"}, std::pair{"GOLD", "OURO"}, std::pair{"QUANTITY", "QUANTIDADE"},
            std::pair{"BUY PRICE", "PREÇO DE COMPRA"}, std::pair{"SELL PRICE", "PREÇO DE VENDA"},
            std::pair{"ASSET VALIDATION", "VALIDAÇÃO DE ASSETS"}, std::pair{"CONTENT VALIDATION", "VALIDAÇÃO DE CONTEÚDO"},
            std::pair{"CONTENT INVALID", "CONTEÚDO INVÁLIDO"}, std::pair{"CONTENT OK", "CONTEÚDO OK"},
            std::pair{"READ-ONLY", "SOMENTE LEITURA"}, std::pair{"Builtin content - read only", "Conteúdo integrado - somente leitura"},
            std::pair{"Dungeon authored/runtime maps (*.uworld;*.umap;*.dmap)", "Mapas authored/runtime (*.uworld;*.umap;*.dmap)"},
            std::pair{"All files", "Todos os arquivos"},
            std::pair{"displayName", "nome de exibição"}, std::pair{"relativePath", "caminho relativo"},
            std::pair{"relativeAssetPath", "caminho relativo do asset"}, std::pair{"imageId", "ID da imagem"},
            std::pair{"visualId", "ID visual"}, std::pair{"visualId (StaticSprite)", "ID visual (Sprite Estático)"},
            std::pair{"visualSetId", "ID do conjunto visual"}, std::pair{"behaviorProfileId", "ID do perfil de comportamento"},
            std::pair{"rewardProfileId (optional)", "ID do perfil de recompensa (opcional)"},
            std::pair{"rewardGrantId", "ID da recompensa garantida"}, std::pair{"projectileDefinitionId", "ID da definição de projétil"},
            std::pair{"defaultDialogueId", "ID do diálogo padrão"}, std::pair{"entryNodeId", "ID do nó inicial"},
            std::pair{"nextNodeId (local)", "ID do próximo nó (local)"}, std::pair{"source rectangle", "retângulo de origem"},
            std::pair{"anchor must contain two comma-separated integers", "a âncora deve conter dois inteiros separados por vírgula"},
            std::pair{"anchor", "âncora"}, std::pair{"drawOffset", "deslocamento de desenho"},
            std::pair{"draw offset", "deslocamento de desenho"}, std::pair{"durationTicks", "ticks de duração"},
            std::pair{"cooldownTicks", "ticks de recarga"}, std::pair{"totalTicks", "ticks totais"},
            std::pair{"lifetimeTicks", "ticks de duração"}, std::pair{"speedPixelsPerTick", "velocidade (pixels por tick)"},
            std::pair{"movementSpeedSubpixelsPerTick", "velocidade de movimento (subpixels por tick)"},
            std::pair{"detectionRangePixels", "alcance de detecção (pixels)"}, std::pair{"disengageRangePixels", "alcance de desistência (pixels)"},
            std::pair{"idleDurationTicks", "duração parado (ticks)"}, std::pair{"wanderDurationTicks", "duração vagando (ticks)"},
            std::pair{"maximumHealth", "vida máxima"}, std::pair{"maximumHealthBonus", "bônus de vida máxima"},
            std::pair{"playerAttackDamageBonus", "bônus de dano do jogador"}, std::pair{"stackLimit", "limite da pilha"},
            std::pair{"collectionBounds", "limites de coleta"}, std::pair{"interactable bounds", "limites de interação"},
            std::pair{"container capacity", "capacidade do contêiner"}, std::pair{"destructible health", "vida destrutível"},
            std::pair{"destructible hurtbox", "área de dano destrutível"}, std::pair{"destruction duration", "duração da destruição"},
            std::pair{"door initial state", "estado inicial da porta"}, std::pair{"door blocking x/y/w/h", "bloqueio da porta x/y/l/a"},
            std::pair{"activation mode", "modo de ativação"}, std::pair{"activation bounds", "limites de ativação"},
            std::pair{"faction", "facção"}, std::pair{"canonicalFacing", "direção canônica"}, std::pair{"hitbox", "área de impacto"},
            std::pair{"damage", "dano"}, std::pair{"knockbackPixels", "recuo (pixels)"}, std::pair{"minimumRangePixels", "alcance mínimo (pixels)"},
            std::pair{"maximumRangePixels", "alcance máximo (pixels)"}, std::pair{"visualActionId", "ID da ação visual"},
            std::pair{"kind", "tipo"}, std::pair{"category", "categoria"}, std::pair{"quantity", "quantidade"},
            std::pair{"meleeHitbox", "corpo a corpo"}, std::pair{"projectile", "projétil"},
            std::pair{"player", "jogador"}, std::pair{"enemy", "inimigo"}, std::pair{"environment", "ambiente"},
            std::pair{"neutral", "neutro"}, std::pair{"consumable", "consumível"}, std::pair{"equipment", "equipamento"},
            std::pair{"key", "chave"}, std::pair{"misc", "diverso"}, std::pair{"armor", "armadura"},
            std::pair{"accessory", "acessório"}, std::pair{"health", "vida"}, std::pair{"currency", "moeda"},
            std::pair{"item", "item"}, std::pair{"interactToggle", "alternar ao interagir"},
            std::pair{"playerPressure", "pressão do jogador"}, std::pair{"locked", "trancado"},
            std::pair{"closed", "fechado"}, std::pair{"open", "aberto"}, std::pair{"flagSet", "flag definida"},
            std::pair{"flagNotSet", "flag não definida"}, std::pair{"setFlag", "definir flag"},
            std::pair{"clearFlag", "limpar flag"}, std::pair{"startQuest", "iniciar missão"},
            std::pair{"openShop", "abrir loja"}, std::pair{"talk", "conversar"}, std::pair{"kill", "derrotar"},
            std::pair{"pickup", "coletar"}, std::pair{"enter", "entrar"}, std::pair{"deliver", "entregar"},
            std::pair{"transient", "transitório"}, std::pair{"persistent", "persistente"},
            std::pair{"down", "baixo"}, std::pair{"up", "cima"}, std::pair{"left", "esquerda"}, std::pair{"right", "direita"},
            std::pair{"default", "padrão"}, std::pair{"side", "lateral"}, std::pair{"unknown", "desconhecido"},
            std::pair{"unverified", "não verificado"}, std::pair{"unclassified", "não classificado"},
            std::pair{"itemId", "ID do item"}, std::pair{"payload", "carga"}, std::pair{"health/currency amount", "quantidade de vida/moedas"},
            std::pair{"equipment slot", "espaço de equipamento"}, std::pair{"experience", "experiência"}, std::pair{"gold", "ouro"},
            std::pair{"chanceBasisPoints", "chance (pontos-base)"}, std::pair{"minimumCount", "quantidade mínima"}, std::pair{"maximumCount", "quantidade máxima"},
            std::pair{"playerBuyPrice optional", "preço de compra do jogador (opcional)"}, std::pair{"playerSellPrice optional", "preço de venda do jogador (opcional)"},
            std::pair{"cumulativeExperienceThresholds", "limiares cumulativos de experiência"}, std::pair{"baseStats.maximumHealth", "vida máxima dos atributos base"},
            std::pair{"title", "título"}, std::pair{"description", "descrição"}, std::pair{"requiredCount", "quantidade necessária"},
            std::pair{"targetId", "ID do alvo"}, std::pair{"targetId (map scoped)", "ID do alvo (escopo do mapa)"},
            std::pair{"choice label", "texto da escolha"}, std::pair{"choice target", "alvo da escolha"}, std::pair{"speaker", "personagem"},
            std::pair{"condition flag", "flag da condição"}, std::pair{"condition kind", "tipo da condição"}, std::pair{"action kind", "tipo da ação"},
            std::pair{"action target", "alvo da ação"}, std::pair{"tag", "tag"}, std::pair{"tag (selected/new)", "tag (selecionada/nova)"},
            std::pair{"family", "família"}, std::pair{"preferredLayer", "camada preferida"}, std::pair{"sourceIndex", "índice de origem"},
            std::pair{"tilesetId", "ID do conjunto de tiles"}, std::pair{"tileSize", "tamanho do tile"}, std::pair{"columns", "colunas"}, std::pair{"rows", "linhas"},
            std::pair{"role", "função"}, std::pair{"topology", "topologia"}, std::pair{"confidence", "confiança"},
            std::pair{"overlay mode", "modo da sobreposição"}, std::pair{"overlay layer", "camada da sobreposição"},
            std::pair{"overlay pulsePeriodTicks", "período do pulso da sobreposição (ticks)"}, std::pair{"shake amplitude", "amplitude do tremor"},
        };
        for (const auto& [source, translated] : extra) if (englishText == source) return translated;
        if (englishText.starts_with("Frame ")) return "Quadro " + std::string(englishText.substr(6));
        if (englishText.starts_with("Marker ")) return "Marcador " + std::string(englishText.substr(7));
        if (englishText.starts_with("CONTENT: ")) return "CONTEÚDO: " + std::string(englishText.substr(9));
        if (englishText.starts_with("ASSET: ")) return "ASSET: " + std::string(englishText.substr(7));
        if (englishText.starts_with("Font unavailable; authoring shell remains active: ")) return
            "Fonte indisponível; o editor continuará ativo: " + std::string(englishText.substr(50));
        if (englishText.starts_with("Content workspace invalid")) return
            "Workspace de conteúdo inválido" + std::string(englishText.substr(25));
        if (englishText.starts_with("Playtest unavailable")) return
            "Playtest indisponível" + std::string(englishText.substr(20));
        if (englishText.starts_with("Source:")) return "Origem:" + std::string(englishText.substr(7));
        if (englishText.starts_with("Map mode")) return "Modo mapa" + std::string(englishText.substr(8));
        if (englishText.starts_with("Content mode")) return "Modo conteúdo" + std::string(englishText.substr(12));
        if (englishText.starts_with("World ")) return "Mundo " + std::string(englishText.substr(6));
        if (englishText.starts_with("Layer ")) return "Camada " + std::string(englishText.substr(6));
        if (englishText.starts_with("Region ")) return "Região " + std::string(englishText.substr(7));
        if (englishText.starts_with("Choice ")) return "Escolha " + std::string(englishText.substr(7));
        if (englishText.starts_with("Page ")) return "Página " + std::string(englishText.substr(5));
        if (englishText.starts_with("Cell ")) return "Célula " + std::string(englishText.substr(5));
        if (englishText.starts_with("Effect ")) return "Efeito " + std::string(englishText.substr(7));
        if (englishText.starts_with("Semantic: ")) return "Semântica: " + std::string(englishText.substr(10));
        if (englishText.starts_with("TILE: ")) return "TILE: " + std::string(englishText.substr(6));
        if (englishText.starts_with("items: ")) return "itens: " + std::string(englishText.substr(7));
        if (englishText.starts_with("offers: ")) return "ofertas: " + std::string(englishText.substr(8));
        if (englishText.starts_with("loot entries: ")) return "drops: " + std::string(englishText.substr(13));
        if (englishText.starts_with("participants ")) return "participantes " + std::string(englishText.substr(13));
        if (englishText.starts_with("Definition created")) return "Definição criada" + std::string(englishText.substr(19));
        if (englishText.starts_with("Definition updated")) return "Definição atualizada" + std::string(englishText.substr(19));
        if (englishText.starts_with("Definition deleted")) return "Definição removida" + std::string(englishText.substr(19));
    }
    return std::string(englishText);
}

bool EditorLocalization::catalogComplete(EditorLanguage language) noexcept {
    for (const auto& entry : entries) {
        const auto value = language == EditorLanguage::englishUnitedStates ? entry.english : entry.portuguese;
        if (entry.id != EditorTextId::unknown && value.empty()) return false;
    }
    return true;
}

const std::vector<EditorTextId>& EditorLocalization::allTextIds() noexcept {
    static const std::vector<EditorTextId> ids{
        EditorTextId::file, EditorTextId::edit, EditorTextId::view, EditorTextId::settings,
        EditorTextId::language, EditorTextId::newMap, EditorTextId::openMap, EditorTextId::save,
        EditorTextId::saveAs, EditorTextId::saveAll, EditorTextId::exit, EditorTextId::undo,
        EditorTextId::redo, EditorTextId::mapMode, EditorTextId::contentMode, EditorTextId::grid,
        EditorTextId::frameMap, EditorTextId::validateWorkspace, EditorTextId::tiles,
        EditorTextId::semantics, EditorTextId::stamps, EditorTextId::entities, EditorTextId::layers,
        EditorTextId::rules, EditorTextId::encounters, EditorTextId::select, EditorTextId::pencil,
        EditorTextId::erase, EditorTextId::rectangle, EditorTextId::fill, EditorTextId::eyedropper,
        EditorTextId::collision, EditorTextId::region, EditorTextId::properties, EditorTextId::categories,
        EditorTextId::definitions, EditorTextId::inspector, EditorTextId::status, EditorTextId::add,
        EditorTextId::remove, EditorTextId::moveUp, EditorTextId::moveDown, EditorTextId::open,
        EditorTextId::create, EditorTextId::update, EditorTextId::clear, EditorTextId::set,
        EditorTextId::placeInMap, EditorTextId::findInMap, EditorTextId::contentStudio,
        EditorTextId::mapMaker, EditorTextId::builtinContent, EditorTextId::contentWorkspace,
        EditorTextId::workspaceInvalid, EditorTextId::compileUnavailable, EditorTextId::playtestUnavailable,
        EditorTextId::id, EditorTextId::displayName, EditorTextId::visual, EditorTextId::behavior,
        EditorTextId::faction, EditorTextId::health, EditorTextId::movementSpeed, EditorTextId::collisionBox,
        EditorTextId::hurtbox, EditorTextId::reward, EditorTextId::attacks, EditorTextId::stackLimit,
        EditorTextId::category, EditorTextId::quantity, EditorTextId::dialogue, EditorTextId::quest,
        EditorTextId::objective, EditorTextId::gold, EditorTextId::experience, EditorTextId::buyPrice,
        EditorTextId::sellPrice, EditorTextId::duration, EditorTextId::priority, EditorTextId::animation,
        EditorTextId::source, EditorTextId::anchor, EditorTextId::frame, EditorTextId::loop,
        EditorTextId::image, EditorTextId::tilesets, EditorTextId::projectiles, EditorTextId::attacksCategory,
        EditorTextId::behaviors, EditorTextId::enemies, EditorTextId::items, EditorTextId::objects,
        EditorTextId::pickups, EditorTextId::npcVisuals, EditorTextId::npcs, EditorTextId::dialogues,
        EditorTextId::quests, EditorTextId::playerProgressions, EditorTextId::rewardProfiles,
        EditorTextId::rewardGrants, EditorTextId::shops, EditorTextId::authoringDescriptors,
        EditorTextId::tileSemantics, EditorTextId::visualImages, EditorTextId::staticSprites,
        EditorTextId::animations, EditorTextId::enemyVisuals, EditorTextId::objectVisuals,
        EditorTextId::missingDefinition, EditorTextId::readOnlyCategory, EditorTextId::fontUnavailable,
        EditorTextId::initializationError, EditorTextId::languagePortugueseBrazil,
        EditorTextId::languageEnglish, EditorTextId::maps, EditorTextId::newProject,
        EditorTextId::importMap, EditorTextId::removeMap, EditorTextId::entryMap,
        EditorTextId::setEntry, EditorTextId::targetMap, EditorTextId::targetSpawn,
        EditorTextId::goToTarget, EditorTextId::openProject, EditorTextId::saveProject,
        EditorTextId::saveProjectAs, EditorTextId::validateProject,
        EditorTextId::projectValid, EditorTextId::projectInvalid,
        EditorTextId::findInProject,
    };
    return ids;
}

} // namespace underworld::editor
