#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#pragma comment(lib,"hid.lib")
#include "../../Usb/CirDialUsbDeviceConfig.h"
#include <initguid.h>
#include <cstdio>
#include "../../Usb/UsbIpServer.h"
#include "../../Usb/VirtualUsbDevice.h"
#include <cstdlib>
#include <cwchar>
#include <vector>
#include "../../Shared/VirtualDialProtocol.h"
#pragma comment(lib, "setupapi.lib")

static HANDLE OpenDial() {
    HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_VIRTUAL_DIAL, nullptr,
                                               nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA iface = {};
    iface.cbSize = sizeof(iface);
    HANDLE result = INVALID_HANDLE_VALUE;
    if (SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVINTERFACE_VIRTUAL_DIAL, 0, &iface)) {
        DWORD size = 0;
        if (SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &size, nullptr) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            DWORD error = GetLastError();
            SetupDiDestroyDeviceInfoList(devices);
            SetLastError(error ? error : ERROR_INVALID_DATA);
            return INVALID_HANDLE_VALUE;
        }
        std::vector<BYTE> bytes(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, size, nullptr, nullptr))
            result = CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    DWORD error = GetLastError();
    SetupDiDestroyDeviceInfoList(devices);
    SetLastError(error);
    return result;
}
static bool Send(HANDLE device, LONG rotation, BYTE down) {
    VIRTUAL_DIAL_COMMAND command = { VIRTUAL_DIAL_COMMAND_VERSION, rotation, down, {0,0,0} };
    DWORD returned = 0;
    if (!DeviceIoControl(device, IOCTL_VIRTUAL_DIAL_SEND, &command, sizeof(command),
                         nullptr, 0, &returned, nullptr)) {
        std::printf("DeviceIoControl failed: %lu\n", GetLastError());
        return false;
    }
    std::printf("sent rotation=%ld (0.1 degree), button=%u\n", rotation, down);
    return true;
}
// Read-only query: zero-access handles; never reads/writes input reports.
static int ProbeUsbHid() {
 GUID guid={};HidD_GetHidGuid(&guid);
 auto devices=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
 if(devices==INVALID_HANDLE_VALUE){std::printf("{\"passed\":false,\"error\":%lu}\n",GetLastError());return 1;}
 bool passed=false;unsigned matched=0;
 std::printf("{\"devices\":[");
 for(DWORD i=0;;++i) {
  SP_DEVICE_INTERFACE_DATA iface={};iface.cbSize=sizeof(iface);
  if(!SetupDiEnumDeviceInterfaces(devices,nullptr,&guid,i,&iface))break;
  DWORD size=0;SetupDiGetDeviceInterfaceDetailW(devices,&iface,nullptr,0,&size,nullptr);
  if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>65536)continue;
  std::vector<BYTE> buffer(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
  if(!SetupDiGetDeviceInterfaceDetailW(devices,&iface,detail,size,nullptr,nullptr))continue;
  HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(h==INVALID_HANDLE_VALUE)continue;
  HIDD_ATTRIBUTES attributes={};attributes.Size=sizeof(attributes);
  if(!HidD_GetAttributes(h,&attributes)||attributes.VendorID!=vdial::usb::VendorId||attributes.ProductID!=vdial::usb::ProductId){CloseHandle(h);continue;}
  wchar_t serial[128]={};
  bool serialMatches=HidD_GetSerialNumberString(h,serial,sizeof(serial))&&wcscmp(serial,L"VDIALRAD0003")==0;
  PHIDP_PREPARSED_DATA data=nullptr;HIDP_CAPS caps={};bool parsed=false,dial=false,button=false;
  if(HidD_GetPreparsedData(h,&data)) {
   parsed=HidP_GetCaps(data,&caps)==HIDP_STATUS_SUCCESS;
   if(parsed) {
    USHORT count=caps.NumberInputValueCaps;std::vector<HIDP_VALUE_CAPS> values(count);
    if(count&&HidP_GetValueCaps(HidP_Input,values.data(),&count,data)==HIDP_STATUS_SUCCESS)
     for(USHORT n=0;n<count;++n){auto& v=values[n];if(v.UsagePage==1&&!v.IsRange&&v.NotRange.Usage==0x37&&v.ReportID==1&&v.BitSize==16&&!v.IsAbsolute&&v.LogicalMin==-3600&&v.LogicalMax==3600)dial=true;}
    count=caps.NumberInputButtonCaps;std::vector<HIDP_BUTTON_CAPS> buttons(count);
    if(count&&HidP_GetButtonCaps(HidP_Input,buttons.data(),&count,data)==HIDP_STATUS_SUCCESS)
     for(USHORT n=0;n<count;++n){auto& b=buttons[n];if(b.UsagePage==9&&b.ReportID==1&&((!b.IsRange&&b.NotRange.Usage==1)||(b.IsRange&&b.Range.UsageMin<=1&&b.Range.UsageMax>=1)))button=true;}
   }
   HidD_FreePreparsedData(data);
  }
  bool ok=serialMatches&&parsed&&caps.UsagePage==1&&caps.Usage==14&&caps.InputReportByteLength==10&&dial&&button;
  if(matched++)std::printf(",");
  std::printf("{\"serialMatches\":%s,\"parsed\":%s,\"usagePage\":%u,\"usage\":%u,\"inputReportBytes\":%u,\"relativeDial16\":%s,\"button1\":%s,\"passed\":%s}",serialMatches?"true":"false",parsed?"true":"false",caps.UsagePage,caps.Usage,caps.InputReportByteLength,dial?"true":"false",button?"true":"false",ok?"true":"false");
  passed=passed||ok;CloseHandle(h);
 }
 SetupDiDestroyDeviceInfoList(devices);
 std::printf("],\"passed\":%s,\"inputSent\":false}\n",passed?"true":"false");return passed?0:1;
}
int wmain(int argc, wchar_t** argv) {
    if(argc==2&&wcscmp(argv[1],L"--usbip-hid-probe")==0)return ProbeUsbHid();
    if(argc==2&&wcscmp(argv[1],L"--usbip-self-test")==0){bool ok=vdial::usb::ProtocolSelfTest()&&vdial::usb::ServerSelfTest();std::puts(ok?"PASS: USB/IP Phase 4 protocol/lifecycle tests":"FAIL: USB/IP Phase 4 tests");return ok?0:1;}
    if(argc>=3&&wcscmp(argv[1],L"--backend")==0&&wcscmp(argv[2],L"usbip")==0){if(argc==4&&wcscmp(argv[3],L"interactive")==0)return vdial::usb::RunUsbIpInteractive();if(argc>=4&&wcscmp(argv[3],L"serve")==0)return vdial::usb::RunUsbIpPoc(argc==5?unsigned(_wtoi(argv[4])):120);std::puts("Phase 4: DialTest --backend usbip serve [1..3600 seconds]. Radial descriptor only; no active input.");return 2;}
    if(argc>=3&&wcscmp(argv[1],L"--backend")==0&&wcscmp(argv[2],L"vhf")==0){argc-=2;argv+=2;}
    if (argc < 2) {
        std::puts("DialTest.exe probe | cw [steps] | ccw [steps] | down | up | hold [ms] | demo\n"
                  "One step is 15 degrees; hold defaults to 1200 ms. Run elevated.");
        return 2;
    }
    HANDLE device = OpenDial();
    if (device == INVALID_HANDLE_VALUE) {
        std::printf("Cannot open Virtual Dial interface: Win32 error %lu\n", GetLastError());
        return 1;
    }
    bool ok = true;
    if (_wcsicmp(argv[1], L"probe") == 0) {
        std::puts("Virtual Dial interface opened successfully. No input sent.");
    } else if (_wcsicmp(argv[1], L"cw") == 0 || _wcsicmp(argv[1], L"ccw") == 0) {
        int count = argc > 2 ? _wtoi(argv[2]) : 1;
        if (count < 1 || count > 100) { std::puts("steps must be 1..100"); ok = false; }
        else for (int i = 0; i < count && ok; ++i) {
            ok = Send(device, _wcsicmp(argv[1], L"cw") == 0 ? 150 : -150, 0);
            Sleep(80);
        }
    } else if (_wcsicmp(argv[1], L"down") == 0) ok = Send(device, 0, 1);
    else if (_wcsicmp(argv[1], L"up") == 0) ok = Send(device, 0, 0);
    else if (_wcsicmp(argv[1], L"hold") == 0) {
        int ms = argc > 2 ? _wtoi(argv[2]) : 1200;
        if (ms < 100 || ms > 10000) { std::puts("hold ms must be 100..10000"); ok = false; }
        else { ok = Send(device, 0, 1); Sleep(ms); ok = Send(device, 0, 0) && ok; }
    } else if (_wcsicmp(argv[1], L"demo") == 0) {
        ok = Send(device, 0, 0);
        for (int i=0; i<4 && ok; ++i) { ok=Send(device,150,0); Sleep(100); }
        for (int i=0; i<4 && ok; ++i) { ok=Send(device,-150,0); Sleep(100); }
        if (ok) { ok=Send(device,0,1); Sleep(1200); ok=Send(device,0,0) && ok; }
    } else { std::puts("Unknown command"); ok = false; }
    CloseHandle(device);
    return ok ? 0 : 2;
}
