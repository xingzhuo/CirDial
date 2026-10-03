#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DialBackend.h"
#include "../../Usb/UsbIpServer.h"
#include <setupapi.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "TransportPins.h"
#pragma comment(lib,"bcrypt.lib")
namespace dial {
static void Trace(const wchar_t* message,DWORD value=0){
 wchar_t folder[32768]={};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",folder,32768))return;
 std::wstring path=std::wstring(folder)+L"\\VDial\\usbip.log";FILE* file=nullptr;
 if(_wfopen_s(&file,path.c_str(),L"a, ccs=UTF-8")!=0)return;
 fwprintf(file,L"tick=%llu %ls value=%lu\n",GetTickCount64(),message,value);fclose(file);
}

class VhfDialBackend final:public IDialBackend {
 HANDLE device=INVALID_HANDLE_VALUE;DWORD error=ERROR_NOT_FOUND;bool position=false;VIRTUAL_DIAL_CAPS caps={};
public:
 ~VhfDialBackend(){Close();}
 bool Connected()const override{return device!=INVALID_HANDLE_VALUE;}
 DWORD Error()const override{return error;}
 void Close()override{if(Connected())CloseHandle(device);device=INVALID_HANDLE_VALUE;position=false;}
 void Connect()override {
  if(Connected())return;
  auto list=SetupDiGetClassDevsW(&GUID_DEVINTERFACE_VIRTUAL_DIAL,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  if(list==INVALID_HANDLE_VALUE){error=GetLastError();return;}
  SP_DEVICE_INTERFACE_DATA iface={};iface.cbSize=sizeof(iface);
  for(DWORD i=0;SetupDiEnumDeviceInterfaces(list,nullptr,&GUID_DEVINTERFACE_VIRTUAL_DIAL,i,&iface);++i){
   DWORD size=0;SetupDiGetDeviceInterfaceDetailW(list,&iface,nullptr,0,&size,nullptr);
   if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))continue;
   std::vector<BYTE> buffer(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
   if(!SetupDiGetDeviceInterfaceDetailW(list,&iface,detail,size,nullptr,nullptr))continue;
   device=CreateFileW(detail->DevicePath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
   if(Connected())break;error=GetLastError();
  }
  SetupDiDestroyDeviceInfoList(list);
  if(Connected()){DWORD n=0;position=DeviceIoControl(device,IOCTL_VIRTUAL_DIAL_CAPS,nullptr,0,&caps,sizeof(caps),&n,nullptr)&&n==sizeof(caps)&&caps.Version==2&&caps.ScreenWidthHundredthsMm>=5000&&caps.ScreenHeightHundredthsMm>=5000&&caps.DialDiameterHundredthsMm>=2000;error=0;}
 }
 bool Caps(VIRTUAL_DIAL_CAPS& c)override{if(position)c=caps;return position;}
 bool Send(const VIRTUAL_DIAL_COMMAND_V2& r)override{
  VIRTUAL_DIAL_COMMAND old={1,r.RotationTenthsOfDegree,r.ButtonDown,{0,0,0}};DWORD n=0;
  if(!DeviceIoControl(device,IOCTL_VIRTUAL_DIAL_SEND,position?static_cast<void*>(const_cast<VIRTUAL_DIAL_COMMAND_V2*>(&r)):static_cast<void*>(&old),position?sizeof(r):sizeof(old),nullptr,0,&n,nullptr)){error=GetLastError();return false;}return true;
 }
};
static bool HashMatches(const std::wstring& path,const wchar_t* expected){
 HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;bool ok=false;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0){
  BYTE buf[8192];DWORD n=0;ok=true;
  while(true){if(!ReadFile(file,buf,sizeof(buf),&n,nullptr)){ok=false;break;}if(!n)break;if(BCryptHashData(hash,buf,n,0)<0){ok=false;break;}}
  BYTE digest[32];wchar_t hex[65]={};if(ok&&BCryptFinishHash(hash,digest,32,0)>=0){for(int i=0;i<32;++i)swprintf_s(hex+i*2,3,L"%02X",digest[i]);ok=wcscmp(hex,expected)==0;}else ok=false;
 }
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(file);return ok;
}
// Execute only the pinned, installed native transport CLI; never use PATH lookup.
static bool Cli(const std::wstring& exe,const std::wstring& args,std::string& output){
 SECURITY_ATTRIBUTES sa={sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
 if(!CreatePipe(&read,&write,&sa,0))return false;SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW si={};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=si.hStdError=write;
 si.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
 PROCESS_INFORMATION pi={};std::wstring line=L"\""+exe+L"\" "+args;
 bool ok=CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;
 CloseHandle(write);if(si.hStdInput!=INVALID_HANDLE_VALUE)CloseHandle(si.hStdInput);
 if(ok){DWORD wait=WaitForSingleObject(pi.hProcess,15000);if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,2000);}
  DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);ok=wait==WAIT_OBJECT_0&&code==0;char b[1024];DWORD n=0;
  DWORD available=0;if(PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr)&&available&&ReadFile(read,b,sizeof(b),&n,nullptr))output.assign(b,n);
  CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 }CloseHandle(read);return ok;
}
class UsbIpDialBackend final:public IDialBackend {
 struct Item{VIRTUAL_DIAL_COMMAND_V2 report;ULONGLONG at;};
 VIRTUAL_DIAL_CAPS screenCaps={2,27400,18300,5900};
 vdial::usb::UsbIpServer server;std::thread worker;std::mutex lock;std::condition_variable wake;
 std::deque<Item> queue;bool stop=false;std::atomic<bool> ready{false};std::atomic<DWORD> error{ERROR_NOT_CONNECTED};
 void Run(){
  Trace(L"USB/IP backend starting");
  // Reuse the machine-specific mapping proven in ConfigureMapping, read-only.
  UINT32 count=0;unsigned matches=0;DWORD width=0,height=0;
  if(GetPointerDevices(&count,nullptr)&&count&&count<256){
   std::vector<POINTER_DEVICE_INFO> devices(count);
   if(GetPointerDevices(&count,devices.data()))for(const auto& d:devices){
    if(d.pointerDeviceType!=POINTER_DEVICE_TYPE_TOUCH||d.monitor!=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY)||wcscmp(d.productString,L"HIDSPI Device"))continue;
    RECT native={},display={};if(!GetPointerDeviceRects(d.device,&native,&display))continue;
    if(native.left!=0||native.top!=0||native.right<5001||native.bottom<5001||native.right>200001||native.bottom>200001)continue;
    width=DWORD(native.right-1);height=DWORD(native.bottom-1);++matches;
   }
  }
  if(matches==1){screenCaps.ScreenWidthHundredthsMm=width;screenCaps.ScreenHeightHundredthsMm=height;}
  Trace(L"Touch mapping candidates",matches);Trace(L"Descriptor physical X",screenCaps.ScreenWidthHundredthsMm);Trace(L"Descriptor physical Y",screenCaps.ScreenHeightHundredthsMm);
  wchar_t path[32768]={};if(!GetEnvironmentVariableW(L"ProgramFiles",path,32768)){error=ERROR_PATH_NOT_FOUND;return;}
  std::wstring dir=std::wstring(path)+L"\\USBip\\",exe=dir+L"usbip.exe";
  for(const auto& pin:TransportPins)if(!HashMatches(dir+pin.name,pin.hash)){error=ERROR_INVALID_IMAGE_HASH;Trace(L"Transport hash rejected",error);return;}
  unsigned failures=0;
  while(true){
   {std::lock_guard<std::mutex> g(lock);if(stop)break;queue.clear();}
   if(!server.Start(3240,screenCaps.ScreenWidthHundredthsMm,screenCaps.ScreenHeightHundredthsMm)){error=server.Error();break;}
   std::string output;unsigned port=0;
   if(Cli(exe,L"attach -r 127.0.0.1 -b 1-1 --once --terse",output)){
    char* end=nullptr;auto parsed=strtoul(output.c_str(),&end,10);
    if(parsed>0&&parsed<=65535&&end&&(*end==0||*end=='\r'||*end=='\n'))port=unsigned(parsed);
   }
   if(!port){error=ERROR_CONNECTION_REFUSED;Trace(L"Attach failed",error);server.Stop();break;}
   Trace(L"Attached port",port);
   auto deadline=GetTickCount64()+15000;
   while(!server.Ready()&&GetTickCount64()<deadline){std::unique_lock<std::mutex> g(lock);if(wake.wait_for(g,std::chrono::milliseconds(100),[&]{return stop;}))break;}
   ready=server.Ready();error=ready?0:ERROR_NOT_READY;Trace(L"Device readiness",error);
   while(ready){
    Item item={};bool have=false;
    {std::unique_lock<std::mutex> g(lock);wake.wait_for(g,std::chrono::milliseconds(250),[&]{return stop||!queue.empty();});if(stop)break;if(!queue.empty()){item=queue.front();queue.pop_front();have=true;}}
    if(!server.Ready()){error=ERROR_DEVICE_NOT_CONNECTED;break;}
    if(have){auto& r=item.report;
     if(GetTickCount64()-item.at>500||!server.SendReport(r.RotationTenthsOfDegree,r.ButtonDown!=0,r.PositionValid!=0,r.X,r.Y)){error=ERROR_WRITE_FAULT;break;}
    }
   }
   ready=false;
   // Neutralize before detaching the owned port. No other USB/IP device is touched.
   if(server.Ready())server.SendReport(0,false,false,65535,65535,100);
   std::string ignored;bool detached=Cli(exe,L"detach -p "+std::to_wstring(port),ignored);Trace(L"Detached owned port",detached?0:ERROR_DEVICE_NOT_CONNECTED);server.Stop();
   std::unique_lock<std::mutex> g(lock);queue.clear();if(stop||++failures>=3)break;
   if(wake.wait_for(g,std::chrono::seconds(2),[&]{return stop;}))break;
  }
  ready=false;
 }
