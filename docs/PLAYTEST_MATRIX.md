# Matriz de cobertura de playtest

Esta matriz descreve o incremento portátil do Block D. `Automated Playtest` significa
um cenário executado por `playtest_runner`; `Screenshot` significa que a sessão gera
um artefato do framebuffer lógico.

| Feature | Unit Test | Integration Test | Automated Playtest | Screenshot | Manual Smoke |
|---|---|---|---|---|---|
| Startup / authored map | yes | yes | `startup` | yes | pending Windows |
| Movement | yes | yes | `movement` | yes | pending Windows |
| Collision | yes | yes | `collision` | yes | pending Windows |
| Melee combat | yes | yes | `melee_combat` | yes | pending Windows |
| Ranged combat | yes | yes | `ranged_combat` | yes | pending Windows |
| Pickups | yes | yes | `pickup_money`, `pickup_heart`, `pickup_life_potion` | yes | pending Windows |
| Inventory overlay | yes | yes | `inventory`, `quick_slot`, `inventory_navigation` | yes | pending Windows |
| Chest / crate | yes | yes | `chest`, `crate` | yes | pending Windows |
| Map transitions | yes | yes | `map_01_to_02`, `map_02_to_01`, `map_02_to_03`, `map_03_to_02` | yes | pending Windows |
| Save / load | yes | yes | `save_load` | yes | pending Windows |
| NPC dialogue open | yes | yes | `npc_dialogue`, `dialogue_pagination`, `dialogue_choice`, `dialogue_flag` | yes | pending Windows |
| Quest progression | yes | yes | startup smoke only; public start command pending | yes | pending Windows |

O runner usa os sistemas reais de gameplay e renderização, mas no Linux sem assets
locais usa um `ImageDecoder` sintético injetado. Isso valida fluxo, estado e
framebuffer; a aparência dos assets licenciados exige o smoke Windows correspondente.
F12/manual audit e build Linux dedicado pertencem aos blocos E e F.
