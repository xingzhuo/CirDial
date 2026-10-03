#pragma once
#include <memory>
#include <cstdint>
namespace vdial::usb {
class UsbIpServer {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 UsbIpServer();~UsbIpServer();
 UsbIpServer(const UsbIpServer&)=delete;UsbIpServer& operator=(const UsbIpServer&)=delete;
 bool Start(uint16_t port=3240,uint32_t width=27400,uint32_t height=18300);
 void Stop();
 bool SendInput(int rotation,bool down);
 bool Ready()const;
 bool SendReport(int rotation,bool down,bool contact,uint16_t x,uint16_t y,unsigned waitMs=250);
 uint16_t Port()const;
 bool Imported()const;
 unsigned Error()const;
};
int RunUsbIpPoc(unsigned seconds);
int RunUsbIpInteractive();
bool ServerSelfTest();
}
