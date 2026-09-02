#pragma once

namespace underworld::platform {

// Temporary Phase 3 tool controls. This is deliberately not gameplay InputState.
struct DebugInputState final {
    bool cameraLeft{};
    bool cameraRight{};
    bool cameraUp{};
    bool cameraDown{};
    bool bodyLeft{};
    bool bodyRight{};
    bool bodyUp{};
    bool bodyDown{};
    bool toggleCollisionPressed{};
};

} // namespace underworld::platform
