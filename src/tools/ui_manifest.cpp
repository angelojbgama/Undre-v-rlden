#include "game/ui/ui_screens.h"

#include <iostream>

// Emits the deterministic UI registry manifest (components, anchors, bindings,
// actions) as JSON on stdout. The Content Studio UI Composer consumes it for
// its pickers, and a Studio test asserts it matches the Python mirror.
int main() {
    std::cout << underworld::game::ui::emitUiManifestJson();
    return 0;
}
