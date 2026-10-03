#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <RadialControllerInterop.h>
#include <initguid.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include "../../Shared/VirtualDialProtocol.h"
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"windowsapp.lib")
using namespace winrt;
using namespace Windows::UI::Input;
static HANDLE device=INVALID_HANDLE_VALUE;
static HWND window=nullptr;
static unsigned ticks=0,contacts=0;
static bool ended=false,failed=false;
static double expectedX=0,expectedY=0,expectedWidth=0;
static FILE* logFile=nullptr;
static RadialController controller{nullptr};
static RadialControllerMenuItem probeMenuItem{nullptr};
static void Log(const char* line) { std::puts(line); if(logFile) { std::fprintf(logFile,"%s\n",line); std::fflush(logFile); } }
static HANDLE Open() {
    HDEVINFO list=SetupDiGetClassDevsW(&GUID_DEVINTERFACE_VIRTUAL_DIAL,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list==INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA data={}; data.cbSize=sizeof(data); HANDLE h=INVALID_HANDLE_VALUE;
    if(SetupDiEnumDeviceInterfaces(list,nullptr,&GUID_DEVINTERFACE_VIRTUAL_DIAL,0,&data)) {
        DWORD bytes=0; SetupDiGetDeviceInterfaceDetailW(list,&data,nullptr,0,&bytes,nullptr);
        if(bytes>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            std::vector<BYTE> buffer(bytes); auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data()); detail->cbSize=sizeof(*detail);
            if(SetupDiGetDeviceInterfaceDetailW(list,&data,detail,bytes,nullptr,nullptr)) h=CreateFileW(detail->DevicePath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        }
    }
    SetupDiDestroyDeviceInfoList(list); return h;
}
static void Send(bool on) {
    if(on&&GetForegroundWindow()!=window) { failed=true; Log("Probe is not foreground; skipped input"); return; }
    VIRTUAL_DIAL_COMMAND_V2 c={VIRTUAL_DIAL_POSITION_VERSION,on?10L:0L,0,static_cast<BYTE>(on),16384,16384,0}; DWORD count=0;
    if(!DeviceIoControl(device,IOCTL_VIRTUAL_DIAL_SEND,&c,sizeof(c),nullptr,0,&count,nullptr)) { failed=true; Log("Position IOCTL failed"); }
}
static void Contact(const char* name,RadialControllerScreenContact const& contact) {
    if(!contact) { Log("Event has no screen contact"); return; }
    auto p=contact.Position(); auto b=contact.Bounds(); ++contacts;
    char text[300]; sprintf_s(text,"%s: position=(%.2f,%.2f) bounds=(%.2f,%.2f,%.2f,%.2f) expected=(%.2f,%.2f) width=%.2f",name,p.X,p.Y,b.X,b.Y,b.Width,b.Height,expectedX,expectedY,expectedWidth); Log(text);
    // Allow rounding in physical unit conversion and DIP mapping.
    if(std::abs(p.X-expectedX)>5||std::abs(p.Y-expectedY)>5||std::abs(b.Width-expectedWidth)>8) { failed=true; Log("Contact geometry differs from expected primary-display mapping"); }
}
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_TIMER) {
        ++ticks;
        if(ticks==2) Send(false);
        if(ticks==3) { SetForegroundWindow(window); controller.Menu().SelectMenuItem(probeMenuItem); Log(GetForegroundWindow()==window?"Probe is foreground":"Probe foreground activation failed"); }
        if(ticks>=4&&ticks<=10) Send(true);
        if(ticks==12) Send(false);
        if(ticks==16) DestroyWindow(hwnd);
        return 0;
    }
    if(msg==WM_PAINT) { PAINTSTRUCT ps; auto dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r); DrawTextW(dc,L"CirDial 屏上位置验证\n正在读取 Windows ScreenContact 事件，约 8 秒后自动关闭。",-1,&r,DT_CENTER|DT_WORDBREAK); EndPaint(hwnd,&ps); return 0; }
    if(msg==WM_DESTROY) { KillTimer(hwnd,1); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int wmain(int argc,wchar_t** argv) {
    if(argc>1) _wfopen_s(&logFile,argv[1],L"w");
    int result=1;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); init_apartment(apartment_type::single_threaded);
        device=Open(); if(device==INVALID_HANDLE_VALUE) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        VIRTUAL_DIAL_CAPS caps={}; DWORD size=0;
        if(!DeviceIoControl(device,IOCTL_VIRTUAL_DIAL_CAPS,nullptr,0,&caps,sizeof(caps),&size,nullptr)||caps.Version!=2) throw hresult_error(E_FAIL,L"On-screen driver v2 required");
        char text[160]; sprintf_s(text,"Driver caps: native panel %.2f x %.2f mm; dial %.2f mm",caps.ScreenWidthHundredthsMm/100.0,caps.ScreenHeightHundredthsMm/100.0,caps.DialDiameterHundredthsMm/100.0); Log(text);
        WNDCLASSW wc={}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=Proc; wc.lpszClassName=L"CirDial.ScreenContactTest"; wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); RegisterClassW(&wc);
        int w=GetSystemMetrics(SM_CXSCREEN),h=GetSystemMetrics(SM_CYSCREEN);
        window=CreateWindowW(wc.lpszClassName,L"CirDial ScreenContact verification",WS_OVERLAPPEDWINDOW,w/4,h/4,w/2,h/2,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        POINT p={w/2,h/2}; ScreenToClient(window,&p); double scale=GetDpiForWindow(window)/96.0;
        expectedX=p.x/scale; expectedY=p.y/scale; expectedWidth=w*caps.DialDiameterHundredthsMm/static_cast<double>(caps.ScreenWidthHundredthsMm)/scale;
        Log(RadialController::IsSupported()?"RadialController.IsSupported = true":"RadialController.IsSupported = false");
        auto interop=get_activation_factory<RadialController,IRadialControllerInterop>();
        check_hresult(interop->CreateForWindow(window,guid_of<RadialController>(),put_abi(controller)));
        controller.RotationResolutionInDegrees(0.1);
        controller.ScreenContactStarted([](auto const&,auto const& e) { Contact("Started",e.Contact()); });
        controller.ScreenContactContinued([](auto const&,auto const& e) { Contact("Continued",e.Contact()); });
        controller.ScreenContactEnded([](auto const&,auto const&) { ended=true; Log("ScreenContactEnded"); });
        controller.ControlAcquired([](auto const&,auto const& e) { Contact("Acquired",e.Contact()); });
        controller.RotationChanged([](auto const&,auto const& e) { Contact("Rotation",e.Contact()); });
        controller.ControlLost([](auto const&,auto const&) { Log("ControlLost"); });
        probeMenuItem=RadialControllerMenuItem::CreateFromKnownIcon(L"CirDial position verification",RadialControllerMenuKnownIcon::Ruler);
        controller.Menu().Items().Append(probeMenuItem);
        auto configFactory=get_activation_factory<RadialControllerConfiguration,IRadialControllerConfigurationInterop>();
        RadialControllerConfiguration configuration{nullptr};
        check_hresult(configFactory->GetForWindow(window,guid_of<RadialControllerConfiguration>(),put_abi(configuration)));
        configuration.ActiveControllerWhenMenuIsSuppressed(controller); configuration.IsMenuSuppressed(true);
        ShowWindow(window,SW_SHOW); SetForegroundWindow(window); UpdateWindow(window); SetTimer(window,1,500,nullptr);
        MSG msg; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Send(false); controller=nullptr;
        result=!failed&&contacts>0&&ended?0:1;
        Log(result==0?"PASS: Windows delivered correctly positioned screen contact and lift events":"FAIL: Expected screen contact geometry/lift events were not observed");
    } catch(hresult_error const& error) {
        char text[160]; sprintf_s(text,"ERROR HRESULT 0x%08lx",static_cast<unsigned long>(error.code().value)); Log(text);
    }
    if(device!=INVALID_HANDLE_VALUE) { Send(false); CloseHandle(device); }
    if(logFile) fclose(logFile);
    return result;
}

