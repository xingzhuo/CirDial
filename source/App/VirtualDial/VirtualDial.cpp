#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <setupapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <initguid.h>
#include <vector>
#include <map>
#include <string>
#include <cstdio>
#include <algorithm>
#include "../../Shared/VirtualDialProtocol.h"
#include "DialMath.h"
#include "DialVisual.h"
#include "DialScheduler.h"
#include "LicenseProbe.h"
#include "DialBackend.h"
#include "RawTouch.h"
#include "TouchTap.h"
#include "TouchRelay.h"
#include "Shake.h"
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"wtsapi32.lib")
#pragma comment(lib,"advapi32.lib")

namespace {
constexpr float Width=240, Height=240, Cx=120, Cy=120;
constexpr UINT TrayMessage=WM_APP+1, ShowMessage=WM_APP+2;
constexpr UINT_PTR Timer=1;
const wchar_t* ClassName=L"CirDial.Touch.1";
HWND window=nullptr;
std::unique_ptr<dial::IDialBackend> backend;
bool usbipBackend=false,backendInitialized=false;
ULONG_PTR graphicsToken=0;
HDC surfaceDC=nullptr; HBITMAP surface=nullptr; HGDIOBJ previousSurface=nullptr;
void* surfaceBits=nullptr; int surfaceSize=0;
bool visualDirty=true, mouseEnabled=false, menuExpanded=false;
UINT32 innerColor=0,outerColor=0x202020;
int innerTransparency=0,outerTransparency=0;
dial::Tween expansion{},touchScale{1,1,1},bounceX{},bounceY{};
bool bouncing=false;
dial::RawTouch rawTouch;
bool gestureSummon=false;
dial::LicenseProbe licenseProbe;
ULONGLONG licenseChecked=0;
std::wstring SettingsExecutable() {
    wchar_t path[32768]={};GetModuleFileNameW(nullptr,path,32768);
    std::wstring exe(path);return exe.substr(0,exe.find_last_of(L"\\/"))+L"\\Settings\\CirDial.Settings.exe";
}
void RefreshLicense() {

    licenseChecked=GetTickCount64();licenseProbe.Begin(window,SettingsExecutable());
}
bool shakeEnabled=false;
dial::Shake shake;
HWND observerWindow=nullptr,noticeWindow=nullptr;
bool summonAvailable=false;
bool observing=false,relayEmergencyKey=false;
dial::TouchRelay touchRelay;
void StopRelay(){if(observerWindow)KillTimer(observerWindow,1);if(observing&&observerWindow)UnregisterPointerInputTarget(observerWindow,PT_TOUCH);observing=false;touchRelay.Cancel();summonAvailable=false;}

dial::TouchTap globalTap,localTap;
ULONGLONG lastSummon=0;
void Show();void Hide();void ConfigureSummon();void ModeNotice();
void ShowNotice(const std::wstring& text);
dial::DialReveal reveal{1,1,1};
UINT timerInterval=16;
void FastTimer() {if(window&&timerInterval!=16){timerInterval=16;SetTimer(window,Timer,16,nullptr);}}
int diameterDip=164,idleDelayTenths=150,idleTransparency=800;
double normalRadius=60,screenRadius=110;
dial::Tween idleFade{},pointerFade{};
bool indicatorEnabled=true;
UINT32 indicatorColor=0xf5f5f5;
double pointerAngle=0;
ULONGLONG lastActivity=0,confirmTime=0;
void Wake() {
    FastTimer();lastActivity=GetTickCount64();idleFade.Set(0,lastActivity,140);visualDirty=true;
}
HWND nativeMenu=nullptr;
ULONGLONG menuPoll=0,menuRequestUntil=0;
float dpi=96;
double phase=0;
int sensitivityTenths=10;
int opacity=80;
bool onScreen=false, positionCapable=false, sessionLocked=false, lifted=true;
VIRTUAL_DIAL_CAPS caps={VIRTUAL_DIAL_POSITION_VERSION,27400,18300,5900};
bool positionDirty=false,centerReportPending=false;
ULONGLONG centerLiftDue=0;
bool reverse=false, pressed=false, closing=false, smoke=false, preview=false,interactionTest=false;
DWORD lastError=ERROR_NOT_FOUND;
ULONGLONG pressTime=0, lastConnect=0, startTime=0;
unsigned long sent=0;
std::wstring settings;
std::wstring logPath;
UINT taskbarCreated=0;
HWINEVENTHOOK overlayHook=nullptr;
bool trayOpen=false, overlayPending=false;
ULONGLONG overlayUntil=0, lastOverlayCheck=0;
HWND lastObstruction=nullptr;
unsigned overlayAttempts=0;
dial::Accumulator accumulator;
enum class Hit { None, Ring, Center, Drag, Hide, Close, Slower, Faster, Reverse, Connect };
struct Contact { Hit hit; double angle; POINT screen; RECT rect; ULONGLONG started=0; bool buttonSent=false; bool moved=false; POINT latest={}; bool angleValid=true; };
std::map<UINT32,Contact> contacts;
bool twoFingerDrag=false;
std::map<UINT32,POINT> dragPoints;
POINT dragOrigin={}; RECT dragRect={};
POINT ContactCentroid() {
    long x=0,y=0;for(const auto& item:dragPoints){x+=item.second.x;y+=item.second.y;}
    return {x/static_cast<long>(dragPoints.size()),y/static_cast<long>(dragPoints.size())};
}
Hit pendingTap=Hit::None;
int tapCount=0;bool tapAlreadyConfirmed=false;
POINT pendingTapPoint={};
ULONGLONG pendingTapTime=0;
bool programmaticHold=false;
constexpr UINT32 MouseId=0xffffffff;
template<class T> void Release(T*& value) { if(value) { value->Release(); value=nullptr; } }
bool Connected() { return backend&&backend->Connected(); }
void ResizeDial();
void TrayMenu(bool exitMenu);
void Paint();
void Save();
void ReloadSettings();
double OuterRadius() {
    double target=onScreen?screenRadius:normalRadius*(.39+.61*1.5);
    double spring=0;
    if(confirmTime) {double t=(GetTickCount64()-confirmTime)/420.0;if(t<1)spring=-normalRadius*.06*std::sin(t*3.141592653589793*2)*std::sin(t*3.141592653589793);}
    return (normalRadius+(target-normalRadius)*expansion.value+spring)*touchScale.value*reveal.outer;
}
double InnerRadius() {return normalRadius*touchScale.value*reveal.inner;}
MONITORINFOEXW Primary() {
    MONITORINFOEXW info={}; info.cbSize=sizeof(info);
    GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info); return info;
}
DWORD Orientation() {
    auto monitor=Primary(); DEVMODEW mode={}; mode.dmSize=sizeof(mode);
    return EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode)?mode.dmDisplayOrientation:0;
}
void Redraw() { visualDirty=true; }
double Sensitivity() { return sensitivityTenths/10.0; }
int LegacySensitivity(int index) { const int values[]={5,10,20,40};return values[std::clamp(index,0,3)]; }
void Log(const wchar_t* event,DWORD error=0) {
    if(smoke||logPath.empty()) return;
    // Bound disk growth during long-running sessions, not only at startup.
    WIN32_FILE_ATTRIBUTE_DATA info={};
    const bool reset=GetFileAttributesExW(logPath.c_str(),GetFileExInfoStandard,&info)&&
        (info.nFileSizeHigh||info.nFileSizeLow>1024*1024);
    FILE* file=nullptr;
    if(_wfopen_s(&file,logPath.c_str(),reset?L"w, ccs=UTF-8":L"a, ccs=UTF-8")!=0) return;
    SYSTEMTIME now; GetLocalTime(&now);
    fwprintf(file,L"%04u-%02u-%02u %02u:%02u:%02u %ls error=%lu reports=%lu\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,event,error,sent);
    fclose(file);
}

// Keep the input surface available without activating it (activation would
// redirect the native radial controller away from the user's application).
// Bound retries: protected shell bands cannot be fixed by a topmost loop.
void MaintainOverlay() {
    if(closing||lifted||sessionLocked||trayOpen||!IsWindowVisible(window)||!mouseEnabled) return;
    const auto now=GetTickCount64();
    if(now-lastOverlayCheck<200) return;
    lastOverlayCheck=now;
    RECT rc={}; GetWindowRect(window,&rc);
    POINT center={(rc.left+rc.right)/2,(rc.top+rc.bottom)/2};
    HWND above=GetAncestor(WindowFromPoint(center),GA_ROOT);
    if(!above||above==window) { lastObstruction=nullptr; overlayAttempts=0; return; }
    DWORD pid=0; GetWindowThreadProcessId(above,&pid);
    if(pid==GetCurrentProcessId()) return;
    if(above!=lastObstruction) { lastObstruction=above; overlayAttempts=0; }
    if(overlayAttempts++>=3) return;
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    wchar_t cls[128]={},entry[256]={}; GetClassNameW(above,cls,128);
    const bool restored=GetAncestor(WindowFromPoint(center),GA_ROOT)==window;
    swprintf_s(entry,L"Overlay %ls; blocker class=%ls pid=%lu",restored?L"restored":L"still covered",cls,pid);
    Log(entry);
}
void CALLBACK OverlayEvent(HWINEVENTHOOK,DWORD,HWND hwnd,LONG object,LONG,DWORD,DWORD) {
    if(hwnd&&hwnd!=window&&object==OBJID_WINDOW&&GetTickCount64()<overlayUntil)
        overlayPending=true;
}

void CloseDevice() { if(backend)backend->Close();pressed=false;positionCapable=false;backendInitialized=false; }
bool Send(long rotation, bool down) {
    if(!Connected()) return false;
    // PositionDirty / Show / settings refresh must not immediately lift the
    // one-shot centre contact before the scheduled release reaches Windows.
    if(!onScreen&&centerLiftDue>GetTickCount64()&&!pressed&&!down&&rotation==0&&!sessionLocked&&!closing)return true;
    VIRTUAL_DIAL_COMMAND_V2 position={VIRTUAL_DIAL_POSITION_VERSION,rotation,static_cast<BYTE>(down),0,0,0,0};
    if(positionCapable&&onScreen&&!lifted&&!sessionLocked&&!closing) {
        auto monitor=Primary(); RECT rect={}; GetWindowRect(window,&rect);
        double x=((rect.left+rect.right)*0.5-monitor.rcMonitor.left)/(monitor.rcMonitor.right-monitor.rcMonitor.left);
        double y=((rect.top+rect.bottom)*0.5-monitor.rcMonitor.top)/(monitor.rcMonitor.bottom-monitor.rcMonitor.top);
        auto native=dial::ToNative(x,y,Orientation());
        position.PositionValid=1; position.X=dial::Normalize(native.x); position.Y=dial::Normalize(native.y);
    }
    bool centerPulse=centerReportPending&&positionCapable&&!onScreen&&!sessionLocked&&!closing;
    if(centerPulse) {position.PositionValid=1;position.X=dial::Normalize(.5);position.Y=dial::Normalize(.5);}
    if(!backend->Send(position)) {
        lastError=backend->Error(); Log(L"Backend input failed",lastError); CloseDevice(); contacts.clear(); accumulator.Reset(); Redraw(); return false;
    }
    if(centerPulse) {centerReportPending=false;centerLiftDue=GetTickCount64()+80;Log(L"Off-screen center coordinate reported once");}
    pressed=down; ++sent; Redraw(); return true;
}
void ReleaseButton() { if(pressed) { if(GetTickCount64()-pressTime>=500) menuRequestUntil=GetTickCount64()+1000;Send(0,false); } pressed=false; }
void CancelInput() { twoFingerDrag=false;dragPoints.clear(); pointerFade.Set(0,GetTickCount64(),120);contacts.clear(); pendingTap=Hit::None;tapCount=0;tapAlreadyConfirmed=false;programmaticHold=false;touchScale.Set(1,GetTickCount64(),120); accumulator.Reset(); ReleaseButton(); if(GetCapture()==window) ReleaseCapture(); Redraw(); }
void ConfirmTool() {
    Wake();confirmTime=GetTickCount64();
    programmaticHold=false;ReleaseButton();Send(0,true);ReleaseButton();
    menuRequestUntil=0;menuExpanded=false;nativeMenu=nullptr;expansion.Set(0,GetTickCount64());
}
void BeginToolSelection() {
    Wake();confirmTime=0;
    pendingTap=Hit::None;ReleaseButton();pressTime=GetTickCount64();
    programmaticHold=Send(0,true)||smoke;Log(L"Double tap: opening native tools");
}
void ToggleScreenMode() {
    CancelInput();menuExpanded=false;nativeMenu=nullptr;menuRequestUntil=0;expansion.Set(0,GetTickCount64());
    if(!smoke&&!settings.empty()) {
        WritePrivateProfileStringW(L"Dial",L"OnScreen",onScreen?L"0":L"1",settings.c_str());ReloadSettings();
    } else {onScreen=!onScreen;centerReportPending=!onScreen;Wake();}
    Send(0,false);Log(onScreen?L"Screen mode enabled":L"Screen mode disabled");
}
void ResolveTaps() {
    int count=tapCount;Hit hit=pendingTap;bool confirmed=tapAlreadyConfirmed;
    tapCount=0;pendingTap=Hit::None;tapAlreadyConfirmed=false;
    if(count==2)BeginToolSelection();else if(count==1&&hit==Hit::Center&&!confirmed)ConfirmTool();
}
void Tap(Hit hit,POINT point) {
    const auto now=GetTickCount64();
    bool continuation=pendingTap!=Hit::None&&now-pendingTapTime<=GetDoubleClickTime()&&
        std::hypot(double(point.x-pendingTapPoint.x),double(point.y-pendingTapPoint.y))<=32*GetDpiForWindow(window)/96.0;
    if(!continuation) {ResolveTaps();tapCount=0;tapAlreadyConfirmed=false;}
    if(menuExpanded) {ConfirmTool();tapAlreadyConfirmed=true;}
    pendingTap=hit;pendingTapTime=now;pendingTapPoint=point;
    if(++tapCount==3) {ToggleScreenMode();return;}

}
void Connect() {
    if(smoke||sessionLocked||closing)return;
    lastConnect=GetTickCount64();
    if(!backend)backend=dial::CreateDialBackend(usbipBackend);
    backend->Connect();DWORD previousError=lastError;lastError=backend->Error();
    if(lastError&&lastError!=previousError)Log(L"Backend connection status",lastError);
    if(Connected()&&!backendInitialized){
        backendInitialized=true;positionCapable=backend->Caps(caps);
        if(positionCapable){if(!onScreen)centerReportPending=true;ResizeDial();}
        Log(usbipBackend?L"USB/IP backend ready":L"VHF backend ready");Send(0,false);Redraw();
    }
}

void Rotate(double degrees) {
    Wake();
    if(!Connected()) return;
    long value=accumulator.Add(degrees,Sensitivity(),reverse);
    while(value!=0) {
        long part=std::clamp(value,-3600L,3600L);
        if(!Send(part,pressed)) break;
        phase=std::remainder(phase+part/10.0,360.0); value-=part;
    }
    Redraw();
}
Hit HitTest(float x,float y) {
    const double r=std::hypot(x-Cx,y-Cy);
    if(r<=InnerRadius()) return Hit::Center;
    if(r<=OuterRadius()) return Hit::Ring;
    return Hit::None;
}
bool Owns(Hit hit) { for(const auto& item:contacts) if(item.second.hit==hit) return true; return false; }
void Save() {
    if(smoke||settings.empty()) return;
    RECT r={}; GetWindowRect(window,&r);
    WritePrivateProfileStringW(L"Dial",L"X",std::to_wstring(r.left).c_str(),settings.c_str());
    WritePrivateProfileStringW(L"Dial",L"Y",std::to_wstring(r.top).c_str(),settings.c_str());
}
void Hide() { lifted=true; CancelInput();menuRequestUntil=0;menuExpanded=false;nativeMenu=nullptr;expansion.Set(0,GetTickCount64());Send(0,false); Save(); reveal.Set(0,GetTickCount64(),360);FastTimer(); Redraw(); Log(L"Screen contact lifted (hidden)"); }
void ClampPosition() {
    RECT r={}; GetWindowRect(window,&r);
    auto mi=Primary();
    int x=std::clamp(r.left,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-(r.right-r.left)));
    int y=std::clamp(r.top,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-(r.bottom-r.top)));
    SetWindowPos(window,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
}
void BounceToEdge() {
    RECT rc={}; GetWindowRect(window,&rc); auto mi=Primary();
    // Clamp the visible circle, rather than the transparent animation margin.
    int margin=static_cast<int>((120-std::max(normalRadius,OuterRadius()))*dpi/96);
    int x=dial::Edge(rc.left+margin,mi.rcWork.left,mi.rcWork.right,rc.right-rc.left-2*margin)-margin;
    int y=dial::Edge(rc.top+margin,mi.rcWork.top,mi.rcWork.bottom,rc.bottom-rc.top-2*margin)-margin;
    bounceX={double(rc.left),double(rc.left),double(rc.left)};bounceY={double(rc.top),double(rc.top),double(rc.top)};
    bounceX.Set(x,GetTickCount64(),240);bounceY.Set(y,GetTickCount64(),240);bouncing=x!=rc.left||y!=rc.top;
    if(!bouncing) Save();
}
void ResizeDial() {
    if(!window) return;
    auto mi=Primary(); DWORD orientation=Orientation();
    double physicalWidth=(orientation==1||orientation==3)?caps.ScreenHeightHundredthsMm:caps.ScreenWidthHundredthsMm;
    int pixels=static_cast<int>(std::lround((mi.rcMonitor.right-mi.rcMonitor.left)*caps.DialDiameterHundredthsMm/physicalWidth));
    RECT old={};GetWindowRect(window,&old);
    double normalPixels=diameterDip*GetDpiForWindow(window)/96.0;
    double screenPixels=pixels*1.18; // Matches the opening measured in the user's native menu capture.
    int canvas=std::clamp(int(std::ceil(std::max(normalPixels*1.5,screenPixels)*1.10)),100,2400);
    dpi=canvas*96.f/Width;
    normalRadius=normalPixels*.5*96/dpi;screenRadius=screenPixels*.5*96/dpi;
    SetWindowPos(window,nullptr,(old.left+old.right-canvas)/2,(old.top+old.bottom-canvas)/2,canvas,canvas,SWP_NOZORDER|SWP_NOACTIVATE);
    BounceToEdge(); positionDirty=true; Redraw();
}
void Action(Hit hit) {
    switch(hit) {
    case Hit::Hide: Hide(); break;
    case Hit::Close: DestroyWindow(window); break;
    case Hit::Slower: sensitivityTenths=std::max(1,sensitivityTenths-1); accumulator.Reset(); Save(); break;
    case Hit::Faster: sensitivityTenths=std::min(40,sensitivityTenths+1); accumulator.Reset(); Save(); break;
    case Hit::Reverse: reverse=!reverse; accumulator.Reset(); Save(); break;
    case Hit::Connect: CancelInput(); CloseDevice(); Connect(); break;
    default: break;
    }
    Redraw();
}
void Down(UINT32 id, POINT screen) {
    if(contacts.empty())shake.Reset();
    if(lifted||reveal.value<.99||contacts.count(id)||twoFingerDrag) return;
    POINT p=screen; ScreenToClient(window,&p);
    float x=p.x*96.f/dpi,y=p.y*96.f/dpi;
    Hit hit=HitTest(x,y);
    if(hit==Hit::None) return;
    if(id!=MouseId&&contacts.size()==1&&!contacts.count(MouseId)) {
        auto first=contacts.begin();
        dragPoints.clear();dragPoints[first->first]=first->second.latest;dragPoints[id]=screen;
        dragOrigin=ContactCentroid();GetWindowRect(window,&dragRect);
        first->second.hit=Hit::Drag;first->second.moved=true;first->second.buttonSent=false;
        contacts[id]={Hit::Drag,0,screen,dragRect,GetTickCount64(),false,true};
        twoFingerDrag=true;bouncing=false;pendingTap=Hit::None;tapCount=0;programmaticHold=false;
        ReleaseButton();accumulator.Reset();pointerFade.Set(0,GetTickCount64(),80);Wake();
        Log(L"Two-finger drag started");return;
    }
    if(Owns(hit)) return;
    if((hit==Hit::Drag&&!contacts.empty())||Owns(Hit::Drag)) return;
    if(hit==Hit::Ring&&!Connected()&&!smoke) return;
    if(hit==Hit::Center&&!contacts.empty()) return;
    if(hit==Hit::Ring&&Owns(Hit::Center)) return;
    Wake();bouncing=false;touchScale.Set(1.035,GetTickCount64(),100);
    Contact c={hit,dial::Angle(x-Cx,y-Cy),screen,{}}; GetWindowRect(window,&c.rect);
    c.started=GetTickCount64();c.latest=screen;
    contacts[id]=c;
    overlayUntil=GetTickCount64()+30000; overlayPending=true;
    lastObstruction=nullptr; overlayAttempts=0;
    Log(hit==Hit::Center?L"Input down: center":hit==Hit::Ring?L"Input down: ring":L"Input down: move");
    if(hit==Hit::Ring) {accumulator.Reset();pointerAngle=c.angle+180;pointerFade.Set(1,GetTickCount64(),80);}
    Redraw();
}
void Move(UINT32 id,POINT screen) {
    auto found=contacts.find(id); if(found==contacts.end()) return;
    Wake();auto& c=found->second;c.latest=screen;
    if(twoFingerDrag) {
        dragPoints[id]=screen;POINT center=ContactCentroid();
        if(shakeEnabled&&shake.Move(center,GetTickCount64(),GetDpiForWindow(window)/96.0)){Hide();return;}
        SetWindowPos(window,HWND_TOPMOST,dragRect.left+center.x-dragOrigin.x,dragRect.top+center.y-dragOrigin.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        positionDirty=true;return;
    }
    if(dial::StartDrag(screen.x-c.screen.x,screen.y-c.screen.y,GetDpiForWindow(window))) c.moved=true;
    // Commit an inner arc to rotation; radial movement and the central dead zone drag.
    // Once a hold is committed, moving must not turn its release into a click.
    if(c.hit==Hit::Center&&!c.buttonSent&&dial::StartDrag(screen.x-c.screen.x,screen.y-c.screen.y,GetDpiForWindow(window))) {
        double scale=96.0/dpi;
        double cx=(c.rect.left+c.rect.right)/2.0,cy=(c.rect.top+c.rect.bottom)/2.0;
        if(dial::InnerArc((c.screen.x-cx)*scale,(c.screen.y-cy)*scale,(screen.x-cx)*scale,(screen.y-cy)*scale)) {
            c.hit=Hit::Ring;accumulator.Reset();pointerFade.Set(1,GetTickCount64(),80);Log(L"Center gesture committed: rotation");
        } else {c.hit=Hit::Drag;Log(L"Center gesture committed: drag");}
    }
    if(c.hit==Hit::Drag) { if(shakeEnabled&&shake.Move(screen,GetTickCount64(),GetDpiForWindow(window)/96.0)){Hide();return;} SetWindowPos(window,HWND_TOPMOST,c.rect.left+screen.x-c.screen.x,c.rect.top+screen.y-c.screen.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE); positionDirty=true; return; }
    if(c.hit!=Hit::Ring) return;
    POINT p=screen; ScreenToClient(window,&p);
    double x=p.x*96.0/dpi-Cx,y=p.y*96.0/dpi-Cy;
    double angle=dial::Angle(x,y), delta=dial::Delta(c.angle,angle); c.angle=angle;pointerAngle=angle+180;Redraw();
    // Avoid singular jumps when a finger crosses the dial's centre.
    if(std::hypot(x,y)<std::max(4.0,InnerRadius()*.15)){c.angleValid=false;return;}
    if(c.moved&&c.angleValid)Rotate(delta);
    c.angleValid=true;
}
void Up(UINT32 id,POINT screen,bool canceled=false) {
    auto found=contacts.find(id); if(found==contacts.end()) return;
    Wake();pointerFade.Set(0,GetTickCount64(),180);Contact contact=found->second;Hit hit=contact.hit; bool buttonSent=contact.buttonSent; contacts.erase(found);
    Log(canceled?L"Input canceled / capture lost":L"Input up");
    if(twoFingerDrag) {
        dragPoints.erase(id);
        if(contacts.empty()) {twoFingerDrag=false;BounceToEdge();touchScale.Set(1,GetTickCount64(),160);}
        else {dragOrigin=ContactCentroid();GetWindowRect(window,&dragRect);}
        Redraw();return;
    }
    if(hit==Hit::Center) {
        if(buttonSent) ReleaseButton();
        else if(!canceled) Tap(hit,screen);
    }
    else if(hit==Hit::Drag) { BounceToEdge(); Send(0,false); Log(L"Screen contact moved"); }
    else if(hit==Hit::Ring&&!canceled&&!contact.moved&&GetTickCount64()-contact.started<350) Tap(hit,screen);
    else if(hit!=Hit::Ring && !canceled) {
        POINT p=screen; ScreenToClient(window,&p);
        if(HitTest(p.x*96.f/dpi,p.y*96.f/dpi)==hit) Action(hit);
    }
    if(contacts.empty()) touchScale.Set(1,GetTickCount64(),160);
    Redraw();
}

void DiscardTarget() {
    if(surfaceDC) { SelectObject(surfaceDC,previousSurface); DeleteObject(surface); DeleteDC(surfaceDC); }
    surfaceDC=nullptr;surface=nullptr;surfaceBits=nullptr;surfaceSize=0;
}
void Paint() {
    if(!window||!visualDirty||!IsWindowVisible(window)) return;
    RECT rc={}; GetWindowRect(window,&rc); int size=rc.right-rc.left;
    if(size<=0) return;
    if(surfaceSize!=size) {
        DiscardTarget(); BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        surfaceDC=CreateCompatibleDC(nullptr);surface=CreateDIBSection(surfaceDC,&info,DIB_RGB_COLORS,&surfaceBits,nullptr,0);
        if(!surface||!surfaceDC) return;
        previousSurface=SelectObject(surfaceDC,surface);surfaceSize=size;
    }
    // Partition every pixel between inner disk and annulus; coverage sums to one
    // at their shared edge, avoiding SourceCopy antialiasing holes.
    auto pixels=static_cast<UINT32*>(surfaceBits);
    double factor=size/240.0,center=size/2.0,outer=OuterRadius()*factor,inner=InnerRadius()*factor;
    double outerA=dial::Alpha(outerTransparency),innerA=dial::Alpha(innerTransparency);
    double idleA=dial::Alpha(idleTransparency);
    outerA+=(std::min(outerA,idleA)-outerA)*idleFade.value;
    innerA+=(std::min(innerA,idleA)-innerA)*idleFade.value;
    // Keep an imperceptible hit surface at fully idle transparency so touch can wake it.
    if(idleFade.value>0) {outerA=std::max(1.0,outerA);innerA=std::max(1.0,innerA);}
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        double dx=x+.5-center,dy=y+.5-center;
        double d=std::hypot(dx,dy);
        double cover=std::clamp(outer+.5-d,0.0,1.0),inside=std::clamp(inner+.5-d,0.0,1.0);
        double ia=inside*innerA*cover,oa=(1-inside)*outerA*cover;
        auto channel=[&](int shift){return UINT32(std::lround((((innerColor>>shift)&255)*ia+((outerColor>>shift)&255)*oa)/255));};
        pixels[y*size+x]=(UINT32(std::lround(ia+oa))<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
    }
    if(indicatorEnabled&&pointerFade.value>.001&&reveal.value>=.999) {
        Gdiplus::Bitmap bitmap(size,size,size*4,PixelFormat32bppPARGB,static_cast<BYTE*>(surfaceBits));
        Gdiplus::Graphics graphics(&bitmap);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        double a=pointerAngle*3.141592653589793/180;
        double r1=outer-std::max(5.0,outer*.14),r2=outer-std::max(3.0,outer*.045);
        Gdiplus::Pen pen(Gdiplus::Color(BYTE(235*pointerFade.value),BYTE(indicatorColor>>16),BYTE(indicatorColor>>8),BYTE(indicatorColor)),float(std::max(2.0,GetDpiForWindow(window)/48.0)));
        pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLine(&pen,float(center+std::cos(a)*r1),float(center+std::sin(a)*r1),float(center+std::cos(a)*r2),float(center+std::sin(a)*r2));
    }
    POINT at={rc.left,rc.top},origin={};SIZE dimensions={size,size};BLENDFUNCTION blend={AC_SRC_OVER,0,BYTE(std::lround(255*std::clamp(reveal.opacity,0.0,1.0))),AC_SRC_ALPHA};
    if(UpdateLayeredWindow(window,nullptr,&at,&dimensions,surfaceDC,&origin,0,&blend,ULW_ALPHA)) visualDirty=false;
}
void ReloadSettings() {
    if(settings.empty()) return;
    auto read=[](const wchar_t* key,int fallback,int lo,int hi) {return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Dial",key,fallback,settings.c_str())),lo,hi);};
    bool wasOnScreen=onScreen;int oldDiameter=diameterDip;
    bool wasGesture=gestureSummon;gestureSummon=licenseProbe.Owned()&&read(L"GestureSummon",0,0,1)!=0;
    rawTouch.Configure(read(L"SummonFingers",3,2,3),read(L"SummonDistanceDip",800,40,3000));
    if(window)ConfigureSummon();
    if(window&&wasGesture!=gestureSummon) {
        if(gestureSummon&&summonAvailable)Hide();else Show();
        if(gestureSummon&&!summonAvailable)ShowNotice(L"后台触摸不可用 · Ctrl+Alt+D 显示/隐藏");
    }
    mouseEnabled=read(L"MouseEnabled",0,0,1)!=0;
    indicatorEnabled=read(L"IndicatorEnabled",1,0,1)!=0;indicatorColor=read(L"IndicatorColor",0xf5f5f5,0,0xffffff);
    shakeEnabled=read(L"ShakeHide",0,0,1)!=0;shake.Reset();
    innerColor=read(L"InnerColor",0,0,0xffffff);outerColor=read(L"OuterColor",0x202020,0,0xffffff);
    innerTransparency=read(L"InnerTransparency",0,0,1000);outerTransparency=read(L"OuterTransparency",0,0,1000);
    sensitivityTenths=read(L"SensitivityTenths",LegacySensitivity(read(L"Speed",1,0,3)),1,40);reverse=read(L"Reverse",0,0,1)!=0;onScreen=read(L"OnScreen",0,0,1)!=0;
    if(window&&wasOnScreen!=onScreen)ModeNotice();
    if(wasOnScreen&&!onScreen)centerReportPending=true;
    if(onScreen){centerReportPending=false;centerLiftDue=0;}
    diameterDip=read(L"DiameterDip",164,96,360);
    idleDelayTenths=read(L"IdleDelayTenths",read(L"IdleSeconds",15,0,600)*10,0,6000);idleTransparency=read(L"IdleTransparency",800,0,1000);
    if(window&&(oldDiameter!=diameterDip||wasOnScreen!=onScreen)) {menuExpanded=false;nativeMenu=nullptr;menuRequestUntil=0;expansion.Set(0,GetTickCount64());ResizeDial();}
    Wake();Redraw();
}
// Observe the shell's own popup. Do not create a RadialController for CirDial:
// that would change the controller belonging to the foreground application.
BOOL CALLBACK FindNativeMenu(HWND candidate,LPARAM data) {
    if(candidate==window||!IsWindowVisible(candidate)) return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(candidate,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
    RECT r={},dialRect={};GetWindowRect(candidate,&r);GetWindowRect(window,&dialRect);
    int diameter=dialRect.right-dialRect.left,w=r.right-r.left,h=r.bottom-r.top;
    if(w<150||h<150||w>diameter*3||h>diameter*3||std::abs(w-h)>std::max(w,h)/4) return TRUE;
    if(onScreen&&(abs((r.left+r.right)-(dialRect.left+dialRect.right))>diameter/2||abs((r.top+r.bottom)-(dialRect.top+dialRect.bottom))>diameter/2)) return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(candidate,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return TRUE;wchar_t path[MAX_PATH]={};DWORD length=MAX_PATH;BOOL ok=QueryFullProcessImageNameW(process,0,path,&length);CloseHandle(process);
    if(!ok)return TRUE;
    const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
    if(!_wcsicmp(name,L"ShellExperienceHost.exe")||!_wcsicmp(name,L"InputDialExperienceHost.exe")||!_wcsicmp(name,L"ShellHost.exe")) {
        *reinterpret_cast<HWND*>(data)=candidate;return FALSE;
    }
    return TRUE;
}
void Animate() {
    auto now=GetTickCount64();
    if(reveal.Tick(now)||reveal.Active(now))Redraw();
    if(lifted&&!reveal.Active(now)&&reveal.to==0)ShowWindow(window,SW_HIDE);
    bool idle=!lifted&&idleDelayTenths>0&&contacts.empty()&&!pressed&&!programmaticHold&&!menuExpanded&&now-lastActivity>=ULONGLONG(idleDelayTenths)*100;
    idleFade.Set(idle?1:0,now,idle?350:140);
    if(idleFade.Tick(now)|pointerFade.Tick(now)) Redraw();
    if(confirmTime) {Redraw();if(now-confirmTime>=420)confirmTime=0;}
    if(!lifted&&(pressed||programmaticHold||now<menuRequestUntil)&&now-pressTime>=450&&!menuExpanded) {
        HWND detected=nullptr;EnumWindows(FindNativeMenu,reinterpret_cast<LPARAM>(&detected));
        // The HID contact footprint defines the native menu's central opening.
        // A local long hold is the fallback when the shell does not expose a HWND.
        if(detected||now-pressTime>=950) {
            nativeMenu=detected;menuExpanded=true;menuRequestUntil=0;expansion.Set(1,now,180);Log(detected?L"Native menu detected":L"Native menu expansion: hold fallback");}
    }
    if(menuExpanded&&nativeMenu&&now-menuPoll>150) {
        menuPoll=now;DWORD cloaked=0;DwmGetWindowAttribute(nativeMenu,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(!IsWindow(nativeMenu)||!IsWindowVisible(nativeMenu)||cloaked) {nativeMenu=nullptr;menuExpanded=false;expansion.Set(0,now);}
    }
    if(expansion.Tick(now)|touchScale.Tick(now)) Redraw();
    if(bouncing) {
        bounceX.Tick(now);bounceY.Tick(now);
        SetWindowPos(window,nullptr,int(std::lround(bounceX.value)),int(std::lround(bounceY.value)),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(!bounceX.Active(now)&&!bounceY.Active(now)) {bouncing=false;Save();}
    }
    Paint();
}
void Tray(DWORD command) {
    NOTIFYICONDATAW data={}; data.cbSize=sizeof(data); data.hWnd=window; data.uID=1;
    data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; data.uCallbackMessage=TrayMessage;
    data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101)); wcscpy_s(data.szTip,L"CirDial · 单击设置，右击退出 · Ctrl+Alt+D 显示/隐藏");
    Shell_NotifyIconW(command,&data);
}
void Show() {
    Wake();if(!IsWindowVisible(window)&&!smoke)reveal={0,0,0};reveal.Set(1,GetTickCount64(),440);BounceToEdge(); ShowWindow(window,SW_SHOWNOACTIVATE); lifted=false;
    Send(0,false); Log(L"Screen contact placed (shown)");
    overlayUntil=GetTickCount64()+30000; overlayPending=true;
}
void TrayMenu(bool exitMenu=false) {
    CancelInput();menuRequestUntil=0;menuExpanded=false;nativeMenu=nullptr;expansion.Set(0,GetTickCount64());
    HWND existing=FindWindowW(nullptr,L"CirDial Settings");
    if(existing&&!exitMenu) { SetForegroundWindow(existing);return; }
    wchar_t path[MAX_PATH]={};GetModuleFileNameW(nullptr,path,MAX_PATH);
    std::wstring exe(path);exe=exe.substr(0,exe.find_last_of(L"\\/"))+L"\\Settings\\CirDial.Settings.exe";
    // Store purchases must run at the interactive user's ordinary integrity level.
    DWORD shellPid=0;GetWindowThreadProcessId(GetShellWindow(),&shellPid);
    HANDLE shell=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,shellPid),token=nullptr,primary=nullptr;
    bool launched=false;
    if(shell&&OpenProcessToken(shell,TOKEN_QUERY|TOKEN_DUPLICATE,&token)&&DuplicateTokenEx(token,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY,nullptr,SecurityImpersonation,TokenPrimary,&primary)) {
        std::wstring command=L"\""+exe+L"\""+(exitMenu?L" --tray-exit":L"");
        STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION process={};
        launched=CreateProcessWithTokenW(primary,LOGON_WITH_PROFILE,exe.c_str(),command.data(),0,nullptr,nullptr,&startup,&process)!=FALSE;
        if(launched){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    }
    if(primary)CloseHandle(primary);if(token)CloseHandle(token);if(shell)CloseHandle(shell);
    if(launched)return;
    Log(L"Ordinary settings launch failed; purchase remains unavailable in elevated fallback",GetLastError());
    auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",exe.c_str(),exitMenu?L"--tray-exit":nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32) MessageBoxW(window,L"无法打开 WinUI 3 设置，请使用完整安装包更新。",L"CirDial",MB_ICONERROR);
}
bool InsideDial(POINT p) {
    if(lifted)return false;RECT r={};GetWindowRect(window,&r);
    return std::hypot(double(p.x-(r.left+r.right)/2),double(p.y-(r.top+r.bottom)/2))<=OuterRadius()*dpi/96;
}
void HandleTap(const dial::TouchTap::Result& result,bool background) {
    if(!gestureSummon||sessionLocked||!result.fired||GetTickCount64()-lastSummon<650)return;
    if(background&&(lifted||!result.anyInside)) {
        CancelInput();menuExpanded=false;menuRequestUntil=0;nativeMenu=nullptr;expansion={};confirmTime=0;reveal={0,0,0};
        Redraw();Paint(); // Clear the old frame before moving a visible layered window.
        lastSummon=GetTickCount64();RECT r={};GetWindowRect(window,&r);
        SetWindowPos(window,nullptr,result.center.x-(r.right-r.left)/2,result.center.y-(r.bottom-r.top)/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);Show();
        Log(L"Global gesture: summon");
    }
}
void DisableRelay(){StopRelay();gestureSummon=false;globalTap.Reset();WritePrivateProfileStringW(L"Dial",L"GestureSummon",L"0",settings.c_str());Show();ShowNotice(L"手势召唤已关闭 · Touch relay off");}
LRESULT CALLBACK ObserverProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_POINTERDOWN||message==WM_POINTERUPDATE||message==WM_POINTERUP) {
        if(observing&&!sessionLocked){
            std::vector<POINTER_TOUCH_INFO> frame;
            if(!touchRelay.Forward(GET_POINTERID_WPARAM(wp),frame)){Log(L"Touch relay failed; releasing global input",GetLastError());DisableRelay();return 0;}
            for(auto& touch:frame){auto& p=touch.pointerInfo;HandleTap(globalTap.Feed(p.pointerId,p.ptPixelLocation,(p.pointerFlags&POINTER_FLAG_DOWN)!=0,(p.pointerFlags&POINTER_FLAG_UP)!=0,(p.pointerFlags&POINTER_FLAG_CANCELED)!=0,InsideDial(p.ptPixelLocation),GetTickCount64()),true);}
        }
        return 0;
    }
    if(message==WM_TIMER&&observing&&!touchRelay.Heartbeat()){Log(L"Touch relay heartbeat failed",GetLastError());DisableRelay();}
    return DefWindowProcW(hwnd,message,wp,lp);
}
void ConfigureSummon() {
    unsigned n=GetPrivateProfileIntW(L"Dial",L"SummonFingers",3,settings.c_str());n=std::clamp(n,2u,3u);
    unsigned gap=GetPrivateProfileIntW(L"Dial",L"SummonDistanceDip",800,settings.c_str());gap=std::clamp(gap,40u,3000u);
    globalTap.Configure(n,gap,GetDpiForWindow(window)/96.0);localTap.Configure(n,gap,GetDpiForWindow(window)/96.0);
    rawTouch.Enable(window,false);
    if(!gestureSummon||smoke||sessionLocked||!relayEmergencyKey){StopRelay();return;}
    if(observing){summonAvailable=true;return;}
    if(!observerWindow){
        WNDCLASSW wc={};wc.lpfnWndProc=ObserverProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CirDial.Touch.Relay";RegisterClassW(&wc);
        observerWindow=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    }
    observing=observerWindow&&InitializeTouchInjection(MAX_TOUCH_COUNT,TOUCH_FEEDBACK_NONE)&&RegisterPointerInputTarget(observerWindow,PT_TOUCH);
    summonAvailable=observing;if(observing)SetTimer(observerWindow,1,50,nullptr);
    Log(observing?L"Global touch relay registered":L"Global touch relay unavailable",observing?0:GetLastError());
}
LRESULT CALLBACK NoticeProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_TIMER){KillTimer(hwnd,1);ShowWindow(hwnd,SW_HIDE);return 0;}
    if(message==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);HBRUSH b=CreateSolidBrush(RGB(32,32,32));FillRect(dc,&r,b);DeleteObject(b);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(250,250,250));HFONT font=CreateFontW(-MulDiv(17,GetDpiForWindow(hwnd),96),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");auto old=SelectObject(dc,font);
        wchar_t text[160]={};GetWindowTextW(hwnd,text,160);DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);DeleteObject(font);EndPaint(hwnd,&ps);return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
