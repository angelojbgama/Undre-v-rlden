#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include "editor/editor_app.h"
#include "editor/editor_launch.h"
#include "editor/editor_preferences.h"
#include "engine/core/utf8.h"
#include "engine/platform/win32/win32_image_decoder.h"
#include "game/content/content_source.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace underworld::editor {
namespace {
constexpr wchar_t className[] = L"DungeonUnderworldMapMakerWindow";
enum MenuId : UINT {
    fileNew = 1001, fileOpen, fileSave, fileSaveAs, fileSaveAll, fileExit,
    editUndo, editRedo, viewGrid, viewFrame, viewMap, viewContent, viewValidate,
    settingsLanguagePortuguese, settingsLanguageEnglish
};
constexpr UINT_PTR autosaveTimerId = 1;
constexpr UINT_PTR previewTimerId = 2;

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return L"?";
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), required);
    return result;
}

std::filesystem::path executableDirectory() {
    std::wstring path(260, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) throw std::runtime_error("cannot determine executable directory");
        if (length < path.size()) { path.resize(length); return std::filesystem::path(path).parent_path(); }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path findAssetRoot() {
    for (auto start : {std::filesystem::current_path(), executableDirectory()}) {
        for (int depth = 0; depth < 6 && !start.empty(); ++depth) {
            const auto candidate = start / "Dungeon Underworld";
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) return candidate;
            const auto parent = start.parent_path();
            if (parent == start) break;
            start = parent;
        }
    }
    return {};
}

void appendMenuText(HMENU menu, UINT flags, UINT_PTR id, std::string_view text) {
    const std::wstring wide = utf8ToWide(text);
    AppendMenuW(menu, flags, id, wide.c_str());
}

class EditorWindow final {
public:
    EditorWindow(HINSTANCE instance, int show, std::filesystem::path assetRoot,
                 game::GameContentRegistry content,
                 std::optional<ContentWorkspaceDocument> contentWorkspace)
        : instance_(instance), show_(show), preferencesPath_(defaultEditorPreferencesPath()),
          preferences_(loadEditorPreferences(preferencesPath_)),
          app_(decoder_, assetRoot, std::move(content), std::move(contentWorkspace)) {
        app_.setLanguage(preferences_.language);
    }

