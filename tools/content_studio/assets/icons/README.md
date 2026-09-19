# Content Studio — Ícones de UI

Biblioteca de ícones: **Tabler Icons** (apenas os SVGs utilizados).
Licença: **MIT** — cópia em [`../licenses/TABLER_ICONS_LICENSE.txt`](../licenses/TABLER_ICONS_LICENSE.txt).
Origem: release oficial [tabler/tabler-icons v3.47.0](https://github.com/tabler/tabler-icons/releases/tag/v3.47.0) (`icons/outline`).
Uso: exclusivamente a UI do Content Studio. Thumbnails de conteúdo do jogo
(sprites, tiles, objetos, portas, itens, animações, player) continuam sendo
assets autorais reais e **não** devem ser substituídos por ícones.

## Layout

```text
tools/content_studio/
├── assets/
│   ├── icons/tabler/       # SVGs Tabler (outline) versionados, só os usados
│   └── licenses/           # licenças de bibliotecas de terceiros
└── ui/icon_registry.py     # acesso centralizado aos ícones
```

Os SVGs Tabler usam `stroke="currentColor"`: um `QIconEngine` no registry
resolve essa cor pela paleta do aplicativo no momento do desenho, então os
ícones acompanham o tema claro/escuro (menu Exibir > Tema) automaticamente,
inclusive o modo "Seguir o sistema".

## Como usar

Não referencie caminhos de SVG diretamente nos widgets. Peça por nome
semântico ao registry:

```python
from .icon_registry import icon

self.actions["save"].setIcon(icon("save"))
button.setIcon(icon("delete"))
```

O registry:

- resolve o nome semântico para o arquivo Tabler (`save -> device-floppy.svg`);
- localiza os assets a partir do próprio módulo, independente do CWD;
- faz cache de `QIcon` por nome (menus/toolbars/inspetores reconstruídos
  não reprocessam o SVG);
- nome desconhecido ou arquivo ausente degrada para `QIcon` nulo
  (controle fica só com texto) e emite um aviso único em `stderr`;
- tamanhos reutilizáveis em `IconSize` (`SMALL` 16, `NORMAL` 20,
  `TOOLBAR` 24, `LARGE` 32) — use o padrão do Qt quando ele for suficiente.

## Como registrar um novo ícone

1. Escolha o SVG no release oficial do Tabler Icons (`icons/outline`);
2. Copie **apenas** o SVG usado para `assets/icons/tabler/<nome-tabler>.svg`;
3. Adicione a entrada `"<semantico>": "<nome-tabler>.svg"` em
   `TABLER_ICONS` em `ui/icon_registry.py`;
4. Use `icon("<semantico>")` no widget — nunca o caminho do arquivo;
5. Rode `python -m unittest tools.content_studio.tests.test_icon_registry -v`:
   os testes garantem que cada nome resolve para um arquivo existente,
   que não há SVG versionado fora do registry e que cada ícone renderiza.

Regra: não misturar outra biblioteca visual (Font Awesome, Material, Lucide
etc.) — Tabler Icons é o sistema de ícones da UI do Studio.