void ShowNotice(const std::wstring& text) {
    if(smoke||text.empty())return;
    if(!noticeWindow){WNDCLASSW wc={};wc.lpfnWndProc=NoticeProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CirDial.Mode.Notice";RegisterClassW(&wc);noticeWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,wc.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);}
    if(!noticeWindow)return;
    SetWindowTextW(noticeWindow,text.c_str());
    auto monitor=Primary();double scale=GetDpiForWindow(window)/96.0;int w=int(420*scale),h=int(60*scale);
    SetWindowRgn(noticeWindow,CreateRoundRectRgn(0,0,w,h,int(16*scale),int(16*scale)),FALSE);SetLayeredWindowAttributes(noticeWindow,0,238,LWA_ALPHA);
    SetWindowPos(noticeWindow,HWND_TOPMOST,(monitor.rcMonitor.left+monitor.rcMonitor.right-w)/2,monitor.rcMonitor.top+(monitor.rcMonitor.bottom-monitor.rcMonitor.top)/3-h/2,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(noticeWindow,nullptr,FALSE);SetTimer(noticeWindow,1,1700,nullptr);
}
void ModeNotice() {
    int language=GetPrivateProfileIntW(L"Dial",L"Language",0,settings.c_str());bool chinese=language==1||language==2||(language==0&&PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE);
    ShowNotice(chinese?(onScreen?L"Surface Dial 屏幕交互模式：已开启":L"Surface Dial 屏幕交互模式：已关闭"):(onScreen?L"Surface Dial screen interaction: On":L"Surface Dial screen interaction: Off"));
}
bool PassMouse(bool enabled,bool known,INPUT_MESSAGE_DEVICE_TYPE type,bool promoted) { return !enabled&&known&&type==IMDT_MOUSE&&!promoted; }
LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_INPUT_DEVICE_CHANGE: rawTouch.Remove(reinterpret_cast<HANDLE>(lp));return 0;
    case WM_INPUT: {
        POINT center={};
        auto before=rawTouch.DeviceCount();
        bool summon=!sessionLocked&&rawTouch.Input(reinterpret_cast<HRAWINPUT>(lp),center);
        if(rawTouch.DeviceCount()!=before)Log(L"Background touchscreen HID reports received");
        if(summon&&lifted&&GetTickCount64()-lastSummon>650) {
            lastSummon=GetTickCount64();{
                RECT rc={};GetWindowRect(window,&rc);
                SetWindowPos(window,nullptr,center.x-(rc.right-rc.left)/2,center.y-(rc.bottom-rc.top)/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                Show();
            }
            Log(L"Raw background touch summon recognized");
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    case WM_APP+5: return smoke?static_cast<LRESULT>(contacts.size()):0;
    case WM_NCHITTEST:
        if(!mouseEnabled) {
            INPUT_MESSAGE_SOURCE source={};
            const bool known=GetCurrentInputMessageSource(&source)!=FALSE;
            const bool promoted=(static_cast<ULONG_PTR>(GetMessageExtraInfo())&0xffffff00)==0xff515700;
            // Synchronous hit tests can have no input source. Never reject a
            // potential touch before Windows has delivered its pointer message.
            if(PassMouse(mouseEnabled,known,source.deviceType,promoted)) return HTTRANSPARENT;
        }
        return HTCLIENT;
    case WM_TOUCH: {
        UINT count=LOWORD(wp); HTOUCHINPUT handle=reinterpret_cast<HTOUCHINPUT>(lp);
        std::vector<TOUCHINPUT> inputs(count<=64?count:0);
        if(!inputs.empty()&&GetTouchInputInfo(handle,count,inputs.data(),sizeof(TOUCHINPUT))) {
            for(const auto& input:inputs) {
                UINT32 id=input.dwID|0x40000000; POINT point={TOUCH_COORD_TO_PIXEL(input.x),TOUCH_COORD_TO_PIXEL(input.y)};
                if(gestureSummon)HandleTap(localTap.Feed(id,point,(input.dwFlags&TOUCHEVENTF_DOWN)!=0,(input.dwFlags&TOUCHEVENTF_UP)!=0,false,InsideDial(point),GetTickCount64()),false);
                if(input.dwFlags&TOUCHEVENTF_DOWN) Down(id,point);
                else if(input.dwFlags&TOUCHEVENTF_UP) Up(id,point);
                else if(input.dwFlags&TOUCHEVENTF_MOVE) Move(id,point);
            }
        }
        CloseTouchInputHandle(handle);return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_POINTERACTIVATE: return PA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps;BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);Redraw();Paint();return 0; }
    case WM_SIZE: Redraw(); return 0;
    case WM_DPICHANGED: DiscardTarget(); ResizeDial(); return 0;
    case WM_DISPLAYCHANGE: StopRelay();CancelInput();rawTouch.Reset();ConfigureSummon();ResizeDial();return 0;
    case WM_MOVE: positionDirty=true; return 0;
    case WM_RBUTTONUP: if(mouseEnabled) TrayMenu(false); return 0;
    case WM_HOTKEY: if(wp==1) { if(IsWindowVisible(window)) Hide(); else Show(); } else if(wp==2)DisableRelay(); return 0;
    case WM_POINTERDOWN: case WM_POINTERUPDATE: case WM_POINTERUP: {
        UINT32 id=GET_POINTERID_WPARAM(wp); POINTER_INFO info={};
        if(!GetPointerInfo(id,&info)) { Up(id,{},true); return 0; }
        if(info.pointerType==PT_MOUSE&&!mouseEnabled) return 0;
        if(gestureSummon&&info.pointerType==PT_TOUCH)HandleTap(localTap.Feed(id,info.ptPixelLocation,message==WM_POINTERDOWN,message==WM_POINTERUP,(info.pointerFlags&POINTER_FLAG_CANCELED)!=0,InsideDial(info.ptPixelLocation),GetTickCount64()),false);
        if(info.pointerFlags&POINTER_FLAG_CANCELED) Up(id,info.ptPixelLocation,true);
        else if(message==WM_POINTERDOWN) Down(id,info.ptPixelLocation);
        else if(message==WM_POINTERUP) Up(id,info.ptPixelLocation);
        else Move(id,info.ptPixelLocation);
        return 0;
    }
    case WM_POINTERCAPTURECHANGED: Up(GET_POINTERID_WPARAM(wp),{},true); return 0;
    case WM_LBUTTONDOWN: case WM_MOUSEMOVE: case WM_LBUTTONUP: {
        if(!mouseEnabled) return 0;
        // Ignore mouse events synthesized from a touch/pen contact already handled above.
        if((static_cast<ULONG_PTR>(GetMessageExtraInfo())&0xffffff00)==0xff515700) return 0;
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(hwnd,&p);
        if(message==WM_LBUTTONDOWN) { SetCapture(hwnd); Down(MouseId,p); }
        else if(message==WM_LBUTTONUP) { Up(MouseId,p); if(GetCapture()==hwnd) ReleaseCapture(); }
        else if(wp&MK_LBUTTON) Move(MouseId,p);
        return 0;
    }
    case WM_CAPTURECHANGED: Up(MouseId,{},true); return 0;
    case WM_MOUSEWHEEL: if(mouseEnabled) Rotate(GET_WHEEL_DELTA_WPARAM(wp)*15.0/WHEEL_DELTA); return 0;
    case WM_CANCELMODE: Log(L"WM_CANCELMODE: releasing input; keeping dial visible"); CancelInput(); overlayPending=true; return 0;
    case WM_POWERBROADCAST: if(wp==PBT_APMSUSPEND) { sessionLocked=true; StopRelay(); rawTouch.Reset();globalTap.Reset();localTap.Reset(); CancelInput(); Send(0,false); CloseDevice(); } else if(wp==PBT_APMRESUMEAUTOMATIC) { sessionLocked=false; Connect();RefreshLicense();ConfigureSummon(); } return TRUE;
    case WM_WTSSESSION_CHANGE: if(wp==WTS_SESSION_LOCK) { sessionLocked=true; StopRelay(); rawTouch.Reset();globalTap.Reset();localTap.Reset(); CancelInput(); Send(0,false); } else if(wp==WTS_SESSION_UNLOCK) { sessionLocked=false; Send(0,false);RefreshLicense();ConfigureSummon(); } return 0;
    case WM_TIMER:
        if(centerLiftDue&&GetTickCount64()>=centerLiftDue) {centerLiftDue=0;if(!onScreen)Send(0,pressed);}
        if(!smoke&&GetTickCount64()-licenseChecked>300000&&!licenseProbe.Busy())RefreshLicense();
        Animate();
        if(programmaticHold&&GetTickCount64()-pressTime>=1200) { programmaticHold=false;ReleaseButton(); }
        if(pendingTap!=Hit::None&&GetTickCount64()-pendingTapTime>GetDoubleClickTime()) {
            ResolveTaps();
        }
        {
            bool hold=false;
            for(auto& item:contacts) {
                auto& contact=item.second;
                if(contact.hit==Hit::Center&&!contact.buttonSent&&GetTickCount64()-contact.started>=350&&Connected()) {
                    pendingTap=Hit::None;
                    tapCount=0;contact.buttonSent=true; hold=true; break;
                }
            }
            if(hold) { pressTime=GetTickCount64(); Send(0,true); Log(L"Center gesture committed: hold"); }
        }
        if(overlayPending||GetTickCount64()<overlayUntil) { overlayPending=false; MaintainOverlay(); }
        if(positionDirty&&Connected()) { positionDirty=false; Send(0,pressed); }
        // A missed release can never leave a button held indefinitely.
        if(pressed&&GetTickCount64()-pressTime>10000) CancelInput();
        if(!Connected()&&backendInitialized){backendInitialized=false;CancelInput();Redraw();}
        if((!Connected()||!backendInitialized)&&GetTickCount64()-lastConnect>2000) Connect();
        if(smoke&&!preview&&GetTickCount64()-startTime>2500) {DestroyWindow(hwnd);return 0;}
        {
            auto now=GetTickCount64();
            bool busy=!contacts.empty()||pendingTap!=Hit::None||programmaticHold||pressed||bouncing||confirmTime||centerLiftDue||dial::PendingDeadline(menuRequestUntil,now)||
                reveal.Active(now)||expansion.Active(now)||touchScale.Active(now)||idleFade.Active(now)||pointerFade.Active(now);
            UINT next=dial::TimerDelay(busy,lifted&&!IsWindowVisible(hwnd));
            if(next!=timerInterval){timerInterval=next;SetTimer(hwnd,Timer,next,nullptr);}
        }
        return 0;
    case WM_APP+6: ReloadSettings(); return 0;
    case WM_APP+4: CancelInput();if(wp==1) {if(lifted)Show();else Hide();} else if(wp==2)DestroyWindow(hwnd);else if(wp==3)ToggleScreenMode();else if(wp==4)Show();else if(wp==5)Hide();else if(wp==6)RefreshLicense();else {ReloadSettings();Send(0,false);} return 0;
    case ShowMessage: Show(); return 0;
    case TrayMessage:
        if(lp==WM_LBUTTONUP) TrayMenu(false);else if(lp==WM_RBUTTONUP) TrayMenu(true);
        return 0;
    case WM_QUERYENDSESSION: lifted=true; CancelInput(); Send(0,false); Save(); return TRUE;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if(overlayHook) { UnhookWinEvent(overlayHook); overlayHook=nullptr; }
        StopRelay();if(observerWindow)DestroyWindow(observerWindow);if(noticeWindow)DestroyWindow(noticeWindow);rawTouch.Enable(hwnd,false);closing=true; lifted=true; CancelInput(); Send(0,false); Save(); UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2); Log(L"Exiting; input released"); KillTimer(hwnd,Timer); Tray(NIM_DELETE); WTSUnRegisterSessionNotification(hwnd); CloseDevice(); PostQuitMessage(0); return 0;
    default: if(taskbarCreated&&message==taskbarCreated&&!closing) Tray(NIM_ADD); break;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
bool SelfTest() {
    if(!dial::DialReveal::SelfTest())return false;
    if(!dial::InnerArc(20,0,17.32,10)||dial::InnerArc(20,0,32,0)||dial::InnerArc(0,0,10,0))return false;
    if(LegacySensitivity(0)!=5||LegacySensitivity(1)!=10||LegacySensitivity(2)!=20||LegacySensitivity(3)!=40)return false;
    if(!dial::TouchRelay::SelfTest()||!dial::RawTouch::SelfTest()||!dial::TouchTap::SelfTest()||!dial::Shake::SelfTest())return false;
    struct CaptureBackend: dial::IDialBackend {
        std::vector<VIRTUAL_DIAL_COMMAND_V2> reports;
        void Connect()override{}void Close()override{}bool Connected()const override{return true;}
        bool Send(const VIRTUAL_DIAL_COMMAND_V2& r)override{reports.push_back(r);return true;}
        bool Caps(VIRTUAL_DIAL_CAPS&)override{return true;}DWORD Error()const override{return 0;}
    };
    auto capture=std::make_unique<CaptureBackend>();auto* observed=capture.get();backend=std::move(capture);
    positionCapable=true;onScreen=false;lifted=true;centerReportPending=true;
    Send(0,false);Send(0,false); // Includes an immediate ordinary position update while hidden.
    bool centered=observed->reports.size()==1&&observed->reports[0].PositionValid&&observed->reports[0].X==dial::Normalize(.5)&&observed->reports[0].Y==dial::Normalize(.5);
    centerLiftDue=0;Send(0,false);centered=centered&&observed->reports.size()==2&&!observed->reports[1].PositionValid;
    backend.reset();positionCapable=false;if(!centered)return false;
    if(!dial::SchedulerTest())return false;
    dial::Accumulator a;
    if(PassMouse(false,false,IMDT_UNAVAILABLE,false)||PassMouse(false,true,IMDT_TOUCH,false)||PassMouse(false,true,IMDT_PEN,false)||PassMouse(false,true,IMDT_MOUSE,true)||PassMouse(true,true,IMDT_MOUSE,false)||!PassMouse(false,true,IMDT_MOUSE,false)) return false;
    bool ok=std::abs(dial::Delta(179,-179)-2)<0.001 && std::abs(dial::Delta(-179,179)+2)<0.001;
    ok=ok&&a.Add(15,1,false)==150; a.Reset();
    ok=ok&&a.Add(15,2,true)==-300; a.Reset();
    long total=0; for(int i=0;i<100;++i) total+=a.Add(0.03,1,false);
    ok=ok&&std::abs(total-30)<=1;
    ok=ok&&HitTest(120,120)==Hit::Center&&HitTest(120+float(normalRadius*.8),120)==Hit::Ring&&HitTest(120+float(normalRadius+10),120)==Hit::None;
    ok=ok&&!dial::StartDrag(15,0,192)&&dial::StartDrag(17,0,192)&&dial::StartDrag(9,0,96);
    ok=ok&&dial::Alpha(0)==255&&dial::Alpha(1000)==0&&dial::Alpha(200)==204;
    ok=ok&&dial::Edge(-100,0,1000,200)==0&&dial::Edge(950,0,1000,200)==800;
    dial::Tween tween; tween.Set(1,100,200);tween.Tick(200);ok=ok&&tween.value>0&&tween.value<1;tween.Tick(300);ok=ok&&tween.value==1;
    ok=ok&&HitTest(0,0)==Hit::None&&dial::Normalize(0)==0&&dial::Normalize(1)==32767&&dial::Normalize(2)==32767;
    auto p=dial::ToNative(0.2,0.3,1); ok=ok&&std::abs(p.x-0.7)<0.0001&&std::abs(p.y-0.2)<0.0001;
    static_assert(sizeof(VIRTUAL_DIAL_COMMAND_V2)==16,"Position ABI changed");
    a.Reset(); double angle=170; total=0;
    for(int i=0;i<360;++i) { double next=std::remainder(angle+1,360.0); total+=a.Add(dial::Delta(angle,next),1,false); angle=next; }
    ok=ok&&total==3600;
    a.Reset(); ok=ok&&a.Add(-15,0.5,false)==-75;
    a.Reset(); ok=ok&&a.Add(180,4,true)==-7200;
    static_assert(sizeof(VIRTUAL_DIAL_COMMAND)==12,"IOCTL ABI changed");
    return ok;
}
bool GestureTest() {
    RECT rc={};GetWindowRect(window,&rc);
    POINT center={(rc.left+rc.right)/2,(rc.top+rc.bottom)/2};
    POINT ring={center.x+int(normalRadius*.8*dpi/96),center.y};
    bool ok=true;
    for(POINT point:{center,ring}) {
        CancelInput();menuExpanded=false;expansion={};
        Down(1,point);Up(1,point);ok=ok&&pendingTap!=Hit::None&&!programmaticHold;
        Down(1,point);Up(1,point);ok=ok&&!programmaticHold&&tapCount==2;ResolveTaps();ok=ok&&programmaticHold;
        pressTime-=1000;Animate();ok=ok&&menuExpanded;
        Down(1,point);Up(1,point);ok=ok&&!menuExpanded&&!programmaticHold;
    }
    CancelInput();Down(1,center);POINT moved={center.x+40,center.y+40};Move(1,moved);
    ok=ok&&Owns(Hit::Drag)&&!pressed;Up(1,moved);ok=ok&&pendingTap==Hit::None;
    CancelInput();Down(1,ring);Move(1,{center.x,center.y+int(normalRadius*.8*dpi/96)});Up(1,ring);
    ok=ok&&pendingTap==Hit::None;
    CancelInput();bool initialMode=onScreen;
    for(POINT point:{center,ring}) {
        for(int i=0;i<3;++i){Down(1,point);Up(1,point);}
        ok=ok&&onScreen!=initialMode&&!programmaticHold;initialMode=onScreen;
    }
    CancelInput();GetWindowRect(window,&rc);
    center={(rc.left+rc.right)/2,(rc.top+rc.bottom)/2};
    POINT other={center.x+int(normalRadius*.7*dpi/96),center.y};
    Down(11,center);Down(12,other);
    ok=ok&&twoFingerDrag&&contacts.size()==2&&!pressed;
    Move(11,{center.x+40,center.y+20});Move(12,{other.x+40,other.y+20});
    RECT after={};GetWindowRect(window,&after);
    ok=ok&&after.left==rc.left+40&&after.top==rc.top+20;
    Up(11,center);ok=ok&&twoFingerDrag&&contacts.size()==1;
    Move(12,{other.x+50,other.y+25});GetWindowRect(window,&after);
    ok=ok&&after.left==rc.left+50&&after.top==rc.top+25;
    Up(12,other);ok=ok&&!twoFingerDrag&&pendingTap==Hit::None&&!programmaticHold;
    CancelInput();GetWindowRect(window,&rc);center={(rc.left+rc.right)/2,(rc.top+rc.bottom)/2};
    int innerDistance=int(InnerRadius()*.8*dpi/96);
    POINT innerStart={center.x+innerDistance,center.y},innerEnd={center.x,center.y+innerDistance};
    Down(21,innerStart);Move(21,innerEnd);
    RECT still={};GetWindowRect(window,&still);
    ok=ok&&Owns(Hit::Ring)&&still.left==rc.left&&still.top==rc.top;
    Up(21,innerEnd);ok=ok&&pendingTap==Hit::None;
    CancelInput();bool savedGesture=gestureSummon;gestureSummon=true;lastSummon=0;
    POINT destination={center.x+150,center.y+120};
    HandleTap({true,false,destination,false},true);GetWindowRect(window,&still);
    ok=ok&&!lifted&&(still.left+still.right)/2==destination.x&&(still.top+still.bottom)/2==destination.y&&reveal.to==1;
    lastSummon=0;HandleTap({true,true,center,true},true);RECT unchanged={};GetWindowRect(window,&unchanged);
    ok=ok&&unchanged.left==still.left&&unchanged.top==still.top;
    gestureSummon=savedGesture;reveal={1,1,1};CancelInput();return ok;
}
bool VisualTest() {
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    wchar_t exe[MAX_PATH]={};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring folder(exe);folder=folder.substr(0,folder.find_last_of(L"\\/"))+L"\\Visuals";CreateDirectoryW(folder.c_str(),nullptr);
    bool ok=true;confirmTime=0;touchScale={1,1,1};idleFade={};expansion={};
    double radius=OuterRadius(),inner=InnerRadius();
    expansion.value=1;ok=ok&&std::abs((OuterRadius()-inner)/(radius-inner)-1.5)<.001;expansion={};
    for(int state=0;state<4;++state) {
        expansion.value=state==1?1:0;idleFade.value=state==2?1:0;pointerFade.value=state==3?1:0;pointerAngle=225;
        Redraw();Paint();
        if(state==0) { // There must be no transparent seam on the interior joint.
            int c=surfaceSize/2,r=int(InnerRadius()*surfaceSize/240);
            auto data=static_cast<UINT32*>(surfaceBits);
            for(int x=c+r-2;x<=c+r+2;++x)ok=ok&&((data[c*surfaceSize+x]>>24)==255);
        }
        Gdiplus::Bitmap bitmap(surfaceSize,surfaceSize,surfaceSize*4,PixelFormat32bppPARGB,static_cast<BYTE*>(surfaceBits));
        std::wstring file=folder+L"\\"+std::to_wstring(state)+L".png";
        ok=ok&&bitmap.Save(file.c_str(),&png)==Gdiplus::Ok;
    }
    expansion={};idleFade={};pointerFade={};
    for(int direction=0;direction<2;++direction){
        reveal=direction?dial::DialReveal(1,1,1):dial::DialReveal(0,0,0);
        reveal.Set(direction?0:1,100,1000);
        for(int frameIndex=1;frameIndex<=4;++frameIndex){
            reveal.Tick(100+frameIndex*250);Redraw();Paint();
            Gdiplus::Bitmap bitmap(surfaceSize,surfaceSize,surfaceSize*4,PixelFormat32bppPARGB,static_cast<BYTE*>(surfaceBits));
            std::wstring file=folder+L"\\"+(direction?L"hide-":L"show-")+std::to_wstring(frameIndex)+L".png";
            ok=ok&&bitmap.Save(file.c_str(),&png)==Gdiplus::Ok;
        }
    }
    reveal={1,1,1};return ok;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int) {
    if(wcsstr(args,L"--observer-check")) {
        WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"CirDial.Observer.Check";RegisterClassW(&wc);
        HWND check=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
        DWORD result=ERROR_NOT_SUPPORTED;
        if(check&&InitializeTouchInjection(MAX_TOUCH_COUNT,TOUCH_FEEDBACK_NONE)){result=RegisterPointerInputTarget(check,PT_TOUCH)?0:GetLastError();if(!result)UnregisterPointerInputTarget(check,PT_TOUCH);}
        if(check)DestroyWindow(check);return int(result);
    }
    usbipBackend=wcsstr(args,L"--backend=vhf")==nullptr;
    if(wcsstr(args,L"--uiaccess-check")) {
        HANDLE token=nullptr; DWORD value=0,bytes=0;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return 9;
        BOOL ok=GetTokenInformation(token,TokenUIAccess,&value,sizeof(value),&bytes); CloseHandle(token);
        return ok&&value?0:9;
    }
    if(wcsstr(args,L"--self-test")) return SelfTest()?0:1;
    preview=wcsstr(args,L"--preview")!=nullptr;
    interactionTest=wcsstr(args,L"--interaction-test")!=nullptr;
    bool visualTest=wcsstr(args,L"--visual-test")!=nullptr;
    smoke=preview||interactionTest||visualTest||wcsstr(args,L"--smoke-test")!=nullptr;
    if(!smoke) {
        SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY; PSID admin=nullptr; BOOL elevated=FALSE;
        if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admin)) {
            CheckTokenMembership(nullptr,admin,&elevated); FreeSid(admin);
        }
        if(!elevated) {
            wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr,path,MAX_PATH);
            SHELLEXECUTEINFOW execute={}; execute.cbSize=sizeof(execute); execute.lpVerb=L"runas"; execute.lpFile=path; execute.lpParameters=args; execute.nShow=SW_SHOWNOACTIVATE;
            if(!ShellExecuteExW(&execute)) {
                MessageBoxW(nullptr,L"CirDial 需要管理员权限连接已安装的驱动。请重新启动并允许 Windows 授权。",L"CirDial",MB_OK|MB_ICONINFORMATION); return 7;
            }
            return 0;
        }
    }
    if(wcsstr(args,L"--quit")) {
        HWND existing=FindWindowW(smoke?L"CirDial.Preview.1":ClassName,nullptr);
        if(existing) {
            PostMessageW(existing,WM_CLOSE,0,0);
            for(int i=0;i<60&&IsWindow(existing);++i) Sleep(50);
            if(IsWindow(existing)) return 8;
        }
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,smoke?L"Local\\CirDial.Smoke":L"Local\\CirDial.Touch");
    if(!mutex) return 2;
    const wchar_t* selectedClass=smoke?L"CirDial.Preview.1":ClassName;
    if(GetLastError()==ERROR_ALREADY_EXISTS) { HWND existing=FindWindowW(selectedClass,nullptr); if(existing) PostMessageW(existing,ShowMessage,0,0); CloseHandle(mutex); return 0; }
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) { CloseHandle(mutex); return 3; }
    Gdiplus::GdiplusStartupInput graphicsInput;
    if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok) {CoUninitialize();CloseHandle(mutex);return 4;}
    int x=CW_USEDEFAULT,y=CW_USEDEFAULT;
    if(!smoke) {
        PWSTR local=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) {
            settings=std::wstring(local)+L"\\VDial"; CreateDirectoryW(settings.c_str(),nullptr); logPath=settings+L"\\app.log"; settings+=L"\\settings.ini"; CoTaskMemFree(local);
            Log(L"CirDial 0.5.0 preview on-screen starting");
            HANDLE token=nullptr; DWORD uiAccess=0,bytes=0;
            if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
                GetTokenInformation(token,TokenUIAccess,&uiAccess,sizeof(uiAccess),&bytes); CloseHandle(token);
            }
            Log(uiAccess?L"TokenUIAccess=1":L"TokenUIAccess=0");
            sensitivityTenths=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Dial",L"SensitivityTenths",LegacySensitivity(GetPrivateProfileIntW(L"Dial",L"Speed",1,settings.c_str())),settings.c_str())),1,40);
            opacity=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Dial",L"Opacity",80,settings.c_str())),40,100);
            onScreen=GetPrivateProfileIntW(L"Dial",L"OnScreen",0,settings.c_str())!=0;
            reverse=GetPrivateProfileIntW(L"Dial",L"Reverse",0,settings.c_str())!=0;
            x=static_cast<int>(GetPrivateProfileIntW(L"Dial",L"X",80,settings.c_str())); y=static_cast<int>(GetPrivateProfileIntW(L"Dial",L"Y",80,settings.c_str()));
        }
    }
    ReloadSettings();
    WNDCLASSW wc={}; wc.lpfnWndProc=Proc; wc.hInstance=instance; wc.lpszClassName=selectedClass; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); RegisterClassW(&wc);
    dpi=static_cast<float>(GetDpiForSystem());
    window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_NOACTIVATE|(preview?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW),selectedClass,L"CirDial 屏上拨盘",WS_POPUP,x,y,static_cast<int>(Width*dpi/96),static_cast<int>(Height*dpi/96),nullptr,nullptr,instance,nullptr);
    int result=5;
    if(window) {
        // Only fixed, argument-free CirDial commands cross the integrity boundary.
        ChangeWindowMessageFilterEx(window,WM_APP+4,MSGFLT_ALLOW,nullptr);
        if(!RegisterTouchWindow(window,TWF_FINETOUCH)) Log(L"RegisterTouchWindow failed",GetLastError());
        ResizeDial();
        overlayHook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_REORDER,nullptr,OverlayEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        if(!smoke) { Tray(NIM_ADD); RegisterHotKey(window,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'D'); relayEmergencyKey=RegisterHotKey(window,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_ESCAPE); WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION); Connect(); }
        if(!smoke)RefreshLicense();
        startTime=GetTickCount64(); SetTimer(window,Timer,16,nullptr);
        ConfigureSummon();
        if(gestureSummon&&summonAvailable&&!smoke){reveal={0,0,0};lifted=true;}
        else Show();
        UpdateWindow(window);
        if(visualTest) {bool passed=VisualTest();DestroyWindow(window);DiscardTarget();Gdiplus::GdiplusShutdown(graphicsToken);CoUninitialize();CloseHandle(mutex);return passed?0:11;}
        if(interactionTest) { bool passed=GestureTest();DestroyWindow(window);DiscardTarget();Gdiplus::GdiplusShutdown(graphicsToken);CoUninitialize();CloseHandle(mutex);return passed?0:10; }
        MSG message={}; BOOL status;
        while((status=GetMessageW(&message,nullptr,0,0))>0) { TranslateMessage(&message); DispatchMessageW(&message); }
        result=status<0?6:0;
    }
    DiscardTarget(); Gdiplus::GdiplusShutdown(graphicsToken); CoUninitialize(); CloseHandle(mutex);
    return result;
}