public:
 ~UsbIpDialBackend(){Close();}
 void Connect()override{if(!worker.joinable()){stop=false;try{worker=std::thread([this]{Run();});}catch(...){error=ERROR_NOT_ENOUGH_MEMORY;}}}
 void Close()override{{std::lock_guard<std::mutex> g(lock);stop=true;queue.clear();}wake.notify_all();if(worker.joinable())worker.join();ready=false;}
 bool Connected()const override{return ready;}
 DWORD Error()const override{return error;}
 bool Caps(VIRTUAL_DIAL_CAPS& c)override{c=screenCaps;return true;}
 bool Send(const VIRTUAL_DIAL_COMMAND_V2& r)override{
  if(!ready||r.RotationTenthsOfDegree < -3600||r.RotationTenthsOfDegree > 3600||r.ButtonDown>1||r.PositionValid>1||(r.PositionValid&&(r.X>32767||r.Y>32767)))return false;
  {std::lock_guard<std::mutex> g(lock);if(stop||queue.size()>=64){error=ERROR_BUFFER_OVERFLOW;return false;}queue.push_back({r,GetTickCount64()});}wake.notify_one();return true;
 }
};
std::unique_ptr<IDialBackend> CreateDialBackend(bool usbip){if(usbip)return std::make_unique<UsbIpDialBackend>();return std::make_unique<VhfDialBackend>();}
}
