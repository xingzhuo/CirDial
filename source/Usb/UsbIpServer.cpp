#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "UsbIpServer.h"
#include "VirtualUsbDevice.h"
#include "CirDialUsbDeviceConfig.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <string>
#include <iostream>
#include <thread>
#pragma comment(lib,"ws2_32.lib")
namespace vdial::usb {
namespace {
bool Receive(SOCKET socket,uint8_t* data,size_t count,bool idle=false,HANDLE stop=nullptr) {
 size_t done=0;ULONGLONG deadline=GetTickCount64()+5000;
 while(done<count) {
  DWORD timeout=INFINITE;
  if(!(idle&&done==0)) {
   auto now=GetTickCount64();if(now>=deadline)return false;
   timeout=DWORD(deadline-now);
  }
  WSAOVERLAPPED operation={};operation.hEvent=WSACreateEvent();if(operation.hEvent==WSA_INVALID_EVENT)return false;
  WSABUF buffer={ULONG(count-done),reinterpret_cast<char*>(data+done)};DWORD n=0,flags=0;
  int result=WSARecv(socket,&buffer,1,&n,&flags,&operation,nullptr);
  if(result==SOCKET_ERROR&&WSAGetLastError()==WSA_IO_PENDING) {
   HANDLE events[]={operation.hEvent,stop};
   DWORD wait=WaitForMultipleObjects(stop?2:1,events,FALSE,timeout);
   if(wait!=WAIT_OBJECT_0) {
    // The OVERLAPPED and buffer must remain alive until cancellation completes.
    CancelIoEx(reinterpret_cast<HANDLE>(socket),&operation);
    WSAGetOverlappedResult(socket,&operation,&n,TRUE,&flags);WSACloseEvent(operation.hEvent);return false;
   }
   result=WSAGetOverlappedResult(socket,&operation,&n,FALSE,&flags)?0:SOCKET_ERROR;
  }
  WSACloseEvent(operation.hEvent);if(result==SOCKET_ERROR||n==0)return false;
  if(idle&&done==0)deadline=GetTickCount64()+5000;
  done+=size_t(n);
 }
 return true;
}
bool Send(SOCKET socket,const Bytes& data) {
 size_t done=0;while(done<data.size()) {int n=send(socket,reinterpret_cast<const char*>(data.data()+done),int(data.size()-done),0);if(n<=0)return false;done+=size_t(n);}return true;
}
Bytes Reply(uint16_t operation,uint32_t status=0){Bytes b;Put16(b,0x0111);Put16(b,operation);Put32(b,status);return b;}
Bytes UrbReply(uint32_t op,uint32_t seq,int32_t status,const Bytes& payload={}) {
 Bytes b;Put32(b,op);Put32(b,seq);Put32(b,0);Put32(b,0);Put32(b,0);Put32(b,uint32_t(status));
 if(op==3)Put32(b,uint32_t(payload.size()));b.resize(48,0);b.insert(b.end(),payload.begin(),payload.end());return b;
}
HANDLE quitEvent=nullptr;
BOOL WINAPI Quit(DWORD code){if(code==CTRL_C_EVENT||code==CTRL_BREAK_EVENT){if(quitEvent)SetEvent(quitEvent);return TRUE;}return FALSE;}
}
struct UsbIpServer::Impl {
 struct Worker {std::thread thread;std::atomic<bool> done{true};SOCKET socket=INVALID_SOCKET;};
 std::array<Worker,4> workers;
 std::mutex lock;std::thread acceptThread;
 std::condition_variable inputAvailable;std::mutex inputLock;SOCKET inputSocket=INVALID_SOCKET;
 std::deque<uint32_t> pending;bool configured=false,button=false;Bytes state={1,0,0,0,255,255,255,255,12,23};
 bool Input(int rotation,bool down,bool contact=false,uint16_t x=65535,uint16_t y=65535,unsigned waitMs=0) {
  if(rotation < -3600 || rotation > 3600 || (contact&&(x>32767||y>32767)))return false;
  std::unique_lock<std::mutex> guard(inputLock);
  if(waitMs)inputAvailable.wait_for(guard,std::chrono::milliseconds(waitMs),[&]{return !pending.empty()||inputSocket==INVALID_SOCKET||!configured;});
  if(inputSocket==INVALID_SOCKET||!configured||pending.empty()) {
   // Never replay stale input later. If a release cannot be delivered, disconnect.
   if(button&&inputSocket!=INVALID_SOCKET){shutdown(inputSocket,SD_BOTH);CancelIoEx(reinterpret_cast<HANDLE>(inputSocket),nullptr);}
   std::puts("INPUT rejected: no configured interrupt read available");return false;
  }
  uint16_t packed=uint16_t(rotation);if(!contact)x=y=65535;Bytes report={1,uint8_t((down?1:0)|(contact?2:0)),uint8_t(packed),uint8_t(packed>>8),uint8_t(x),uint8_t(x>>8),uint8_t(y),uint8_t(y>>8),12,23};
  auto seq=pending.front();pending.pop_front();
  if(!Send(inputSocket,UrbReply(3,seq,0,report))){shutdown(inputSocket,SD_BOTH);CancelIoEx(reinterpret_cast<HANDLE>(inputSocket),nullptr);return false;}
  button=down;state=report;state[2]=state[3]=0;
  std::printf("INPUT delivered seq=%u rotation=%d button=%d bytes=10 tick=%llu\n",seq,rotation,int(down),GetTickCount64());return true;
 }
 SOCKET listener=INVALID_SOCKET;HANDLE stop=nullptr;WSAEVENT incoming=WSA_INVALID_EVENT;
 uint32_t screenWidth=27400,screenHeight=18300;
 std::atomic<bool> imported{false};std::atomic<unsigned> error{0};uint16_t port=0;bool wsa=false;
 void Session(SOCKET socket) {
  std::array<uint8_t,8> op={};if(!Receive(socket,op.data(),op.size(),false,stop))return;
  if(Be16(op.data())!=0x0111||Be32(op.data()+4)!=0)return;
  VirtualUsbDevice device(screenWidth,screenHeight);
  if(Be16(op.data()+2)==0x8005) {
   auto b=Reply(5);Put32(b,1);auto record=device.ExportRecord();b.insert(b.end(),record.begin(),record.end());b.insert(b.end(),{3,0,0,0});Send(socket,b);return;
  }
  if(Be16(op.data()+2)!=0x8003)return;
  std::array<uint8_t,32> bus={};if(!Receive(socket,bus.data(),bus.size(),false,stop))return;
  std::array<uint8_t,32> expected={};std::memcpy(expected.data(),BusId,sizeof(BusId));
  if(bus!=expected){Send(socket,Reply(3,1));return;}
  bool available=false;
  if(!imported.compare_exchange_strong(available,true)){Send(socket,Reply(3,1));return;}
  struct ResetImport {std::atomic<bool>& value;~ResetImport(){value=false;}} reset{imported};
  auto b=Reply(3);auto record=device.ExportRecord();b.insert(b.end(),record.begin(),record.end());if(!Send(socket,b))return;
  std::puts("USB/IP: imported; Windows enumeration NOT yet verified");
  {std::lock_guard<std::mutex> guard(inputLock);inputSocket=socket;pending.clear();configured=false;button=false;}
  struct InputCleanup {Impl* self;~InputCleanup(){std::lock_guard<std::mutex> guard(self->inputLock);self->inputSocket=INVALID_SOCKET;self->pending.clear();self->configured=false;self->button=false;self->state={1,0,0,0,255,255,255,255,12,23};self->inputAvailable.notify_all();}} cleanup{this};
  while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0) {
   std::array<uint8_t,48> header={};if(!Receive(socket,header.data(),header.size(),true,stop))break;
   std::lock_guard<std::mutex> inputGuard(inputLock);
   const auto command=Be32(header.data()),seq=Be32(header.data()+4);
   if(command==2) {
    if(Be32(header.data()+8)!=DeviceId||Be32(header.data()+12)>1||Be32(header.data()+16)>1)break;
    // Successful unlink cancels the pending URB without a RET_SUBMIT.
    auto found=std::find(pending.begin(),pending.end(),Be32(header.data()+20));
    bool cancelled=found!=pending.end();if(cancelled)pending.erase(found);
    if(!Send(socket,UrbReply(4,seq,cancelled?-104:0)))break;continue;
   }
   if(!ValidateSubmit(header)){std::puts("USB/IP: rejected malformed/unsupported URB");break;}
   const auto length=Be32(header.data()+24);
   if(Be32(header.data()+16)==1) {
    if(device.Configuration()!=1){if(!Send(socket,UrbReply(3,seq,-32)))break;continue;}
    // Preserve submission order, not numerical sequence order (sequence IDs wrap).
    if(pending.size()>=64||std::find(pending.begin(),pending.end(),seq)!=pending.end())break;
    pending.push_back(seq);inputAvailable.notify_all();
    continue;
   }
   if(Be32(header.data()+12)==0&&length){Bytes discard(length);if(!Receive(socket,discard.data(),discard.size(),false,stop))break;}
   std::array<uint8_t,8> setup={};std::copy(header.begin()+40,header.end(),setup.begin());
   auto result=device.Control(setup);
   if(result.status==0&&setup[0]==0&&setup[1]==9){
    configured=device.Configuration()==1;button=false;state={1,0,0,0,255,255,255,255,12,23};inputAvailable.notify_all();
    for(auto pendingSeq:pending)if(!Send(socket,UrbReply(3,pendingSeq,-108))){shutdown(socket,SD_BOTH);break;}
    pending.clear();
   }
   if(result.status==0&&setup[0]==0xa1&&setup[1]==1&&result.data.size()>1)std::copy_n(state.begin(),result.data.size(),result.data.begin());
   std::printf("USB control request=%02x type=%02x status=%d bytes=%zu\n",setup[1],setup[0],result.status,result.data.size());
   if(!Send(socket,UrbReply(3,seq,result.status,result.data)))break;
  }
  std::puts("USB/IP: disconnected");
 }
 void Accept() {
  HANDLE events[]={stop,incoming};
  while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1) {
   WSANETWORKEVENTS e={};if(WSAEnumNetworkEvents(listener,incoming,&e)){error=WSAGetLastError();break;}
   if(!(e.lNetworkEvents&FD_ACCEPT))continue;
   SOCKET client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)continue;
   WSAEventSelect(client,nullptr,0);u_long blocking=0;ioctlsocket(client,FIONBIO,&blocking);
   DWORD timeout=5000;setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
   Worker* slot=nullptr;for(auto& worker:workers)if(worker.done){slot=&worker;break;}
   if(!slot){closesocket(client);continue;}
   if(slot->thread.joinable())slot->thread.join();
   {std::lock_guard<std::mutex> guard(lock);slot->socket=client;slot->done=false;}
   try {slot->thread=std::thread([this,slot,client]{
    try{Session(client);}catch(...){error=ERROR_NOT_ENOUGH_MEMORY;}
    {std::lock_guard<std::mutex> guard(lock);shutdown(client,SD_BOTH);closesocket(client);slot->socket=INVALID_SOCKET;}
    slot->done=true;
   });}catch(...){std::lock_guard<std::mutex> guard(lock);closesocket(client);slot->socket=INVALID_SOCKET;slot->done=true;error=ERROR_NOT_ENOUGH_MEMORY;}
  }
 }
};
UsbIpServer::UsbIpServer():impl_(std::make_unique<Impl>()){}
UsbIpServer::~UsbIpServer(){Stop();}
bool UsbIpServer::Start(uint16_t port,uint32_t width,uint32_t height) {
 Stop();auto& p=*impl_;p.error=0;
 if(width<5000||height<5000||width>200000||height>200000){p.error=ERROR_INVALID_PARAMETER;return false;}
 p.screenWidth=width;p.screenHeight=height;WSADATA w={};int startup=WSAStartup(MAKEWORD(2,2),&w);
 if(startup){p.error=unsigned(startup);return false;}p.wsa=true;
 p.stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);p.incoming=WSACreateEvent();
 p.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
 if(!p.stop||p.incoming==WSA_INVALID_EVENT||p.listener==INVALID_SOCKET){p.error=WSAGetLastError();Stop();return false;}
 BOOL exclusive=TRUE;setsockopt(p.listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));
 sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(bind(p.listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(p.listener,4)||WSAEventSelect(p.listener,p.incoming,FD_ACCEPT)) {p.error=WSAGetLastError();Stop();return false;}
 int size=sizeof(address);if(getsockname(p.listener,reinterpret_cast<sockaddr*>(&address),&size)){p.error=WSAGetLastError();Stop();return false;}p.port=ntohs(address.sin_port);
 try{p.acceptThread=std::thread([&p]{p.Accept();});}catch(...){p.error=ERROR_NOT_ENOUGH_MEMORY;Stop();return false;}
 return true;
}
void UsbIpServer::Stop() {
 auto& p=*impl_;if(p.stop)SetEvent(p.stop);if(p.acceptThread.joinable())p.acceptThread.join();
 {std::lock_guard<std::mutex> guard(p.lock);for(auto& worker:p.workers)if(worker.socket!=INVALID_SOCKET)shutdown(worker.socket,SD_BOTH);}
 for(auto& worker:p.workers)if(worker.thread.joinable())worker.thread.join();
 if(p.listener!=INVALID_SOCKET){closesocket(p.listener);p.listener=INVALID_SOCKET;}
 if(p.incoming!=WSA_INVALID_EVENT){WSACloseEvent(p.incoming);p.incoming=WSA_INVALID_EVENT;}
 if(p.stop){CloseHandle(p.stop);p.stop=nullptr;}if(p.wsa){WSACleanup();p.wsa=false;}p.port=0;p.imported=false;
}
uint16_t UsbIpServer::Port()const{return impl_->port;}
bool UsbIpServer::Imported()const{return impl_->imported;}
bool UsbIpServer::Ready()const{std::lock_guard<std::mutex> guard(impl_->inputLock);return impl_->inputSocket!=INVALID_SOCKET&&impl_->configured;}
bool UsbIpServer::SendReport(int rotation,bool down,bool contact,uint16_t x,uint16_t y,unsigned waitMs){return impl_->Input(rotation,down,contact,x,y,waitMs);}
bool UsbIpServer::SendInput(int rotation,bool down){return impl_->Input(rotation,down);}
unsigned UsbIpServer::Error()const{return impl_->error;}
int RunUsbIpPoc(unsigned seconds) {
 if(seconds<1||seconds>3600)return 2;
 setvbuf(stdout,nullptr,_IONBF,0);
 UsbIpServer server;if(!server.Start()){std::printf("USB/IP initialization failed: %u\n",server.Error());return 1;}
 quitEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!quitEvent)return 1;SetConsoleCtrlHandler(Quit,TRUE);
 std::printf("CirDial Phase 4: localhost:%u, bus 1-1. Private test VID/PID 1209:0001.\nPassive server. Use interactive mode for explicitly requested input.\nNo driver installed or attached by this command. Timeout %u seconds; Ctrl+C to stop.\n",server.Port(),seconds);std::fflush(stdout);
 WaitForSingleObject(quitEvent,seconds*1000);server.Stop();SetConsoleCtrlHandler(Quit,FALSE);CloseHandle(quitEvent);quitEvent=nullptr;
 return server.Error()?1:0;
}
int RunUsbIpInteractive() {
 setvbuf(stdout,nullptr,_IONBF,0);UsbIpServer server;
 if(!server.Start())return 1;
 std::puts("READY Phase4 interactive: cw ccw click hold up quit. No automatic input.");
 std::string command;
 while(std::getline(std::cin,command)) {
  bool ok=false;
  if(command=="quit")break;
  if(command=="cw"||command=="ccw")ok=server.SendInput(command=="cw"?150:-150,false);
  else if(command=="up")ok=server.SendInput(0,false);
  else if(command=="click"||command=="hold") {
   ok=server.SendInput(0,true);
   if(ok){Sleep(command=="hold"?1200:80);ok=server.SendInput(0,false);}
  }else{std::puts("COMMAND invalid");continue;}
  std::printf("COMMAND %s %s\n",command.c_str(),ok?"delivered":"FAILED");
 }
 server.Stop();std::puts("STOPPED");return server.Error()?1:0;
}
bool ServerSelfTest() {
 WSADATA w={};if(WSAStartup(MAKEWORD(2,2),&w))return false;struct Cleanup{~Cleanup(){WSACleanup();}}cleanup;
 UsbIpServer server;if(!server.Start(0))return false;
 auto connect=[&](){SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a={};a.sin_family=AF_INET;a.sin_port=htons(server.Port());a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))){closesocket(s);return INVALID_SOCKET;}return s;};
 SOCKET client=connect();if(client==INVALID_SOCKET)return false;
 auto op=Reply(0x8005);bool ok=true;
 for(auto byte:op)ok=Send(client,Bytes{byte})&&ok;
 Bytes reply(328);ok=Receive(client,reply.data(),reply.size())&&ok;closesocket(client);
 ok=ok&&Be16(reply.data())==0x0111&&Be16(reply.data()+2)==5&&Be32(reply.data()+8)==1;
 std::printf("list/framing=%d\n",int(ok));UsbIpServer collision;ok=!collision.Start(server.Port())&&ok;
 client=connect();auto request=Reply(0x8003);request.resize(40,0);std::memcpy(request.data()+8,BusId,sizeof(BusId));
 ok=Send(client,request)&&ok;reply.resize(320);ok=Receive(client,reply.data(),reply.size())&&ok;
 ok=ok&&Be32(reply.data()+4)==0&&server.Imported();
 std::printf("import/collision=%d\n",int(ok));
 // Exercise the same position-aware entry point used by the GUI backend.
 Bytes config;for(auto v:{1u,100u,DeviceId,0u,0u,0u,0u,0u,0u,0u})Put32(config,v);
 config.insert(config.end(),{0,9,1,0,0,0,0,0});ok=Send(client,config)&&ok;reply.resize(48);ok=Receive(client,reply.data(),48)&&ok;
 Bytes input;for(auto v:{1u,101u,DeviceId,1u,1u,0u,10u,0u,0u,0u})Put32(input,v);input.resize(48,0);ok=Send(client,input)&&ok;
 ok=server.SendReport(150,true,true,1234,2345,1000)&&ok;
 reply.resize(58);ok=Receive(client,reply.data(),58)&&ok;
 ok=ok&&reply[48]==1&&reply[49]==3&&reply[50]==150&&reply[51]==0&&reply[52]==0xd2&&reply[53]==4&&reply[54]==0x29&&reply[55]==9;
 ok=!server.SendReport(0,false,true,40000,0)&&ok;
 std::printf("GUI report coordinates/validation=%d\n",int(ok));
 // A partial hostile header must not prevent shutdown or leak a thread.
 Send(client,Bytes{0});auto before=GetTickCount64();server.Stop();std::printf("shutdown ms=%llu\n",GetTickCount64()-before);ok=GetTickCount64()-before<2000&&ok;closesocket(client);
 ok=server.Start(0)&&ok;server.Stop();server.Stop();return ok;
}
}
