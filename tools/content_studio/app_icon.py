"""CLI: ``python -m tools.content_studio.app_icon``.

Regenerates ``build/game_icon.ico`` (and the ``game.rc`` resource script when
missing) from a source PNG, so ``build.bat`` can embed the icon into
``game.exe``. The Content Studio tools menu calls the same service.
"""

from __future__ import annotations

from .services.app_icon_service import main

if __name__ == "__main__":
    raise SystemExit(main())
