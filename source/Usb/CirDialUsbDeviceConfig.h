#pragma once
#include <cstdint>
namespace vdial::usb {
// pid.codes/1209/0001 is reserved for PRIVATE TESTING ONLY. It is not unique
// and MUST NOT be used for distributed/sold/manufactured devices.
constexpr uint16_t VendorId=0x1209, ProductId=0x0001;
constexpr char BusId[]="1-1";
constexpr char Manufacturer[]="CirDial Development";
constexpr char Product[]="CirDial USB Radial POC";
constexpr char Serial[]="VDIALRAD0003";
constexpr uint32_t DeviceId=0x00010001;
}
