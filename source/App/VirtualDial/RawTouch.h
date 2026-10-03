#pragma once
#include <hidsdi.h>
#include <hidpi.h>
#include <set>
#pragma comment(lib,"hid.lib")

namespace dial {
// Passive HID observation: never captures, consumes or injects touch input.
class RawTouch {
 struct Finger { LONG id; double x,y; };
 struct Device {
  std::vector<BYTE> preparsed;
  std::vector<HIDP_VALUE_CAPS> values;
  std::vector<HIDP_BUTTON_CAPS> buttons;
  unsigned expected=0,received=0; ULONGLONG frameAt=0,start=0;
  std::map<LONG,Finger> frame,origin;
  bool candidate=false,invalid=false,three=false;
  POINT center={};
 };
 std::map<HANDLE,Device> devices;
 bool enabled=false;
 unsigned fingers=3;double maxDistance=800;
 static bool Has(const HIDP_VALUE_CAPS& c,USAGE page,USAGE usage) {
  return c.UsagePage==page&&(c.IsRange?(usage>=c.Range.UsageMin&&usage<=c.Range.UsageMax):c.NotRange.Usage==usage);
 }
 static bool Value(Device& d,USAGE page,USAGE usage,USHORT link,char* report,ULONG size,ULONG& v,const HIDP_VALUE_CAPS** cap=nullptr) {
  for(auto& c:d.values)if(c.LinkCollection==link&&Has(c,page,usage)&&
   HidP_GetUsageValue(HidP_Input,page,link,usage,&v,reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data()),report,size)==HIDP_STATUS_SUCCESS){if(cap)*cap=&c;return true;}
  return false;
 }
 static bool Tip(Device& d,USHORT link,char* report,ULONG size,bool& tip) {
  ULONG v=0;if(Value(d,0x0d,0x42,link,report,size,v)){tip=v!=0;return true;}
  for(auto& c:d.buttons)if(c.UsagePage==0x0d&&c.LinkCollection==link) {
   USAGE usages[64]={};ULONG count=64;
   if(HidP_GetUsages(HidP_Input,0x0d,link,usages,&count,reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data()),report,size)==HIDP_STATUS_SUCCESS){tip=std::find(usages,usages+count,0x42)!=usages+count;return true;}
  }return false;
 }
 static double Normal(ULONG v,const HIDP_VALUE_CAPS* c) {return std::clamp((double(v)-c->LogicalMin)/double(c->LogicalMax-c->LogicalMin),0.0,1.0);}
 bool Complete(Device& d,ULONGLONG now,POINT& result) {
  if(d.frame.empty()) {
   bool fire=d.candidate&&d.three&&!d.invalid&&now-d.start<=400;
   result=d.center;d.candidate=false;d.invalid=false;d.three=false;d.origin.clear();return fire;
  }
  if(!d.candidate){d.candidate=true;d.start=now;d.invalid=false;d.three=false;d.origin.clear();}
  if(now-d.start>400||d.frame.size()>fingers)d.invalid=true;
  for(auto& [id,f]:d.frame) {
   auto [it,added]=d.origin.emplace(id,f);
   if(added&&(now-d.start>180||d.origin.size()>fingers))d.invalid=true;
   // Normalized tolerance is ~3 mm on a 30 cm panel, independent of scaling.
   if(std::hypot(f.x-it->second.x,f.y-it->second.y)>.012)d.invalid=true;
  }
  DEVMODEW orientation={};orientation.dmSize=sizeof(orientation);EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&orientation);
  const bool rotated=orientation.dmDisplayOrientation==DMDO_90||orientation.dmDisplayOrientation==DMDO_270;
  double width=GetSystemMetrics(rotated?SM_CYSCREEN:SM_CXSCREEN),height=GetSystemMetrics(rotated?SM_CXSCREEN:SM_CYSCREEN);
  double scale=GetDpiForSystem()/96.0;
  for(auto& [id,a]:d.frame)for(auto& [other,b]:d.frame)
   if(std::hypot((a.x-b.x)*width,(a.y-b.y)*height)>maxDistance*scale)d.invalid=true;
  if(d.frame.size()==fingers&&!d.invalid) {
   d.three=true;double x=0,y=0;for(auto& [id,f]:d.frame){x+=f.x;y+=f.y;}
   // Background raw HID has no monitor mapping. Limit recognition to the primary
   // touchscreen installation; convert native panel orientation into desktop axes.
   DEVMODEW dm={};dm.dmSize=sizeof(dm);EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&dm);
   x/=fingers;y/=fingers;double sx=x,sy=y;
   if(dm.dmDisplayOrientation==DMDO_90){sx=y;sy=1-x;}
   else if(dm.dmDisplayOrientation==DMDO_180){sx=1-x;sy=1-y;}
   else if(dm.dmDisplayOrientation==DMDO_270){sx=1-y;sy=x;}
   d.center={LONG(std::lround(sx*(GetSystemMetrics(SM_CXSCREEN)-1))),LONG(std::lround(sy*(GetSystemMetrics(SM_CYSCREEN)-1)))};
  }return false;
 }
 public:
 static bool SelfTest() {
  RawTouch r;POINT result={};
  auto start=[&](Device& d,unsigned n){for(unsigned i=0;i<n;++i)d.frame[LONG(i)]={LONG(i),.4+i*.01,.4};r.Complete(d,100,result);};
  Device valid;start(valid,3);valid.frame.clear();if(!r.Complete(valid,250,result))return false;
  Device extra;start(extra,4);extra.frame.clear();if(r.Complete(extra,250,result))return false;
  Device swipe;start(swipe,3);swipe.frame[0].x+=.1;r.Complete(swipe,180,result);swipe.frame.clear();if(r.Complete(swipe,250,result))return false;
  Device hold;start(hold,3);hold.frame.clear();if(r.Complete(hold,550,result))return false;
  Device distant;start(distant,3);distant.frame[0].x=.9;r.Complete(distant,150,result);distant.frame.clear();if(r.Complete(distant,250,result))return false;
  r.Configure(2,120);Device two;start(two,2);two.frame.clear();return r.Complete(two,250,result);
 }
 void Configure(unsigned count,double distance){count=std::clamp(count,2u,3u);distance=std::clamp(distance,40.0,3000.0);if(count!=fingers||distance!=maxDistance){Reset();fingers=count;maxDistance=distance;}}
 bool Enable(HWND hwnd,bool on) {
  if(on==enabled)return true;
  if(on) {
   UINT count=0;if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1)||count>1024)return false;
   std::vector<RAWINPUTDEVICELIST> list(count);
   if(GetRawInputDeviceList(list.data(),&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1))return false;
   unsigned touchscreens=0;
   for(UINT i=0;i<count;++i){RID_DEVICE_INFO info={};info.cbSize=sizeof(info);UINT size=sizeof(info);
    if(GetRawInputDeviceInfoW(list[i].hDevice,RIDI_DEVICEINFO,&info,&size)!=UINT(-1)&&info.dwType==RIM_TYPEHID&&info.hid.usUsagePage==0x0d&&info.hid.usUsage==4)++touchscreens;
   }
   // Do not map an unrelated panel onto the primary display.
   if(touchscreens!=1||GetSystemMetrics(SM_CMONITORS)!=1){SetLastError(ERROR_NOT_SUPPORTED);return false;}
  }
  RAWINPUTDEVICE rid={0x0d,0x04,on?DWORD(RIDEV_INPUTSINK|RIDEV_DEVNOTIFY):DWORD(RIDEV_REMOVE),on?hwnd:nullptr};
  if(!RegisterRawInputDevices(&rid,1,sizeof(rid)))return false;
  enabled=on;devices.clear();return true;
 }
 void Reset(){devices.clear();}
 size_t DeviceCount()const{return devices.size();}
 void Remove(HANDLE device){devices.erase(device);}
 bool Input(HRAWINPUT handle,POINT& point) {
  if(!enabled)return false;
  UINT bytes=0;if(GetRawInputData(handle,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER))!=0||bytes<sizeof(RAWINPUTHEADER)||bytes>65536)return false;
  std::vector<BYTE> raw(bytes);if(GetRawInputData(handle,RID_INPUT,raw.data(),&bytes,sizeof(RAWINPUTHEADER))!=bytes)return false;
  auto* input=reinterpret_cast<RAWINPUT*>(raw.data());if(input->header.dwType!=RIM_TYPEHID)return false;
  auto& hid=input->data.hid;
  if(!hid.dwSizeHid||!hid.dwCount||hid.dwCount>64||offsetof(RAWINPUT,data.hid.bRawData)+size_t(hid.dwSizeHid)*hid.dwCount>bytes)return false;
  if(!devices.count(input->header.hDevice)) {
   if(devices.size()>=8)return false;
   Device d;UINT n=0;GetRawInputDeviceInfoW(input->header.hDevice,RIDI_PREPARSEDDATA,nullptr,&n);
   if(!n||n>65536)return false;d.preparsed.resize(n);
   if(GetRawInputDeviceInfoW(input->header.hDevice,RIDI_PREPARSEDDATA,d.preparsed.data(),&n)==UINT(-1))return false;
   auto pp=reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsed.data());HIDP_CAPS caps={};
   if(HidP_GetCaps(pp,&caps)!=HIDP_STATUS_SUCCESS||caps.UsagePage!=0x0d||caps.Usage!=4||caps.NumberInputValueCaps>256||caps.NumberInputButtonCaps>256)return false;
   USHORT nv=caps.NumberInputValueCaps,nb=caps.NumberInputButtonCaps;d.values.resize(nv);d.buttons.resize(nb);
   if(nv)HidP_GetValueCaps(HidP_Input,d.values.data(),&nv,pp);d.values.resize(nv);
   if(nb)HidP_GetButtonCaps(HidP_Input,d.buttons.data(),&nb,pp);d.buttons.resize(nb);
   devices.emplace(input->header.hDevice,std::move(d));
  }
  auto& d=devices.at(input->header.hDevice);bool fire=false;
  for(DWORD i=0;i<hid.dwCount;++i) {
   char* report=reinterpret_cast<char*>(hid.bRawData+i*hid.dwSizeHid);ULONG count=0;bool counted=false;
   for(auto& c:d.values)if(Has(c,0x0d,0x54)){counted=Value(d,0x0d,0x54,c.LinkCollection,report,hid.dwSizeHid,count);if(counted)break;}
   if(!counted||count>64)continue;
   auto now=GetTickCount64();
   if(d.expected&&now-d.frameAt>100){d.expected=0;d.candidate=false;d.origin.clear();}
   if(count){d.expected=count;d.received=0;d.frame.clear();d.frameAt=now;}
   if(!d.expected){d.frame.clear();fire=Complete(d,now,point)||fire;continue;}
   std::set<USHORT> links;
   for(auto& c:d.values)if(Has(c,0x0d,0x51))links.insert(c.LinkCollection);
   for(auto link:links) {
    ULONG id=0,x=0,y=0;bool tip=false;const HIDP_VALUE_CAPS *xc=nullptr,*yc=nullptr;
    if(!Value(d,0x0d,0x51,link,report,hid.dwSizeHid,id)||!Tip(d,link,report,hid.dwSizeHid,tip))continue;
    if(tip){if(!Value(d,1,0x30,link,report,hid.dwSizeHid,x,&xc)||!Value(d,1,0x31,link,report,hid.dwSizeHid,y,&yc)||xc->LogicalMax<=xc->LogicalMin||yc->LogicalMax<=yc->LogicalMin){d.invalid=true;continue;}
     d.frame[LONG(id)]={LONG(id),Normal(x,xc),Normal(y,yc)};
    }
    if(++d.received>=d.expected)break;
   }
   if(d.received>=d.expected){d.expected=0;fire=Complete(d,now,point)||fire;}
  }return fire;
 }
};
}
