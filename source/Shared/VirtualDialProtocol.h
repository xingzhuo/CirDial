#pragma once
// Shared between the KMDF driver and the native test executable.
#include <guiddef.h>
#if defined(_KERNEL_MODE)
#include <devioctl.h>
#else
#include <winioctl.h>
#endif
// {5D405D3A-64A0-4D96-B726-0B7883BE5411}
DEFINE_GUID(GUID_DEVINTERFACE_VIRTUAL_DIAL,
    0x5d405d3a, 0x64a0, 0x4d96, 0xb7, 0x26, 0x0b, 0x78, 0x83, 0xbe, 0x54, 0x11);
#define VIRTUAL_DIAL_REPORT_ID 1
#define VIRTUAL_DIAL_COMMAND_VERSION 1
#define IOCTL_VIRTUAL_DIAL_SEND CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_VIRTUAL_DIAL_CAPS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VIRTUAL_DIAL_POSITION_VERSION 2
#define VIRTUAL_DIAL_LOGICAL_MAX 32767
#define VIRTUAL_DIAL_NULL_POSITION 65535
#pragma pack(push, 1)
typedef struct _VIRTUAL_DIAL_COMMAND {
    unsigned long Version;  // 1
    long RotationTenthsOfDegree; // signed relative amount, [-3600,3600]
    unsigned char ButtonDown; // 0 or 1; persistent state
    unsigned char Reserved[3];
} VIRTUAL_DIAL_COMMAND;
typedef struct _VIRTUAL_DIAL_COMMAND_V2 {
    unsigned long Version;
    long RotationTenthsOfDegree;
    unsigned char ButtonDown;
    unsigned char PositionValid;
    unsigned short X;
    unsigned short Y;
    unsigned short Reserved;
} VIRTUAL_DIAL_COMMAND_V2;
typedef struct _VIRTUAL_DIAL_CAPS {
    unsigned long Version;
    unsigned long ScreenWidthHundredthsMm;
    unsigned long ScreenHeightHundredthsMm;
    unsigned long DialDiameterHundredthsMm;
} VIRTUAL_DIAL_CAPS;
#pragma pack(pop)