    int run() {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = &procedure;
        wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = className;
        if (!RegisterClassExW(&wc)) return 1;
        registered_ = true;
        const std::wstring title = utf8ToWide(app_.windowTitle());
        window_ = CreateWindowExW(0, className, title.c_str(), WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, nullptr,
                                  createMenu(), instance_, this);
        if (!window_) return 1;
        ShowWindow(window_, show_); UpdateWindow(window_);
        SetTimer(window_, autosaveTimerId, 5000, nullptr);
        SetTimer(window_, previewTimerId, 16, nullptr);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    ~EditorWindow() {
        if (window_ && IsWindow(window_)) {
            KillTimer(window_, autosaveTimerId); KillTimer(window_, previewTimerId); DestroyWindow(window_);
        }
        if (registered_) UnregisterClassW(className, instance_);
    }

private:
    HMENU createMenu() {
        const auto text = [&](EditorTextId id) { return std::string(app_.localization().text(id)); };
        HMENU bar = CreateMenu();
        HMENU file = CreatePopupMenu(); HMENU edit = CreatePopupMenu(); HMENU view = CreatePopupMenu();
        HMENU settings = CreatePopupMenu(); HMENU language = CreatePopupMenu();
        appendMenuText(file, MF_STRING, fileNew, "&" + text(EditorTextId::newMap) + "\tCtrl+N");
        appendMenuText(file, MF_STRING, fileOpen, "&" + text(EditorTextId::openMap));
        appendMenuText(file, MF_STRING, fileSave, "&" + text(EditorTextId::save) + "\tCtrl+S");
        appendMenuText(file, MF_STRING, fileSaveAs, text(EditorTextId::saveAs));
        appendMenuText(file, MF_STRING, fileSaveAll, text(EditorTextId::saveAll));
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        appendMenuText(file, MF_STRING, fileExit, "&" + text(EditorTextId::exit));
        appendMenuText(edit, MF_STRING, editUndo, "&" + text(EditorTextId::undo) + "\tCtrl+Z");
        appendMenuText(edit, MF_STRING, editRedo, "&" + text(EditorTextId::redo) + "\tCtrl+Y");
        appendMenuText(view, MF_STRING, viewMap, "&" + text(EditorTextId::mapMode));
        appendMenuText(view, MF_STRING, viewContent, "&" + text(EditorTextId::contentMode));
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        appendMenuText(view, MF_STRING, viewGrid, "&" + text(EditorTextId::grid));
        appendMenuText(view, MF_STRING, viewFrame, "&" + text(EditorTextId::frameMap) + "\tHome");
        appendMenuText(view, MF_STRING, viewValidate, "&" + text(EditorTextId::validateWorkspace));
        appendMenuText(language, MF_STRING | (app_.language() == EditorLanguage::portugueseBrazil ? MF_CHECKED : 0),
                       settingsLanguagePortuguese, editorLanguageNativeName(EditorLanguage::portugueseBrazil));
        appendMenuText(language, MF_STRING | (app_.language() == EditorLanguage::englishUnitedStates ? MF_CHECKED : 0),
                       settingsLanguageEnglish, editorLanguageNativeName(EditorLanguage::englishUnitedStates));
        appendMenuText(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(language), "&" + text(EditorTextId::language));
        appendMenuText(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), "&" + text(EditorTextId::file));
        appendMenuText(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), "&" + text(EditorTextId::edit));
        appendMenuText(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), "&" + text(EditorTextId::view));
        appendMenuText(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), "&" + text(EditorTextId::settings));
        return bar;
    }

    void rebuildMenu() {
        const HMENU oldMenu = GetMenu(window_);
        SetMenu(window_, createMenu()); DrawMenuBar(window_);
        if (oldMenu) DestroyMenu(oldMenu);
    }

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->window_ = hwnd;
        }
        return self ? self->handle(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    LRESULT handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_SIZE: if (wp != SIZE_MINIMIZED) { app_.resize(LOWORD(lp), HIWORD(lp)); InvalidateRect(window_, nullptr, FALSE); } return 0;
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE: input_.pointer.x = GET_X_LPARAM(lp); input_.pointer.y = GET_Y_LPARAM(lp); modifiers(); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: SetFocus(window_); SetCapture(window_); input_.pointer.x = GET_X_LPARAM(lp); input_.pointer.y = GET_Y_LPARAM(lp); input_.pointer.leftDown = true; input_.pointer.leftPressed = true; modifiers(); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_LBUTTONUP: if (GetCapture() == window_) ReleaseCapture(); input_.pointer.x = GET_X_LPARAM(lp); input_.pointer.y = GET_Y_LPARAM(lp); input_.pointer.leftDown = false; input_.pointer.leftReleased = true; modifiers(); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_MBUTTONDOWN: SetCapture(window_); input_.pointer.x = GET_X_LPARAM(lp); input_.pointer.y = GET_Y_LPARAM(lp); input_.pointer.middleDown = true; input_.pointer.middlePressed = true; InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_MBUTTONUP: if (GetCapture() == window_) ReleaseCapture(); input_.pointer.x = GET_X_LPARAM(lp); input_.pointer.y = GET_Y_LPARAM(lp); input_.pointer.middleDown = false; input_.pointer.middleReleased = true; InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_MOUSEWHEEL: { POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(window_, &point); input_.pointer.x = point.x; input_.pointer.y = point.y; input_.pointer.wheelDelta += GET_WHEEL_DELTA_WPARAM(wp); InvalidateRect(window_, nullptr, FALSE); return 0; }
        case WM_CHAR: appendCharacter(static_cast<std::uint32_t>(wp)); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_KEYDOWN: keyDown(wp); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_KEYUP: if (wp == VK_SPACE) input_.space = false; modifiers(); return 0;
        case WM_KILLFOCUS: if (GetCapture() == window_) ReleaseCapture(); input_.pointer.leftDown = false; input_.pointer.middleDown = false; input_.space = false; pendingHighSurrogate_ = 0; input_.focusLost = true; InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_COMMAND: menuCommand(LOWORD(wp)); InvalidateRect(window_, nullptr, FALSE); return 0;
        case WM_TIMER: if (wp == autosaveTimerId) { std::string error; static_cast<void>(app_.autosave(error)); InvalidateRect(window_, nullptr, FALSE); } else if (wp == previewTimerId) { ++input_.previewTicks; InvalidateRect(window_, nullptr, FALSE); } return 0;
        case WM_CLOSE: if (confirmUnsaved()) DestroyWindow(window_); return 0;
        case WM_DESTROY: window_ = nullptr; PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window_, message, wp, lp);
        }
    }

    void appendCharacter(std::uint32_t value) {
        if (value >= 0xd800U && value <= 0xdbffU) { pendingHighSurrogate_ = value; return; }
        if (value >= 0xdc00U && value <= 0xdfffU && pendingHighSurrogate_ != 0) {
            value = 0x10000U + ((pendingHighSurrogate_ - 0xd800U) << 10U) + (value - 0xdc00U);
            pendingHighSurrogate_ = 0;
        } else if (pendingHighSurrogate_ != 0) pendingHighSurrogate_ = 0;
        if (value >= 32U && value != 127U && value != '\r' && value != '\n' && value != '\t') core::appendUtf8Codepoint(input_.textInput, value);
    }

    void modifiers() { input_.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0; input_.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0; input_.alt = (GetKeyState(VK_MENU) & 0x8000) != 0; }
    void keyDown(WPARAM key) { modifiers(); if (key == VK_SPACE) input_.space = true; else if (key == VK_DELETE) input_.deletePressed = true; else if (key == VK_HOME) input_.homePressed = true; else if (key == VK_RETURN) input_.enterPressed = true; else if (key == VK_ESCAPE) input_.escapePressed = true; else if (key == VK_BACK) input_.backspacePressed = true; else if (input_.control && key == 'D') input_.duplicatePressed = true; else if (input_.control && key == 'Z') input_.undoPressed = true; else if (input_.control && key == 'Y') input_.redoPressed = true; else if (input_.control && key == 'N') menuCommand(fileNew); else if (input_.control && key == 'O') menuCommand(fileOpen); else if (input_.control && key == 'S') menuCommand(fileSave); }

    void menuCommand(UINT id) {
        if (id == fileNew) { if (confirmUnsaved()) app_.shellCommand(EditorShellCommand::newMap); }
        else if (id == fileOpen) { if (confirmUnsaved()) openFile(); }
        else if (id == fileSave) save(false); else if (id == fileSaveAs) save(true);
        else if (id == fileSaveAll) { std::string error; if (!app_.saveAll(error)) showError(error); }
        else if (id == fileExit) SendMessageW(window_, WM_CLOSE, 0, 0);
        else if (id == editUndo) app_.shellCommand(EditorShellCommand::undo); else if (id == editRedo) app_.shellCommand(EditorShellCommand::redo);
        else if (id == viewMap) app_.shellCommand(EditorShellCommand::mapMode); else if (id == viewContent) app_.shellCommand(EditorShellCommand::contentMode);
        else if (id == viewGrid) app_.shellCommand(EditorShellCommand::toggleGrid); else if (id == viewFrame) app_.shellCommand(EditorShellCommand::frameMap);
        else if (id == viewValidate) app_.shellCommand(EditorShellCommand::validateWorkspace);
        else if (id == settingsLanguagePortuguese || id == settingsLanguageEnglish) {
            const EditorLanguage language = id == settingsLanguageEnglish ? EditorLanguage::englishUnitedStates : EditorLanguage::portugueseBrazil;
            app_.setLanguage(language); preferences_.language = language;
            std::string error; if (!saveEditorPreferences(preferencesPath_, preferences_, error)) showError(error);
            rebuildMenu();
        }
    }

    std::optional<std::filesystem::path> fileDialog(bool saveDialog) {
        wchar_t buffer[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_;
        const std::string filterLabel = app_.localization().localize("Dungeon authored/runtime maps (*.umap;*.dmap)");
        const std::string allFilesLabel = app_.localization().localize("All files");
        std::wstring filter = utf8ToWide(filterLabel); filter.push_back(L'\0'); filter += L"*.umap;*.dmap";
        filter.push_back(L'\0'); filter += utf8ToWide(allFilesLabel); filter.push_back(L'\0'); filter += L"*.*"; filter.push_back(L'\0'); filter.push_back(L'\0');
        dialog.lpstrFilter = filter.c_str();
        dialog.lpstrFile = buffer; dialog.nMaxFile = static_cast<DWORD>(std::size(buffer)); dialog.lpstrDefExt = L"umap";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (saveDialog ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL result = saveDialog ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return result ? std::optional<std::filesystem::path>{buffer} : std::nullopt;
    }
    void openFile() { if (const auto path = fileDialog(false)) { std::string error; if (!app_.open(*path, error)) showError(error); } }
    bool save(bool forceAs) { std::string error; if (app_.contentMode()) { if (!app_.save(error)) { showError(error); return false; } return true; } if (forceAs || !app_.document().filePath()) { const auto path = fileDialog(true); if (!path) return false; if (!app_.saveAs(*path, error)) { showError(error); return false; } } else if (!app_.save(error)) { showError(error); return false; } return true; }
    bool confirmUnsaved() { if (!app_.hasUnsavedChanges()) return true; const std::wstring message = utf8ToWide(app_.localization().text(EditorTextId::saveChangesBeforeContinuing)); const std::wstring title = utf8ToWide(app_.localization().text(EditorTextId::contentStudio)); const int choice = MessageBoxW(window_, message.c_str(), title.c_str(), MB_YESNOCANCEL | MB_ICONWARNING); if (choice == IDCANCEL) return false; if (choice == IDYES) return save(false); return true; }
    void showError(const std::string& error) { const std::wstring wide = utf8ToWide(error); const std::wstring title = utf8ToWide(app_.localization().text(EditorTextId::contentStudio)); MessageBoxW(window_, wide.c_str(), title.c_str(), MB_OK | MB_ICONERROR); }
    void paint() { PAINTSTRUCT ps{}; HDC dc = BeginPaint(window_, &ps); app_.updateAndRender(input_); const auto& surface = app_.framebuffer(); dib_.resize(surface.pixels().size()); for (std::size_t i = 0; i < surface.pixels().size(); ++i) { const auto p = surface.pixels()[i]; dib_[i] = static_cast<std::uint32_t>(p.b) | (static_cast<std::uint32_t>(p.g) << 8U) | (static_cast<std::uint32_t>(p.r) << 16U); } BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = surface.width(); info.bmiHeader.biHeight = -surface.height(); info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB; StretchDIBits(dc, 0, 0, surface.width(), surface.height(), 0, 0, surface.width(), surface.height(), dib_.data(), &info, DIB_RGB_COLORS, SRCCOPY); const std::wstring title = utf8ToWide(app_.windowTitle()); SetWindowTextW(window_, title.c_str()); EndPaint(window_, &ps); clearEdges(); }
    void clearEdges() { input_.pointer.leftPressed = false; input_.pointer.leftReleased = false; input_.pointer.middlePressed = false; input_.pointer.middleReleased = false; input_.pointer.wheelDelta = 0; input_.deletePressed = false; input_.duplicatePressed = false; input_.undoPressed = false; input_.redoPressed = false; input_.homePressed = false; input_.enterPressed = false; input_.escapePressed = false; input_.backspacePressed = false; input_.focusLost = false; input_.previewTicks = 0; input_.textInput.clear(); }

    HINSTANCE instance_{}; int show_{}; HWND window_{}; bool registered_{};
    platform::win32::Win32ImageDecoder decoder_;
    std::filesystem::path preferencesPath_;
    EditorPreferences preferences_;
    EditorApp app_;
    EditorInputState input_;
    std::uint32_t pendingHighSurrogate_{};
    std::vector<std::uint32_t> dib_;
};
} // namespace
} // namespace underworld::editor

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        int argc = 0; LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!raw) throw std::runtime_error("could not parse editor command line");
        std::vector<const wchar_t*> argv; argv.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) argv.push_back(raw[index]);
        std::string error; const auto options = underworld::editor::parseEditorLaunchOptions(argc, argv.data(), error); LocalFree(raw);
        if (!options) throw std::runtime_error(error);
        const auto assetRoot = options->assetRoot.value_or(underworld::editor::findAssetRoot());
        std::optional<underworld::editor::ContentWorkspaceDocument> document;
        underworld::game::GameContentRegistry registry;
        if (options->contentRoot) {
            auto opened = underworld::editor::ContentWorkspaceDocument::open(*options->contentRoot, error);
            if (!opened) throw std::runtime_error(error); document = std::move(*opened);
            if (document->compiledRegistry()) registry = *document->compiledRegistry();
        } else {
            underworld::game::content::ContentSourceSelection selection; const auto source = underworld::game::content::loadContentSource(selection);
            if (!source) { std::string message; for (const auto& diagnostic : source.diagnostics) message += underworld::game::content::formatContentWorkspaceDiagnostic(diagnostic) + "\n"; throw std::runtime_error(message); }
            registry = std::move(source.content->registry);
        }
        return underworld::editor::EditorWindow(instance, show, assetRoot, std::move(registry), std::move(document)).run();
    } catch (const std::exception& exception) {
        const std::wstring message = underworld::editor::utf8ToWide(exception.what());
        underworld::editor::EditorLocalization localization;
        localization.setLanguage(underworld::editor::loadEditorPreferences(
            underworld::editor::defaultEditorPreferencesPath()).language);
        const std::wstring title = underworld::editor::utf8ToWide(
            localization.text(underworld::editor::EditorTextId::initializationError));
        MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR); return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"Unknown initialization failure", L"Map Maker initialization error", MB_OK | MB_ICONERROR); return 1;
    }
}
