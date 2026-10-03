# Third-party licenses and notices / 第三方许可与声明

VDial 0.4.2 — 夜冷星灼 — Xingzhuo1989@live.cn

本目录保留实际 NuGet 依赖与自包含运行时提供的完整许可证、版权声明及第三方声明；原文不翻译、不删改。dependency-inventory.json 记录版本、文件和 SHA-256。解析依赖包含仅用于构建的工具，不表示全部工具都随产品发行。

主要组件：
- .NET 10.0.12 runtime：MIT，另见随附 THIRD-PARTY-NOTICES 中各组件的条款。分发时保留版权及许可全文。
- Microsoft Windows App SDK 1.8.260921001 与 WinUI：本 NuGet 二进制按随包 Microsoft Software License Terms 及 NOTICE 分发，不能因上游源码为开源就把全部二进制都标成 MIT。许可允许分发 NuGet 随应用部署的代码，并规定终端用户条款、商标和其它条件。
- WebView2 Loader 1.0.3179.45：按随包 LICENSE 和 NOTICE，作为 Windows App SDK 传递依赖；VDial 不使用浏览网页功能。
- System.Numerics.Tensors（如清单所列）：MIT 及随包第三方声明。
- Windows SDK / .NET Windows 投影 / WDK / MSVC：构建或系统集成相关条款各自适用。SDK/WDK 开发工具不作为产品文件分发；MSVC 静态运行时代码的再分发遵循 Visual Studio 条款。不得把 SDK 工具打入商店安装器。

Windows App SDK 的 NOTICE 含第三方源码获取渠道及适用 LGPL 调试反向工程例外，完整保留在子目录内；是否某段条款对应实际随包二进制，应结合最终发布内容继续核对，不能据此宣称整个 VDial 必须开源，也不能宣称不存在任何附加义务。

VDial 是独立开发的应用，与 Microsoft 没有隶属或背书关系。Windows、Microsoft Store 和 Surface Dial 是其相应权利人的商标。图标来自本项目用户提供图片及生成式图像处理，不是 Microsoft 提供的官方图标；公开商用前需确认输入图片权利及外观使用没有造成官方背书误解。

本清单不是为 VDial 自有代码授予开源许可证。正式发布前仍需确定应用自身终端用户条款、分发渠道、驱动正式签名及商店协议。

Exact upstream documents / 原始文件：

Microsoft.Web.WebView2/1.0.3179.45 — LICENSE.txt
  Microsoft.Web.WebView2-1.0.3179.45/LICENSE.txt
  Microsoft.Web.WebView2-1.0.3179.45/NOTICE.txt

Microsoft.Windows.SDK.BuildTools/10.0.26100.4654 — See accompanying terms

Microsoft.Windows.SDK.BuildTools.MSIX/1.7.20250829.1 — sdk_license.txt
  Microsoft.Windows.SDK.BuildTools.MSIX-1.7.20250829.1/NOTICE.txt
  Microsoft.Windows.SDK.BuildTools.MSIX-1.7.20250829.1/sdk_license.txt

Microsoft.WindowsAppSDK/1.8.260921001 — license.txt
  Microsoft.WindowsAppSDK-1.8.260921001/license.txt
  Microsoft.WindowsAppSDK-1.8.260921001/NOTICE.txt

Microsoft.WindowsAppSDK.AI/1.8.79 — license.txt
  Microsoft.WindowsAppSDK.AI-1.8.79/license.txt

Microsoft.WindowsAppSDK.Base/1.8.251216001 — license.txt
  Microsoft.WindowsAppSDK.Base-1.8.251216001/license.txt
  Microsoft.WindowsAppSDK.Base-1.8.251216001/NOTICE.txt

Microsoft.WindowsAppSDK.DWrite/1.8.25122902 — license.txt
  Microsoft.WindowsAppSDK.DWrite-1.8.25122902/license.txt

Microsoft.WindowsAppSDK.Foundation/1.8.260803002 — license.txt
  Microsoft.WindowsAppSDK.Foundation-1.8.260803002/license.txt

Microsoft.WindowsAppSDK.InteractiveExperiences/1.8.260708001 — license.txt
  Microsoft.WindowsAppSDK.InteractiveExperiences-1.8.260708001/license.txt

Microsoft.WindowsAppSDK.ML/1.8.2223 — license.txt
  Microsoft.WindowsAppSDK.ML-1.8.2223/license.txt
  Microsoft.WindowsAppSDK.ML-1.8.2223/ThirdPartyNotices.txt

Microsoft.WindowsAppSDK.Runtime/1.8.260921001 — license.txt
  Microsoft.WindowsAppSDK.Runtime-1.8.260921001/license.txt
  Microsoft.WindowsAppSDK.Runtime-1.8.260921001/NOTICE.txt

Microsoft.WindowsAppSDK.Widgets/1.8.251231004 — license.txt
  Microsoft.WindowsAppSDK.Widgets-1.8.251231004/license.txt

Microsoft.WindowsAppSDK.WinUI/1.8.260803003 — license.txt
  Microsoft.WindowsAppSDK.WinUI-1.8.260803003/license.txt
  Microsoft.WindowsAppSDK.WinUI-1.8.260803003/NOTICE.txt

System.Numerics.Tensors/9.0.0 — MIT
  System.Numerics.Tensors-9.0.0/LICENSE.TXT
  System.Numerics.Tensors-9.0.0/THIRD-PARTY-NOTICES.TXT
