# CirDial

CirDial is a Windows virtual touch dial. It presents a touch-operated dial
and sends HID radial-controller reports so compatible applications can use
Windows' native radial tools. The native application is C++/Win32; its
on-demand settings UI is C#/WinUI 3. It supports ARM64 and x64.

The project is being prepared for an MSIX Microsoft Store release. The
official build will offer gesture summon as a one-time purchase, with free
color customization. Store product IDs and publishing approval are pending.
Current packages are development previews, not production installers.

## USB interface and current identifier

`source/Usb` implements a USB/IP virtual USB device with a HID System
Multi-Axis Controller collection, contact position, rotation and button
reports. USB descriptors are defined in `VirtualUsbDevice.cpp`; identity
constants are in `CirDialUsbDeviceConfig.h`. The app currently uses the
separately installed usbip-win2 transport driver. It exposes the USB/IP
endpoint only on IPv4 loopback, not the network.

**VID:PID `1209:0001` is for PRIVATE TESTING ONLY. It is not unique and must
not be used for redistributed, sold or manufactured devices.** We are
preparing a pid.codes application; no permanent PID has been assigned to
CirDial yet. Publishing source that documents this development configuration
does not authorize distribution of a device using that identifier.

## Build and verification

Install Visual Studio C++ tools, the .NET 10 SDK and the matching Windows
SDK/WDK packages. `source/packages.config` pins native SDK/WDK versions;
`source/App/CirDial.Settings/CirDial.Settings.csproj` pins the Windows App SDK.

The root build scripts accept ARM64 or x64. This source snapshot uses repository-local caches. Use explicit `-Packages` when supplying native
packages from another location.

```powershell
nuget restore .\source\packages.config -PackagesDirectory .\source\packages
.\build-native.ps1 -Platform ARM64 -Packages C:\path\to\native-packages\
.\build-settings.ps1 -Platform ARM64
.\verify-build.ps1 -Platform ARM64 -IncludeUI
.\verify-license-probe.ps1 -Platform ARM64
```

MSIX experiments use `build-msix.ps1 -Development`. Production requires real
Partner Center identity, a suitable production USB identity, an installed
driver path, deployment verification and capability approval. MSIX cannot
install the driver itself. Store release and driver deployment validation remain pending.

## Source layout

- `source/App/VirtualDial`: native UI, gesture recognition, rendering,
  timer scheduling, HID backend and license probe orchestration.
- `source/App/CirDial.Settings`: settings, localization, Store purchasing
  and an ordinary-user headless license probe.
- `source/Usb`: USB/IP transport and virtual HID device descriptors.
- `source/Shared`: shared report/protocol definitions.
- `source/Driver`: earlier KMDF/VHF development backend, kept for reference.
- `source/Test`: protocol/device tests and developer diagnostics.
- `pid-codes`: application explanation and submission templates.

Third-party license texts remain
unchanged. Project-owned source is offered under MIT; third-party components
retain their original terms. See `LICENSE`.
