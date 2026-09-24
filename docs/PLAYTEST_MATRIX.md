# Matriz de cobertura de playtest

Esta matriz descreve o incremento portátil do Block D. `Automated Playtest` significa
um cenário executado por `playtest_runner`; `Screenshot` significa que a sessão gera
um artefato do framebuffer lógico. Todos os cenários abaixo fazem parte do `--all`
e estão verdes na suíte Linux/Docker (`./docker/build_linux.sh playtest`).

Salas de inicialização por cenário: cenários de mecânica (containers, pickups,
diálogo) bootam no santuário (`map.6`, sala segura); cenários de combate e
travessia bootam no hub hostil (`map.1`).

| Feature | Unit Test | Integration Test | Automated Playtest | Screenshot | Manual Smoke |
|---|---|---|---|---|---|
| Startup / authored map | yes | yes | `startup` | yes | pending Windows |
| Movement | yes | yes | `movement` | yes | pending Windows |
| Collision | yes | yes | `collision` | yes | pending Windows |
| Melee combat | yes | yes | `melee_combat` | yes | pending Windows |
| Ranged combat | yes | yes | `ranged_combat` | yes | pending Windows |
| Pickups | yes | yes | `pickup_money`, `pickup_heart`, `pickup_life_potion` (map.6) | yes | pending Windows |
| Inventory overlay | yes | yes | `inventory`, `quick_slot`, `inventory_navigation` | yes | pending Windows |
| Chest / crate | yes | yes | `chest`, `crate` (map.6) | yes | pending Windows |
| Save / load | yes | yes | `save_load` | yes | pending Windows |
| NPC dialogue open | yes | yes | `npc_dialogue` (map.6) | yes | pending Windows |
| Quest / rewards / loot | yes | yes | `rewards_loot` | yes | pending Windows |
| Title shell / start picker | yes | yes | `title_start` | yes | pending Windows |
| Game over / retry | yes | yes | `game_over` | yes | pending Windows |
| Presentation effects | yes | yes | `presentation_feedback` | yes | pending Windows |
| World logic (fixtures sintéticas) | yes | yes | `world_logic` | yes | pending Windows |
| Interactive world (fixtures sintéticas) | yes | yes | `interactive_world` | yes | pending Windows |
| Visual content external | yes | yes | `visual_content` | yes | pending Windows |
| Slimes / hazards / TNT (runtime) | yes (session test: TNT chain; decode/validate das três mecânicas) | yes | cobertos indiretamente pelas salas 2–4 via `rewards_loot`/`melee_combat` | yes | pending Windows |

Cenários por nome (`--scenario`) que exigem mapas retirados de produção
(`map_01_to_02` etc. e `quest`) permanecem registrados no runner e só rodam
quando mapas com esses ids existirem novamente.

O runner usa os sistemas reais de gameplay e renderização, mas no Linux sem assets
locais usa um `ImageDecoder` sintético injetado. Isso valida fluxo, estado e
framebuffer; a aparência dos assets licenciados exige o smoke Windows correspondente.
F12/manual audit e build Linux dedicado pertencem aos blocos E e F.
