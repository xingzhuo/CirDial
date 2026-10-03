# CirDial pid.codes application preparation

Status: local draft. No source repository has been published and no request
has been submitted or approved.

The user authorized pursuing the open-source eligibility route on 2026-10-03.
The planned source license is MIT, preserving third-party notices. Connect
GitHub before publishing the repository and submitting the application.

## Eligibility and technical justification

CirDial implements a software-defined USB HID radial controller. The
public source repository will contain the complete virtual USB interface
implementation, descriptors, USB/IP transport, native dial UI, settings,
protocol definitions and tests. It has no custom PCB or physical firmware.

The virtual device enumerates through the USB/IP virtual host-controller
driver, so its USB device descriptor needs a stable VID/PID. The desired
identity belongs to the CirDial virtual device implementation, rather than
the third-party host-controller driver. A shared permanent identity avoids
asking every app user to register a separate USB product identity. The
project needs one PID for the same device implementation on ARM64 and x64;
those architectures do not create different USB products.

The implementation uses a HID System Multi-Axis Controller top-level
collection to send rotation, button and screen-position reports into the
Windows radial-controller stack. It neither emulates a keyboard nor exposes
its USB/IP service to remote hosts; the endpoint binds to 127.0.0.1.

pid.codes explicitly allows software-only applications to be considered
with additional justification, but allocation is at the maintainers'
discretion. This application must explain that there is no physical board
and must not imply hardware certification or prior PID approval.

## Submission steps once GitHub is connected

1. Publish the reviewed source export as a public CirDial repository with
   MIT license and retained third-party notices.
2. Record its real owner and repository URL in the application templates.
3. Check the current pid.codes allocation list for a free, non-reserved PID.
   Do not invent an allocation or select the private-test/reserved ranges.
4. Fork pidcodes/pidcodes.github.com and add the organization and PID entry
   using the real owner, source URL, selected available PID and MIT license.
5. Submit a pull request with the technical justification above.
6. Wait for approval. Only then replace the private test identity, update
   descriptors/branding and test Windows enumeration, native tools and
   screen-position behavior on real hardware before distributing a device.

Sources: https://pid.codes/howto/ and https://pid.codes/1209/0001/
