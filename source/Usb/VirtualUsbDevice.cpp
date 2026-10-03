#include "VirtualUsbDevice.h"
#include "CirDialUsbDeviceConfig.h"
#include <algorithm>
#include <cstring>
#include <string>
namespace vdial::usb {
uint16_t Be16(const uint8_t* p){return uint16_t((uint16_t(p[0])<<8)|p[1]);}
uint32_t Be32(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
void Put16(Bytes& b,uint16_t v){b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));}
void Put32(Bytes& b,uint32_t v){Put16(b,uint16_t(v>>16));Put16(b,uint16_t(v));}
static uint16_t Le16(const uint8_t* p){return uint16_t(p[0]|(uint16_t(p[1])<<8));}
Bytes VirtualUsbDevice::ReportDescriptor()const {
 // Preserve VHF report layout, applying the same runtime touch-range calibration.
 Bytes report={0x05,0x01,0x09,0x0e,0xa1,0x01,0x85,0x01,0x05,0x0d,0x09,0x21,0xa1,0x00,0x05,0x09,0x09,0x01,0x95,0x01,0x75,0x01,0x15,0x00,0x25,0x01,0x81,0x02,0x05,0x0d,0x09,0x33,0x81,0x02,0x75,0x06,0x81,0x03,0x05,0x01,0x09,0x37,0x75,0x10,0x55,0x0f,0x65,0x14,0x36,0xf0,0xf1,0x46,0x10,0x0e,0x16,0xf0,0xf1,0x26,0x10,0x0e,0x81,0x06,0x09,0x30,0x75,0x10,0x55,0x0d,0x65,0x11,0x35,0x00,0x47,0x08,0x6b,0x00,0x00,0x15,0x00,0x26,0xff,0x7f,0x81,0x42,0x09,0x31,0x47,0x7c,0x47,0x00,0x00,0x81,0x42,0x05,0x0d,0x09,0x48,0x37,0x0c,0x17,0x00,0x00,0x47,0x0c,0x17,0x00,0x00,0x17,0x0c,0x17,0x00,0x00,0x27,0x0c,0x17,0x00,0x00,0x81,0x03,0xc0,0xc0};
 for(unsigned i=0;i<4;++i){report[73+i]=uint8_t(width_>>(i*8));report[87+i]=uint8_t(height_>>(i*8));}
 return report;
}
Bytes VirtualUsbDevice::HidDescriptor()const {
 return {9,0x21,0x11,1,0,1,0x22,uint8_t(ReportDescriptor().size()),0};
}
Bytes VirtualUsbDevice::DeviceDescriptor()const {
 return {18,1,0,2,0,0,0,64,uint8_t(VendorId&255),uint8_t(VendorId>>8),uint8_t(ProductId&255),uint8_t(ProductId>>8),0,1,1,2,3,1};
}
Bytes VirtualUsbDevice::ConfigurationDescriptor()const {
 Bytes b={9,2,34,0,1,1,0,0x80,50,9,4,0,0,1,3,0,0,0};
 auto hid=HidDescriptor();b.insert(b.end(),hid.begin(),hid.end());
 b.insert(b.end(),{7,5,0x81,3,10,0,10});return b;
}
Bytes VirtualUsbDevice::ExportRecord()const {
 Bytes b(288,0);constexpr char path[]="/vdial/usbip/1-1";
 std::memcpy(b.data(),path,sizeof(path));std::memcpy(b.data()+256,BusId,sizeof(BusId));
 Put32(b,1);Put32(b,1);Put32(b,2); // full-speed, not a USB3 emulation
 Put16(b,VendorId);Put16(b,ProductId);Put16(b,0x0100);
 b.insert(b.end(),{0,0,0,configuration_,1,1});return b;
}
static Bytes StringDescriptor(const char* text){Bytes b={2,3};for(;*text;++text){b.push_back(uint8_t(*text));b.push_back(0);}b[0]=uint8_t(b.size());return b;}
ControlResult VirtualUsbDevice::Control(const std::array<uint8_t,8>& s) {
 const auto value=Le16(s.data()+2),index=Le16(s.data()+4),length=Le16(s.data()+6);
 ControlResult r;
 if(s[0]==0x81&&s[1]==6&&index==0&&(value&255)==0) {
  if((value>>8)==0x21)r.data=HidDescriptor();
  else if((value>>8)==0x22)r.data=ReportDescriptor();
  if(!r.data.empty()){r.status=0;r.data.resize(std::min<size_t>(length,r.data.size()));}
 } else if(s[0]==0xa1&&s[1]==1&&value==0x0101&&index==0&&length>0){r.status=0;r.data={1,0,0,0,0xff,0xff,0xff,0xff,0x0c,0x17};r.data.resize(std::min<size_t>(length,r.data.size()));}
 // Idle=0 suppresses duplicate reports. Nonzero idle is not implemented in this POC.
 else if(s[0]==0x21&&s[1]==10&&(value==0||value==1)&&index==0&&length==0){r.status=0;}
 else if(s[0]==0xa1&&s[1]==2&&value==1&&index==0&&length==1){r.status=0;r.data={0};}
 else if(s[0]==2&&s[1]==1&&value==0&&index==0x81&&length==0){r.status=0;}
 else if(s[0]==0x82&&s[1]==0&&value==0&&index==0x81&&length==2){r.status=0;r.data={0,0};}
 else if(s[0]==0x80&&s[1]==6) {
  if((value>>8)==1&&(value&255)==0&&index==0)r.data=DeviceDescriptor();
  else if((value>>8)==2&&(value&255)==0&&index==0)r.data=ConfigurationDescriptor();
  else if((value>>8)==3) {
   if((value&255)==0&&index==0)r.data={4,3,9,4};
   else if(index==0x0409) {
    switch(value&255){case 1:r.data=StringDescriptor(Manufacturer);break;case 2:r.data=StringDescriptor(Product);break;case 3:{std::string serial=Serial;
     if(width_!=27400||height_!=18300)serial+="-"+std::to_string(width_)+"-"+std::to_string(height_);
     r.data=StringDescriptor(serial.c_str());break;}}
   }
  }
  if(!r.data.empty()){r.status=0;r.data.resize(std::min<size_t>(length,r.data.size()));}
 } else if(s[0]==0&&s[1]==5&&value<=127&&index==0&&length==0){address_=uint8_t(value);r.status=0;}
 else if(s[0]==0&&s[1]==9&&value<=1&&index==0&&length==0){configuration_=uint8_t(value);r.status=0;}
 else if(s[0]==0x80&&s[1]==8&&value==0&&index==0&&length==1){r.status=0;r.data={configuration_};}
 else if((s[0]==0x80||s[0]==0x81||s[0]==0x82)&&s[1]==0&&value==0&&index==0&&length==2){r.status=0;r.data={0,0};}
 else if(s[0]==0x81&&s[1]==10&&configuration_==1&&value==0&&index==0&&length==1){r.status=0;r.data={0};}
 else if(s[0]==1&&s[1]==11&&configuration_==1&&value==0&&index==0&&length==0)r.status=0;
 return r;
}
bool ValidateSubmit(const std::array<uint8_t,48>& h) {
 if(Be32(h.data())!=1||Be32(h.data()+8)!=DeviceId||Be32(h.data()+12)>1||Be32(h.data()+16)>1)return false;
 const auto length=Be32(h.data()+24),iso=Be32(h.data()+32);
 if(length>4096||(iso!=0&&iso!=0xffffffff))return false;
 if(Be32(h.data()+16)==1) {
  return Be32(h.data()+12)==1&&length>=10&&length<=64&&
   std::all_of(h.begin()+40,h.end(),[](uint8_t b){return b==0;});
 }
 if(length!=Le16(h.data()+46))return false;
 return uint32_t((h[40]>>7)&1)==Be32(h.data()+12);
}
bool ProtocolSelfTest() {
 VirtualUsbDevice mapped(29340,16520);auto calibrated=mapped.ReportDescriptor();
 if(calibrated[73]!=0x9c||calibrated[74]!=0x72||calibrated[87]!=0x88||calibrated[88]!=0x40)return false;
 VirtualUsbDevice d;auto bytes=d.DeviceDescriptor();auto config=d.ConfigurationDescriptor();
 if(bytes.size()!=18||bytes[0]!=18||config.size()!=Le16(config.data()+2)||d.ExportRecord().size()!=312)return false;
 if(Le16(bytes.data()+8)!=VendorId||Le16(bytes.data()+10)!=ProductId)return false;
 auto r=d.Control({0x80,6,0,1,0,0,8,0});if(r.status||r.data.size()!=8)return false;
 r=d.Control({0x80,6,0,1,0,0,255,0});if(r.status||r.data.size()!=18)return false;
 if(d.Control({0,9,1,0,0,0,0,0}).status||d.Configuration()!=1)return false;
 if(d.Control({0,9,2,0,0,0,0,0}).status==0||d.Configuration()!=1)return false;
 if(d.Control({0x80,6,0,0x99,0,0,8,0}).status==0)return false;
 d.Reset();if(d.Configuration()!=0)return false;
 std::array<uint8_t,48> h={};h[3]=1;h[9]=1;h[11]=1;h[15]=1;h[27]=18;h[40]=0x80;h[41]=6;h[43]=1;h[46]=18;
 if(!ValidateSubmit(h))return false;
 h[24]=0xff;if(ValidateSubmit(h))return false;h[24]=0;
 h[19]=1;if(ValidateSubmit(h))return false;h[19]=0;
 h[35]=1;if(ValidateSubmit(h))return false;h[35]=0;
 h[15]=2;if(ValidateSubmit(h))return false;
 return true;
}
}
