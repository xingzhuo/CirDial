using System.IO.Pipes;
using System.Security.Principal;
using System.Text.Json;
using Windows.Services.Store;

namespace CirDial.Settings;

static class LicenseProbe {
 // Short-lived ordinary-user process. No window or WinUI application is created.
 internal static async Task<int> Run(string pipeName) {
  if(!pipeName.StartsWith("CirDial.License.",StringComparison.Ordinal)||pipeName.Length>180)return 2;
  bool owned=false;
  try {
   if(!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)) {
    _=Windows.ApplicationModel.Package.Current.Id;
    using var config=JsonDocument.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory,"store-config.json")));
    string id=config.RootElement.GetProperty("GestureProductStoreId").GetString()??"";
    if(!string.IsNullOrWhiteSpace(id)) {
     using var queryTimeout=new CancellationTokenSource(TimeSpan.FromSeconds(10));
     var license=await StoreContext.GetDefault().GetAppLicenseAsync().AsTask(queryTimeout.Token);
     owned=license.IsActive&&license.AddOnLicenses.Values.Any(l=>l.IsActive&&(l.SkuStoreId==id||l.SkuStoreId.StartsWith(id+"/",StringComparison.Ordinal)));
    }
   }
  } catch {owned=false;}
  try {
   using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(5));
   await using var pipe=new NamedPipeClientStream(".",pipeName,PipeDirection.Out,PipeOptions.Asynchronous);
   await pipe.ConnectAsync(timeout.Token);
   await pipe.WriteAsync(new byte[]{owned?(byte)1:(byte)0},timeout.Token);
   await pipe.FlushAsync(timeout.Token);
   return 0;
  } catch {return 3;}
 }
}
