#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace vdial::usb {
using Bytes=std::vector<uint8_t>;
uint16_t Be16(const uint8_t* p);
uint32_t Be32(const uint8_t* p);
void Put16(Bytes& b,uint16_t v);
void Put32(Bytes& b,uint32_t v);
struct ControlResult {int32_t status=-32;Bytes data;};
// Phase 3: original VHF Dial descriptor; active input is still gated.
class VirtualUsbDevice {
    uint8_t configuration_=0,address_=0;
    uint32_t width_=27400,height_=18300;
public:
    VirtualUsbDevice(uint32_t width=27400,uint32_t height=18300):width_(width),height_(height){}
    Bytes ReportDescriptor() const;
    Bytes HidDescriptor() const;
    Bytes DeviceDescriptor() const;
    Bytes ConfigurationDescriptor() const;
    Bytes ExportRecord() const;
    ControlResult Control(const std::array<uint8_t,8>& setup);
    void Reset(){configuration_=address_=0;}
    uint8_t Configuration()const{return configuration_;}
};
bool ValidateSubmit(const std::array<uint8_t,48>& header);
bool ProtocolSelfTest();
}
