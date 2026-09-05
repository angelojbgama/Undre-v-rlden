#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include "editor/editor_app.h"
#include "engine/platform/win32/win32_image_decoder.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace underworld::editor {
namespace {
constexpr wchar_t className[]=L"DungeonUnderworldMapMakerWindow";
enum MenuId : UINT { fileNew=1001,fileOpen,fileSave,fileSaveAs,fileExit,editUndo,editRedo,viewGrid,viewFrame };
constexpr UINT_PTR autosaveTimerId = 1;

std::filesystem::path executableDirectory(){std::wstring path(260,L'\0');for(;;){const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(length==0)throw std::runtime_error("cannot determine executable directory");if(length<path.size()){path.resize(length);return std::filesystem::path(path).parent_path();}path.resize(path.size()*2);}}
std::filesystem::path findAssetRoot(){for(auto start:{std::filesystem::current_path(),executableDirectory()})for(int depth=0;depth<6&&!start.empty();++depth){const auto candidate=start/"Dungeon Underworld";std::error_code error;if(std::filesystem::is_directory(candidate,error))return candidate;const auto parent=start.parent_path();if(parent==start)break;start=parent;}return{};}

class EditorWindow final {
public:
    EditorWindow(HINSTANCE instance,int show):instance_(instance),show_(show),app_(decoder_,findAssetRoot()){}
    int run(){
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=&procedure;wc.hInstance=instance_;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=className;
        if(!RegisterClassExW(&wc))return 1;registered_=true;
        window_=CreateWindowExW(0,className,L"Dungeon Underworld - Map Maker",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,createMenu(),instance_,this);
        if(!window_)return 1;ShowWindow(window_,show_);UpdateWindow(window_);SetTimer(window_,autosaveTimerId,5000,nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
    }
    ~EditorWindow(){if(window_&&IsWindow(window_)){KillTimer(window_,autosaveTimerId);DestroyWindow(window_);}if(registered_)UnregisterClassW(className,instance_);}
private:
    HMENU createMenu(){HMENU bar=CreateMenu(),file=CreatePopupMenu(),edit=CreatePopupMenu(),view=CreatePopupMenu();AppendMenuW(file,MF_STRING,fileNew,L"&New\tCtrl+N");AppendMenuW(file,MF_STRING,fileOpen,L"&Open...\tCtrl+O");AppendMenuW(file,MF_STRING,fileSave,L"&Save\tCtrl+S");AppendMenuW(file,MF_STRING,fileSaveAs,L"Save &As...");AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,fileExit,L"E&xit");AppendMenuW(edit,MF_STRING,editUndo,L"&Undo\tCtrl+Z");AppendMenuW(edit,MF_STRING,editRedo,L"&Redo\tCtrl+Y");AppendMenuW(view,MF_STRING,viewGrid,L"&Grid");AppendMenuW(view,MF_STRING,viewFrame,L"&Frame Map\tHome");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),L"&File");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(edit),L"&Edit");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"&View");return bar;}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){EditorWindow* self=reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window_=hwnd;}return self?self->handle(message,wp,lp):DefWindowProcW(hwnd,message,wp,lp);}
    LRESULT handle(UINT message,WPARAM wp,LPARAM lp){
        switch(message){
        case WM_SIZE:if(wp!=SIZE_MINIMIZED){app_.resize(LOWORD(lp),HIWORD(lp));InvalidateRect(window_,nullptr,FALSE);}return 0;
        case WM_PAINT:paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_MOUSEMOVE:input_.pointer.x=GET_X_LPARAM(lp);input_.pointer.y=GET_Y_LPARAM(lp);modifiers();InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:SetFocus(window_);SetCapture(window_);input_.pointer.x=GET_X_LPARAM(lp);input_.pointer.y=GET_Y_LPARAM(lp);input_.pointer.leftDown=true;input_.pointer.leftPressed=true;modifiers();InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:if(GetCapture()==window_)ReleaseCapture();input_.pointer.x=GET_X_LPARAM(lp);input_.pointer.y=GET_Y_LPARAM(lp);input_.pointer.leftDown=false;input_.pointer.leftReleased=true;modifiers();InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_MBUTTONDOWN:SetCapture(window_);input_.pointer.middleDown=true;input_.pointer.middlePressed=true;InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_MBUTTONUP:if(GetCapture()==window_)ReleaseCapture();input_.pointer.middleDown=false;input_.pointer.middleReleased=true;InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window_,&p);input_.pointer.x=p.x;input_.pointer.y=p.y;input_.pointer.wheelDelta+=GET_WHEEL_DELTA_WPARAM(wp);InvalidateRect(window_,nullptr,FALSE);return 0;}
        case WM_CHAR:if(wp>=32&&wp<127)input_.textInput.push_back(static_cast<char>(wp));InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_KEYDOWN:keyDown(wp);InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_KEYUP:if(wp==VK_SPACE)input_.space=false;modifiers();return 0;
        case WM_KILLFOCUS:if(GetCapture()==window_)ReleaseCapture();input_.pointer.leftDown=false;input_.pointer.middleDown=false;input_.space=false;input_.focusLost=true;InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_COMMAND:menuCommand(LOWORD(wp));InvalidateRect(window_,nullptr,FALSE);return 0;
        case WM_TIMER:if(wp==autosaveTimerId){std::string error;app_.autosave(error);InvalidateRect(window_,nullptr,FALSE);}return 0;
        case WM_CLOSE:if(confirmUnsaved())DestroyWindow(window_);return 0;
        case WM_DESTROY:window_=nullptr;PostQuitMessage(0);return 0;
        default:return DefWindowProcW(window_,message,wp,lp);}
    }
    void modifiers(){input_.shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;input_.control=(GetKeyState(VK_CONTROL)&0x8000)!=0;input_.alt=(GetKeyState(VK_MENU)&0x8000)!=0;}
    void keyDown(WPARAM key){modifiers();if(key==VK_SPACE)input_.space=true;else if(key==VK_DELETE)input_.deletePressed=true;else if(key==VK_HOME)input_.homePressed=true;else if(key==VK_RETURN)input_.enterPressed=true;else if(key==VK_ESCAPE)input_.escapePressed=true;else if(key==VK_BACK)input_.backspacePressed=true;else if(input_.control&&key=='D')input_.duplicatePressed=true;else if(input_.control&&key=='Z')input_.undoPressed=true;else if(input_.control&&key=='Y')input_.redoPressed=true;else if(input_.control&&key=='N')menuCommand(fileNew);else if(input_.control&&key=='O')menuCommand(fileOpen);else if(input_.control&&key=='S')menuCommand(fileSave);}
    void menuCommand(UINT id){if(id==fileNew){if(confirmUnsaved())app_.shellCommand(EditorShellCommand::newMap);}else if(id==fileOpen){if(confirmUnsaved())openFile();}else if(id==fileSave)save(false);else if(id==fileSaveAs)save(true);else if(id==fileExit)SendMessageW(window_,WM_CLOSE,0,0);else if(id==editUndo)app_.shellCommand(EditorShellCommand::undo);else if(id==editRedo)app_.shellCommand(EditorShellCommand::redo);else if(id==viewGrid)app_.shellCommand(EditorShellCommand::toggleGrid);else if(id==viewFrame)app_.shellCommand(EditorShellCommand::frameMap);}
    std::optional<std::filesystem::path> fileDialog(bool saveDialog){wchar_t buffer[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;dialog.lpstrFilter=L"Dungeon maps (*.dmap)\0*.dmap\0All files\0*.*\0";dialog.lpstrFile=buffer;dialog.nMaxFile=static_cast<DWORD>(std::size(buffer));dialog.lpstrDefExt=L"dmap";dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(saveDialog?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);const BOOL result=saveDialog?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);return result?std::optional<std::filesystem::path>{buffer}:std::nullopt;}
    void openFile(){if(const auto path=fileDialog(false)){std::string error;if(!app_.open(*path,error))showError(error);}}
    bool save(bool forceAs){std::string error;if(forceAs||!app_.document().filePath()){const auto path=fileDialog(true);if(!path)return false;if(!app_.saveAs(*path,error)){showError(error);return false;}}else if(!app_.save(error)){showError(error);return false;}return true;}
    bool confirmUnsaved(){if(!app_.document().dirty())return true;const int choice=MessageBoxW(window_,L"Save changes before continuing?",L"Dungeon Underworld - Map Maker",MB_YESNOCANCEL|MB_ICONWARNING);if(choice==IDCANCEL)return false;if(choice==IDYES)return save(false);return true;}
    void showError(const std::string& error){const std::wstring wide(error.begin(),error.end());MessageBoxW(window_,wide.c_str(),L"Map Maker",MB_OK|MB_ICONERROR);}
    void paint(){PAINTSTRUCT ps{};HDC dc=BeginPaint(window_,&ps);app_.updateAndRender(input_);const auto& surface=app_.framebuffer();dib_.resize(surface.pixels().size());for(std::size_t i=0;i<surface.pixels().size();++i){const auto p=surface.pixels()[i];dib_[i]=static_cast<std::uint32_t>(p.b)|(static_cast<std::uint32_t>(p.g)<<8U)|(static_cast<std::uint32_t>(p.r)<<16U);}
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=surface.width();info.bmiHeader.biHeight=-surface.height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;StretchDIBits(dc,0,0,surface.width(),surface.height(),0,0,surface.width(),surface.height(),dib_.data(),&info,DIB_RGB_COLORS,SRCCOPY);const std::string title=app_.windowTitle();const std::wstring wide(title.begin(),title.end());SetWindowTextW(window_,wide.c_str());EndPaint(window_,&ps);clearEdges();}
    void clearEdges(){input_.pointer.leftPressed=false;input_.pointer.leftReleased=false;input_.pointer.middlePressed=false;input_.pointer.middleReleased=false;input_.pointer.wheelDelta=0;input_.deletePressed=false;input_.duplicatePressed=false;input_.undoPressed=false;input_.redoPressed=false;input_.homePressed=false;input_.enterPressed=false;input_.escapePressed=false;input_.backspacePressed=false;input_.focusLost=false;input_.textInput.clear();}
    HINSTANCE instance_{};int show_{};HWND window_{};bool registered_{};platform::win32::Win32ImageDecoder decoder_;EditorApp app_;EditorInputState input_;std::vector<std::uint32_t> dib_;
};
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{return underworld::editor::EditorWindow(instance,show).run();}catch(const std::exception& exception){const std::string text=exception.what();const std::wstring wide(text.begin(),text.end());MessageBoxW(nullptr,wide.c_str(),L"Map Maker initialization error",MB_OK|MB_ICONERROR);return 1;}catch(...){MessageBoxW(nullptr,L"Unknown initialization failure",L"Map Maker initialization error",MB_OK|MB_ICONERROR);return 1;}}
