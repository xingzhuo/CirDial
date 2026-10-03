#include <ntddk.h>
#include <wdf.h>
#include <wdmsec.h>
#include <vhf.h>
#include <initguid.h>
#include "../../Shared/VirtualDialProtocol.h"

typedef struct _DEVICE_CONTEXT {
    VHFHANDLE Vhf;
    VIRTUAL_DIAL_CAPS Caps;
    UCHAR Descriptor[160];
    USHORT DescriptorSize;
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceContext);
EVT_WDF_DRIVER_DEVICE_ADD DialDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP DialCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DialIoControl;

// Native-panel X/Y coordinates; physical units are 0.001 cm (0.01 mm).
// Constant Width and nullable X/Y follow the Windows radial HID protocol.
#define LE32(v) static_cast<UCHAR>((v)&255), static_cast<UCHAR>(((v)>>8)&255), static_cast<UCHAR>(((v)>>16)&255), static_cast<UCHAR>(((v)>>24)&255)
static void BuildDescriptor(DEVICE_CONTEXT* ctx) {
    const ULONG width=ctx->Caps.ScreenWidthHundredthsMm;
    const ULONG height=ctx->Caps.ScreenHeightHundredthsMm;
    const ULONG diameter=ctx->Caps.DialDiameterHundredthsMm;
    const UCHAR bytes[]={
        0x05,0x01,0x09,0x0e,0xa1,0x01,0x85,VIRTUAL_DIAL_REPORT_ID,
        0x05,0x0d,0x09,0x21,0xa1,0x00,
        0x05,0x09,0x09,0x01,0x95,0x01,0x75,0x01,0x15,0x00,0x25,0x01,0x81,0x02,
        0x05,0x0d,0x09,0x33,0x81,0x02, // Touch: puck is on the display
        0x75,0x06,0x81,0x03, // byte-align the two switches
        0x05,0x01,0x09,0x37,0x75,0x10,0x55,0x0f,0x65,0x14,
        0x36,0xf0,0xf1,0x46,0x10,0x0e,0x16,0xf0,0xf1,0x26,0x10,0x0e,0x81,0x06,
        0x09,0x30,0x75,0x10,0x55,0x0d,0x65,0x11,0x35,0x00,
        0x47,LE32(width),0x15,0x00,0x26,0xff,0x7f,0x81,0x42,
        0x09,0x31,0x47,LE32(height),0x81,0x42,
        0x05,0x0d,0x09,0x48,
        0x37,LE32(diameter),0x47,LE32(diameter),
        0x17,LE32(diameter),0x27,LE32(diameter),0x81,0x03,
        0xc0,0xc0
    };
    static_assert(sizeof(bytes)<=sizeof(ctx->Descriptor),"Descriptor buffer too small");
    RtlCopyMemory(ctx->Descriptor,bytes,sizeof(bytes)); ctx->DescriptorSize=static_cast<USHORT>(sizeof(bytes));
}
#undef LE32
static NTSTATUS Submit(DEVICE_CONTEXT* ctx,LONG rotation,UCHAR button,USHORT x,USHORT y) {
    const USHORT packed=static_cast<USHORT>(rotation);
    const UCHAR switches=static_cast<UCHAR>(button|((x!=VIRTUAL_DIAL_NULL_POSITION&&y!=VIRTUAL_DIAL_NULL_POSITION)?2:0));
    const USHORT diameter=static_cast<USHORT>(ctx->Caps.DialDiameterHundredthsMm);
    UCHAR bytes[]={VIRTUAL_DIAL_REPORT_ID,switches,static_cast<UCHAR>(packed),static_cast<UCHAR>(packed>>8),
        static_cast<UCHAR>(x),static_cast<UCHAR>(x>>8),static_cast<UCHAR>(y),static_cast<UCHAR>(y>>8),
        static_cast<UCHAR>(diameter),static_cast<UCHAR>(diameter>>8)};
    HID_XFER_PACKET packet={}; packet.reportBuffer=bytes; packet.reportBufferLen=sizeof(bytes); packet.reportId=VIRTUAL_DIAL_REPORT_ID;
    return VhfReadReportSubmit(ctx->Vhf,&packet);
}
void DialCleanup(WDFOBJECT object) {
    auto ctx=DeviceContext(reinterpret_cast<WDFDEVICE>(object));
    if(ctx->Vhf) { VhfDelete(ctx->Vhf,TRUE); ctx->Vhf=nullptr; }
}
void DialIoControl(WDFQUEUE queue,WDFREQUEST request,size_t outLength,size_t inLength,ULONG code) {
    UNREFERENCED_PARAMETER(outLength);
    auto ctx=DeviceContext(WdfIoQueueGetDevice(queue)); NTSTATUS status=STATUS_INVALID_DEVICE_REQUEST;
    if(code==IOCTL_VIRTUAL_DIAL_CAPS) {
        VIRTUAL_DIAL_CAPS* caps=nullptr;
        status=WdfRequestRetrieveOutputBuffer(request,sizeof(*caps),reinterpret_cast<PVOID*>(&caps),nullptr);
        if(NT_SUCCESS(status)) { *caps=ctx->Caps; WdfRequestCompleteWithInformation(request,status,sizeof(*caps)); return; }
    } else if(code==IOCTL_VIRTUAL_DIAL_SEND) {
        PVOID buffer=nullptr; status=WdfRequestRetrieveInputBuffer(request,sizeof(ULONG),&buffer,nullptr);
        if(NT_SUCCESS(status)) {
            LONG rotation=0; UCHAR button=0; USHORT x=VIRTUAL_DIAL_NULL_POSITION,y=VIRTUAL_DIAL_NULL_POSITION; bool valid=false;
            if(*static_cast<ULONG*>(buffer)==VIRTUAL_DIAL_COMMAND_VERSION && inLength==sizeof(VIRTUAL_DIAL_COMMAND)) {
                const auto c=static_cast<VIRTUAL_DIAL_COMMAND*>(buffer); rotation=c->RotationTenthsOfDegree; button=c->ButtonDown;
                valid=c->Reserved[0]==0&&c->Reserved[1]==0&&c->Reserved[2]==0;
            } else if(*static_cast<ULONG*>(buffer)==VIRTUAL_DIAL_POSITION_VERSION && inLength==sizeof(VIRTUAL_DIAL_COMMAND_V2)) {
                const auto c=static_cast<VIRTUAL_DIAL_COMMAND_V2*>(buffer); rotation=c->RotationTenthsOfDegree; button=c->ButtonDown;
                valid=c->Reserved==0&&c->PositionValid<=1;
                if(c->PositionValid) { x=c->X; y=c->Y; valid=valid&&x<=VIRTUAL_DIAL_LOGICAL_MAX&&y<=VIRTUAL_DIAL_LOGICAL_MAX; }
            }
            status=valid&&button<=1&&rotation>=-3600&&rotation<=3600?Submit(ctx,rotation,button,x,y):STATUS_INVALID_PARAMETER;
        }
    }
    WdfRequestComplete(request,status);
}
static ULONG ReadDimension(WDFKEY key,PCWSTR name,ULONG fallback,ULONG minimum,ULONG maximum) {
    UNICODE_STRING valueName; RtlInitUnicodeString(&valueName,name); ULONG value=0;
    return NT_SUCCESS(WdfRegistryQueryULong(key,&valueName,&value))&&value>=minimum&&value<=maximum?value:fallback;
}
NTSTATUS DialDeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT init) {
    UNREFERENCED_PARAMETER(driver);
    WdfDeviceInitSetCharacteristics(init,FILE_AUTOGENERATED_DEVICE_NAME|FILE_DEVICE_SECURE_OPEN,TRUE);
    NTSTATUS status=WdfDeviceInitAssignSDDLString(init,&SDDL_DEVOBJ_SYS_ALL_ADM_ALL); if(!NT_SUCCESS(status)) return status;
    WDF_OBJECT_ATTRIBUTES attributes; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,DEVICE_CONTEXT);
    attributes.ExecutionLevel=WdfExecutionLevelPassive; attributes.EvtCleanupCallback=DialCleanup;
    WDFDEVICE device; status=WdfDeviceCreate(&init,&attributes,&device); if(!NT_SUCCESS(status)) return status;
    auto ctx=DeviceContext(device); ctx->Vhf=nullptr;
    // Local Surface Pro X EDID is 274 x 183 mm. Other panels can override the
    // values in Device Parameters before starting the device.
    ctx->Caps={VIRTUAL_DIAL_POSITION_VERSION,27400,18300,5900};
    WDFKEY key;
    if(NT_SUCCESS(WdfDeviceOpenRegistryKey(device,PLUGPLAY_REGKEY_DEVICE,KEY_READ,WDF_NO_OBJECT_ATTRIBUTES,&key))) {
        ctx->Caps.ScreenWidthHundredthsMm=ReadDimension(key,L"ScreenWidthHundredthsMm",27400,5000,200000);
        ctx->Caps.ScreenHeightHundredthsMm=ReadDimension(key,L"ScreenHeightHundredthsMm",18300,5000,200000);
        ctx->Caps.DialDiameterHundredthsMm=ReadDimension(key,L"DialDiameterHundredthsMm",5900,2000,12000);
        WdfRegistryClose(key);
    }
    BuildDescriptor(ctx);
    WDF_IO_QUEUE_CONFIG queue; WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue,WdfIoQueueDispatchSequential); queue.EvtIoDeviceControl=DialIoControl;
    status=WdfIoQueueCreate(device,&queue,WDF_NO_OBJECT_ATTRIBUTES,nullptr); if(!NT_SUCCESS(status)) return status;
    status=WdfDeviceCreateDeviceInterface(device,&GUID_DEVINTERFACE_VIRTUAL_DIAL,nullptr); if(!NT_SUCCESS(status)) return status;
    VHF_CONFIG config; VHF_CONFIG_INIT(&config,WdfDeviceWdmGetDeviceObject(device),ctx->DescriptorSize,ctx->Descriptor);
    config.VendorID=0x1209; config.ProductID=0x0001; config.VersionNumber=0x0004;
    status=VhfCreate(&config,&ctx->Vhf); if(!NT_SUCCESS(status)) { ctx->Vhf=nullptr; return status; }
    return VhfStart(ctx->Vhf);
}
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config; WDF_DRIVER_CONFIG_INIT(&config,DialDeviceAdd);
    return WdfDriverCreate(driver,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

