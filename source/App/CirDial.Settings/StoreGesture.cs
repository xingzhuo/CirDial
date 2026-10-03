using Windows.Services.Store;
using System.Security.Principal;
using System.Text.Json;

namespace CirDial.Settings;

// Store entitlement is kept in memory and refreshed from the Store license.
// User preferences never constitute proof of purchase.
sealed class StoreGesture {
 readonly SemaphoreSlim gate=new(1,1);
 StoreContext? context;
 string productId="";
 public bool Owned {get;private set;}
 public string Price {get;private set;}="";
 public bool Available {get;private set;}
 public async Task Refresh(nint hwnd) {
  await gate.WaitAsync();
  try { await RefreshCore(hwnd); } finally { gate.Release(); }
 }
 async Task RefreshCore(nint hwnd) {
  Owned=false;Available=false;Price="";context=null;
  var config=System.IO.Path.Combine(AppContext.BaseDirectory,"store-config.json");
  if(!File.Exists(config))return;
  using var json=JsonDocument.Parse(await File.ReadAllTextAsync(config));
  productId=json.RootElement.GetProperty("GestureProductStoreId").GetString()??"";
  if(string.IsNullOrWhiteSpace(productId))return;
  if(new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))return;
  try {_=Windows.ApplicationModel.Package.Current.Id;} catch {return;}
  context=StoreContext.GetDefault();
  if(hwnd!=0)WinRT.Interop.InitializeWithWindow.Initialize(context,hwnd);
  using var queryTimeout=new CancellationTokenSource(TimeSpan.FromSeconds(30));
  var license=await context.GetAppLicenseAsync().AsTask(queryTimeout.Token);

  Owned=license.IsActive&&license.AddOnLicenses.Values.Any(l=>l.IsActive&&(l.SkuStoreId==productId||l.SkuStoreId.StartsWith(productId+"/",StringComparison.Ordinal)));
  // A valid existing license remains usable if product/price discovery fails.
  var products=await context.GetStoreProductsAsync(new[]{"Durable"},new[]{productId}).AsTask(queryTimeout.Token);
  if(products.ExtendedError!=null)throw products.ExtendedError;
  var product=products.Products.Values.FirstOrDefault(p=>p.StoreId==productId);
  if(product!=null){Price=product.Price.FormattedPrice;Available=true;}
 }
 public async Task<bool> Purchase(nint hwnd) {
  await gate.WaitAsync();
  try {
   if(!Available||context==null)return false;
   var result=await context.RequestPurchaseAsync(productId);
   if(result.ExtendedError!=null)throw result.ExtendedError;
   if(result.Status!=StorePurchaseStatus.Succeeded&&result.Status!=StorePurchaseStatus.AlreadyPurchased)return false;
   await RefreshCore(hwnd);return Owned;
  } finally {gate.Release();}
 }
}
