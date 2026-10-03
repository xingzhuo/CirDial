#pragma once
#include <windows.h>
#include <memory>
#include "../../Shared/VirtualDialProtocol.h"
namespace dial {
class IDialBackend {
public:
 virtual ~IDialBackend()=default;
 virtual void Connect()=0;
 virtual void Close()=0;
 virtual bool Connected()const=0;
 virtual bool Send(const VIRTUAL_DIAL_COMMAND_V2& report)=0;
 virtual bool Caps(VIRTUAL_DIAL_CAPS& caps)=0;
 virtual DWORD Error()const=0;
};
std::unique_ptr<IDialBackend> CreateDialBackend(bool usbip);
}
