using System.Runtime.InteropServices;
using System.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Input;
using Windows.Globalization.NumberFormatting;
using Windows.Graphics;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace CirDial.Settings;
static class Program {
 [STAThread] static void Main() {
  var launchArgs=Environment.GetCommandLineArgs();
  int probeFlag=Array.IndexOf(launchArgs,"--license-check");
  if(probeFlag>=0) {
   WinRT.ComWrappersSupport.InitializeComWrappers();
   Environment.ExitCode=probeFlag+1<launchArgs.Length?Task.Run(()=>LicenseProbe.Run(launchArgs[probeFlag+1])).GetAwaiter().GetResult():2;
   return;
  }
  Native.SetProcessDpiAwarenessContext(new nint(-4));
  bool test=Environment.GetCommandLineArgs().Any(a=>a=="--smoke-test"||a=="--render-preview"||a=="--startup-check"||a=="--ui-test");
  bool exitMenu=Environment.GetCommandLineArgs().Contains("--tray-exit");
  using var singleton=new Mutex(true,test?"Local\\CirDial.Settings.Test."+Environment.ProcessId:exitMenu?"Local\\CirDial.ExitMenu":"Local\\CirDial.Settings",out bool created);
  if(!created) { Native.SetForegroundWindow(Native.FindWindowW(null,"CirDial Settings")); return; }
  WinRT.ComWrappersSupport.InitializeComWrappers();
  Application.Start(_=>{
   SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread()));
   new SettingsApp();
  });
 }
}
public sealed partial class SettingsApp:Application {
 Window? window;
 internal static void Error(Exception error) {try{Directory.CreateDirectory(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"CirDial"));File.WriteAllText(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"CirDial","settings-error.txt"),error.ToString());}catch{}}
 public SettingsApp(){UnhandledException+=(_,e)=>Error(e.Exception);InitializeComponent();}
 protected override void OnLaunched(LaunchActivatedEventArgs args) {try{ window=Environment.GetCommandLineArgs().Contains("--tray-exit")?new ExitWindow():new SettingsWindow(); window.Activate(); }catch(Exception ex){Error(ex);throw;} }
}
static class Native {
 [DllImport("dwmapi.dll")] internal static extern int DwmSetWindowAttribute(nint hwnd,uint attribute,ref uint value,uint size);
 [DllImport("user32.dll")] internal static extern bool SetProcessDpiAwarenessContext(nint context);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] internal static extern nint FindWindowW(string? cls,string? title);
 [DllImport("user32.dll")] internal static extern bool PostMessageW(nint hwnd,uint msg,nuint wp,nint lp);
 [DllImport("user32.dll")] internal static extern bool SetForegroundWindow(nint hwnd);
 [DllImport("user32.dll")] internal static extern uint GetDpiForWindow(nint hwnd);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern uint GetPrivateProfileStringW(string section,string key,string fallback,StringBuilder value,uint size,string path);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool WritePrivateProfileStringW(string section,string key,string value,string path);
 static readonly string PathName=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"VDial","settings.ini");
 internal static int Read(string key,int fallback) { var b=new StringBuilder(64); GetPrivateProfileStringW("Dial",key,fallback.ToString(),b,64,PathName); return int.TryParse(b.ToString(),out int v)?v:fallback; }
 internal static void Write(string key,int value) { Directory.CreateDirectory(System.IO.Path.GetDirectoryName(PathName)!); if(!WritePrivateProfileStringW("Dial",key,value.ToString(),PathName)) throw new IOException("无法保存设置"); }
 internal static void Command(nuint command) => PostMessageW(FindWindowW("CirDial.Touch.1",null),0x8004,command,0);
}
sealed class SettingsWindow:Window {
 readonly StackPanel body=new(){Spacing=18,Padding=new Thickness(24)};
 readonly Dictionary<string,int> pending=new();
 readonly Dictionary<string,ToggleSwitch> toggles=new();
 readonly Microsoft.UI.Dispatching.DispatcherQueueTimer syncTimer;
 readonly Microsoft.UI.Dispatching.DispatcherQueueTimer debounce;
 
 readonly StoreGesture store=new();
 TextBlock storeStatus=new();Button buyButton=new();
 bool wheelAttached;
 Microsoft.UI.Dispatching.DispatcherQueueTimer? startupTimer;
 readonly ScrollViewer scroll=new(){VerticalScrollMode=ScrollMode.Enabled,HorizontalScrollMode=ScrollMode.Disabled,ZoomMode=ZoomMode.Disabled,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
 bool loading;
 public SettingsWindow() {
  Title="CirDial Settings";
  debounce=DispatcherQueue.CreateTimer(); debounce.Interval=TimeSpan.FromMilliseconds(180); debounce.IsRepeating=false; debounce.Tick+=(_,_)=>Flush();
  BuildContent();SystemTheme.Follow(scroll);
  syncTimer=DispatcherQueue.CreateTimer();syncTimer.Interval=TimeSpan.FromMilliseconds(1000);syncTimer.Tick+=(_,_)=>SyncToggles();syncTimer.Start();
  ((FrameworkElement)Content).Loaded+=async(_,_)=>await RefreshStore();
  var presenter=Microsoft.UI.Windowing.OverlappedPresenter.Create(); presenter.IsMaximizable=false;AppWindow.SetPresenter(presenter);
  uint border=0xfffffffe;Native.DwmSetWindowAttribute(WinRT.Interop.WindowNative.GetWindowHandle(this),34,ref border,4);
  var area=Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id,Microsoft.UI.Windowing.DisplayAreaFallback.Primary).WorkArea;
  double scale=Native.GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this))/96.0;
  int w=Math.Min((int)(480*scale),area.Width-24), h=Math.Min((int)(820*scale),area.Height-24);AppWindow.MoveAndResize(new RectInt32(area.X+area.Width-w-12,area.Y+area.Height-h-12,w,h));
  var icon=System.IO.Path.Combine(AppContext.BaseDirectory,"VirtualDial.ico");if(File.Exists(icon)) AppWindow.SetIcon(icon);
  Closed+=(_,_)=>{syncTimer.Stop();Flush();Application.Current.Exit();};
  var args=Environment.GetCommandLineArgs();
  if(args.Contains("--smoke-test")||args.Contains("--startup-check")||args.Contains("--ui-test")) {
   ((FrameworkElement)Content).Loaded+=(_,_)=>{
    startupTimer=DispatcherQueue.CreateTimer();startupTimer.Interval=TimeSpan.FromMilliseconds(700);startupTimer.IsRepeating=false;
    startupTimer.Tick+=async(_,_)=>{
     try {
      if(args.Contains("--ui-test")) {
       if(body.Children[0] is not TextBlock heading||heading.Text!="CirDial")throw new InvalidOperationException("Title must be first");
       if(store.Owned||toggles["GestureSummon"].IsEnabled)throw new InvalidOperationException("Unconfigured Store must keep gesture summon locked");
       foreach(var appearance in body.Children.OfType<Expander>()) {appearance.IsExpanded=true;await Task.Delay(50);}
       var freePickers=body.Children.OfType<Expander>().Select(e=>e.Content).OfType<ColorPicker>().ToArray();
       if(freePickers.Length!=3||freePickers.Any(p=>!p.IsEnabled))throw new InvalidOperationException("All three colors must be free without a Store license");
       int pendingBefore=pending.Count;
       SyncToggles((_,_)=>1);
       if(!toggles["OnScreen"].IsOn||toggles["GestureSummon"].IsOn)throw new InvalidOperationException("Toggle synchronization failed");
       SyncToggles((_,_)=>0);
       if(toggles["OnScreen"].IsOn||toggles["GestureSummon"].IsOn||pending.Count!=pendingBefore)throw new InvalidOperationException("Toggle sync wrote settings back");
       SyncToggles();
       ScrollWheel(-120);await Task.Delay(250);
       if(scroll.VerticalOffset<=0)throw new InvalidOperationException("Wheel scrolling did not change the view");
       if(Native.GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this))<96)throw new InvalidOperationException("Invalid DPI");
       for(int i=1;i<L.Codes.Length;++i){L.PreviewLanguage=i;BuildContent();if(body.FlowDirection!=L.Direction)throw new InvalidOperationException("Language direction mismatch");}
       L.PreviewLanguage=null;BuildContent();
       var sliders=body.Children.OfType<StackPanel>().SelectMany(p=>p.Children.OfType<Slider>()).ToArray();
       if(!sliders.Any(s=>(string?)s.Tag=="SummonDistanceDip"&&s.Maximum==3000))throw new InvalidOperationException("Summon range mismatch");
       if(!sliders.Any(s=>(string?)s.Tag=="SensitivityTenths"&&s.StepFrequency==.1&&s.Minimum==.1&&s.Maximum==4))throw new InvalidOperationException("Sensitivity slider mismatch");
       if(toggles.ContainsKey("ToolNotice"))throw new InvalidOperationException("Obsolete tool notice");
       if(!sliders.Any(s=>(string?)s.Tag=="IdleDelayTenths"&&s.StepFrequency==.1))throw new InvalidOperationException("Idle delay step mismatch");
      }
      int flag=Array.IndexOf(args,"--startup-check");if(flag>=0&&flag+1<args.Length)File.WriteAllText(args[flag+1],"READY");
     }catch(Exception ex){SettingsApp.Error(ex);Environment.ExitCode=12;}finally{Close();}
    };startupTimer.Start();
   };
  }
  if(Environment.GetCommandLineArgs().Contains("--render-preview")) {
   ((FrameworkElement)Content).Loaded+=async(_,_)=>{
    try {
     await Task.Delay(350);
     var bitmap=new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();await bitmap.RenderAsync((FrameworkElement)Content);
     var buffer=await bitmap.GetPixelsAsync();byte[] pixels=new byte[buffer.Length];DataReader.FromBuffer(buffer).ReadBytes(pixels);
     using var file=File.Create(System.IO.Path.Combine(AppContext.BaseDirectory,"Settings-preview.png"));
     var encoder=await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId,file.AsRandomAccessStream());
     encoder.SetPixelData(BitmapPixelFormat.Bgra8,BitmapAlphaMode.Premultiplied,(uint)bitmap.PixelWidth,(uint)bitmap.PixelHeight,96,96,pixels);await encoder.FlushAsync();
    }catch(Exception error){SettingsApp.Error(error);}finally{Close();}
   };
  }
 }
 void BuildContent() {
  loading=true;toggles.Clear();body.Children.Clear();body.FlowDirection=L.Direction;
  body.Children.Add(new TextBlock{Text="CirDial",FontSize=28,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold});
  var language=new ComboBox{Header=L.T("Language"),HorizontalAlignment=HorizontalAlignment.Stretch,MinHeight=48};
  foreach(var name in L.Names)language.Items.Add(name);language.SelectedIndex=L.Selected;
  language.SelectionChanged+=(_,_)=>{if(loading)return;Flush();Native.Write("Language",language.SelectedIndex);BuildContent();};body.Children.Add(language);
  body.Children.Add(new TextBlock{Text=L.T("Help"),TextWrapping=TextWrapping.Wrap,Opacity=.8,FontSize=13});

  var actions=new StackPanel{Orientation=Orientation.Horizontal,Spacing=12};
  actions.Children.Add(Button("ShowHide",()=>Native.Command(1)));actions.Children.Add(Button("Exit",()=>{Flush();Native.Command(2);Close();}));body.Children.Add(actions);
  AddToggle("Mouse","MouseHelp","MouseEnabled",0);AddToggle("Screen",null,"OnScreen",0);
  body.Children.Add(new TextBlock{Text=L.T("GesturePaid"),FontSize=18,TextWrapping=TextWrapping.Wrap});
  storeStatus=new TextBlock{Text=L.T("StorePending"),TextWrapping=TextWrapping.Wrap,Opacity=.7};body.Children.Add(storeStatus);
  buyButton=Button("Buy",async()=>{try{buyButton.IsEnabled=false;await store.Purchase(WinRT.Interop.WindowNative.GetWindowHandle(this));await RefreshStore();}catch(Exception ex){SettingsApp.Error(ex);storeStatus.Text=L.T("StoreError");}});
  body.Children.Add(buyButton);body.Children.Add(Button("Restore",async()=>await RefreshStore()));
  AddToggle("Summon","SummonHelp","GestureSummon",0);
  AddToggle("ShakeHide","ShakeHelp","ShakeHide",0);
  var fingers=new ComboBox{Header=L.T("SummonFingers"),HorizontalAlignment=HorizontalAlignment.Stretch,MinHeight=48};
  fingers.Items.Add(L.T("TwoFingers"));fingers.Items.Add(L.T("ThreeFingers"));fingers.SelectedIndex=Math.Clamp(Native.Read("SummonFingers",3)-2,0,1);
  fingers.SelectionChanged+=(_,_)=>Save("SummonFingers",fingers.SelectedIndex+2);body.Children.Add(fingers);
  AddSlider("SummonDistance","SummonDistanceDip",800,40,3000,1);
  var size=AddSlider("Diameter","DiameterDip",164,96,360,1);
  body.Children.Add(Button("ResetSize",()=>{size.Value=164;Save("DiameterDip",164);Flush();}));
  AddSlider("IdleDelay","IdleDelayTenths",Native.Read("IdleSeconds",15)*10,0,600,.1,10);
  AddSlider("IdleAlpha","IdleTransparency",800,0,100,.1,10);
  AddToggle("Indicator",null,"IndicatorEnabled",1);
  AddToggle("Reverse",null,"Reverse",0);
  AddSlider("Speed","SensitivityTenths",new[]{5,10,20,40}[Math.Clamp(Native.Read("Speed",1),0,3)],.1,4,.1,10);
  body.Children.Add(new TextBlock{Text=L.T("ColorsFree"),FontSize=18,TextWrapping=TextWrapping.Wrap});
  AddAppearance("IndicatorColor","IndicatorColor",0xf5f5f5);
  AddAppearance("InnerColor","InnerColor",0);AddSlider("InnerAlpha","InnerTransparency",0,0,100,.1,10);
  AddAppearance("OuterColor","OuterColor",0x202020);AddSlider("OuterAlpha","OuterTransparency",0,0,100,.1,10);
  body.Children.Add(Button("ResetLook",()=>{Flush();Native.Write("IndicatorColor",0xf5f5f5);Native.Write("InnerColor",0);Native.Write("OuterColor",0x202020);Native.Write("InnerTransparency",0);Native.Write("OuterTransparency",0);BuildContent();Native.Command(0);}));
  body.Children.Add(Button("Licenses",async()=>{
   string file=System.IO.Path.Combine(AppContext.BaseDirectory,"Licenses","THIRD-PARTY-NOTICES.md");
   string text=File.Exists(file)?File.ReadAllText(file):"License files are missing from this package.";
   var notice=new TextBlock{Text=text,TextWrapping=TextWrapping.Wrap,IsTextSelectionEnabled=true};
   var documents=new ComboBox{HorizontalAlignment=HorizontalAlignment.Stretch};var directory=System.IO.Path.GetDirectoryName(file)!;
   var paths=Directory.Exists(directory)?Directory.GetFiles(directory,"*",SearchOption.AllDirectories).Where(p=>!p.EndsWith(".json")).ToArray():Array.Empty<string>();
   foreach(var path in paths)documents.Items.Add(System.IO.Path.GetRelativePath(directory,path));
   documents.SelectionChanged+=(_,_)=>{if(documents.SelectedIndex>=0)notice.Text=File.ReadAllText(paths[documents.SelectedIndex]);};
   var panel=new StackPanel{Spacing=12};panel.Children.Add(documents);panel.Children.Add(new ScrollViewer{MaxHeight=370,Content=notice});
   var dialog=new ContentDialog{Title=L.T("Licenses"),CloseButtonText=L.T("Close"),XamlRoot=body.XamlRoot,Content=panel};await dialog.ShowAsync();
  }));
  body.Children.Add(new TextBlock{Text=L.T("Version")+" 0.5.0 Preview\n"+L.T("Author")+"：夜冷星灼\n"+L.T("Contact")+"：Xingzhuo1989@live.cn",TextWrapping=TextWrapping.Wrap,IsTextSelectionEnabled=true,FontSize=13,Opacity=.7});
  scroll.Content=body;Content=scroll;body.UseLayoutRounding=true;
  if(!wheelAttached){body.AddHandler(UIElement.PointerWheelChangedEvent,new PointerEventHandler((_,e)=>{var point=e.GetCurrentPoint(body);if(point.Properties.IsHorizontalMouseWheel)return;ScrollWheel(point.Properties.MouseWheelDelta);e.Handled=true;}),true);wheelAttached=true;}
  loading=false;UpdateStoreControls();
 }
 async Task RefreshStore() {
  try {await store.Refresh(WinRT.Interop.WindowNative.GetWindowHandle(this));UpdateStoreControls();Native.Command(6);}
  catch(Exception ex){SettingsApp.Error(ex);UpdateStoreControls();storeStatus.Text=L.T("StoreError");}
 }
 void UpdateStoreControls() {
  if(toggles.TryGetValue("GestureSummon",out var summon)) {
   summon.IsEnabled=store.Owned;
   bool previous=loading;loading=true;
   try {summon.IsOn=store.Owned&&Native.Read("GestureSummon",0)!=0;} finally {loading=previous;}
  }
  buyButton.IsEnabled=store.Available&&!store.Owned;
  buyButton.Content=L.T("Buy")+(store.Available?" · "+store.Price:"");
  storeStatus.Text=store.Owned?L.T("Owned"):store.Available?"":L.T("StorePending");
 }
 void ScrollWheel(int delta) => scroll.ChangeView(null,Math.Clamp(scroll.VerticalOffset-delta/120.0*72,0,scroll.ScrollableHeight),null,true);
 Slider AddSlider(string title,string key,int fallback,double minimum,double maximum,double step,int multiplier=1) {
  var panel=new StackPanel{Spacing=4};panel.Children.Add(new TextBlock{Text=L.T(title),TextWrapping=TextWrapping.Wrap});
  var value=new TextBlock{FlowDirection=FlowDirection.LeftToRight,HorizontalAlignment=HorizontalAlignment.Right};
  var slider=new Slider{Tag=key,Minimum=minimum,Maximum=maximum,StepFrequency=step,SmallChange=step,LargeChange=step*10,Value=Math.Clamp(Native.Read(key,fallback)/(double)multiplier,minimum,maximum),MinHeight=48};
  value.Text=slider.Value.ToString(step<1?"F1":"F0");
  slider.ValueChanged+=(_,e)=>{value.Text=e.NewValue.ToString(step<1?"F1":"F0");Save(key,(int)Math.Round(e.NewValue*multiplier));};
  panel.Children.Add(value);panel.Children.Add(slider);body.Children.Add(panel);return slider;
 }
 Button Button(string text,Action click) {var b=new Button{Content=L.T(text),MinHeight=48,Padding=new Thickness(18,10,18,10)};b.Click+=(_,_)=>click();return b;}
 void AddToggle(string title,string? detail,string key,int fallback) {
  var t=new ToggleSwitch{Header=L.T(title),OnContent=L.T("On"),OffContent=L.T("Off"),IsOn=Native.Read(key,fallback)!=0,MinHeight=64};t.Toggled+=(_,_)=>{if(loading)return;if(key=="GestureSummon"&&!store.Owned){UpdateStoreControls();return;}Save(key,t.IsOn?1:0);if(key=="GestureSummon"||key=="OnScreen")Flush();};toggles[key]=t;body.Children.Add(t);
  if(detail!=null)body.Children.Add(new TextBlock{Text=L.T(detail),Opacity=.65,FontSize=12,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,-12,0,0)});
 }
 static Windows.UI.Color Color(int rgb)=>Windows.UI.Color.FromArgb(255,(byte)(rgb>>16),(byte)(rgb>>8),(byte)rgb);
 void AddAppearance(string title,string colorKey,int defaultColor) {
  var section=new Expander{Header=L.T(title),HorizontalAlignment=HorizontalAlignment.Stretch};
  section.Expanding+=(_,_)=>{
   if(section.Content!=null)return;
   var picker=new ColorPicker{Color=Color(Native.Read(colorKey,defaultColor)),IsAlphaEnabled=false,IsAlphaSliderVisible=false,IsAlphaTextInputVisible=false,MinWidth=240,HorizontalAlignment=HorizontalAlignment.Stretch};
   picker.ColorChanged+=(_,e)=>Save(colorKey,(e.NewColor.R<<16)|(e.NewColor.G<<8)|e.NewColor.B);section.Content=picker;
  };
  body.Children.Add(section);
 }
 void SyncToggles(Func<string,int,int>? read=null){
  read??=Native.Read;
  loading=true;
  try{foreach(var key in new[]{"OnScreen","GestureSummon"})if(!pending.ContainsKey(key)&&toggles.TryGetValue(key,out var toggle))toggle.IsOn=(key!="GestureSummon"||store.Owned)&&read(key,0)!=0;}
  finally{loading=false;}
 }
 void Save(string key,int value) {if(loading)return;pending[key]=value;debounce.Stop();debounce.Start();}
 void Flush(){debounce.Stop();if(pending.Count==0)return;foreach(var pair in pending)Native.Write(pair.Key,pair.Value);pending.Clear();Native.Command(0);}
}

sealed class ExitWindow:Window {
 bool activated;
 public ExitWindow() {
  Title="CirDial";
  var panel=new StackPanel{Padding=new Thickness(8),Spacing=6,FlowDirection=L.Direction};
  void Add(string label,nuint command){var button=new Button{Content=L.T(label),MinHeight=48,HorizontalAlignment=HorizontalAlignment.Stretch};button.Click+=(_,_)=>{Native.Command(command);Close();};panel.Children.Add(button);}
  Add("Show",4);Add("Hide",5);
  bool syncing=false;
  var screen=new ToggleSwitch{Header=L.T("Screen"),IsOn=Native.Read("OnScreen",0)!=0,OnContent=L.T("On"),OffContent=L.T("Off")};screen.Toggled+=(_,_)=>{if(syncing)return;Native.Write("OnScreen",screen.IsOn?1:0);Native.Command(0);};panel.Children.Add(screen);
  var sync=DispatcherQueue.CreateTimer();sync.Interval=TimeSpan.FromMilliseconds(1000);sync.Tick+=(_,_)=>{syncing=true;screen.IsOn=Native.Read("OnScreen",0)!=0;syncing=false;};sync.Start();Closed+=(_,_)=>sync.Stop();
  Add("Exit",2);Content=panel;SystemTheme.Follow(panel);
  var presenter=Microsoft.UI.Windowing.OverlappedPresenter.Create();presenter.SetBorderAndTitleBar(false,false);presenter.IsResizable=false;presenter.IsAlwaysOnTop=true;AppWindow.SetPresenter(presenter);
  uint border=0xfffffffe;Native.DwmSetWindowAttribute(WinRT.Interop.WindowNative.GetWindowHandle(this),34,ref border,4);
  var area=Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id,Microsoft.UI.Windowing.DisplayAreaFallback.Primary).WorkArea;
  double scale=Native.GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this))/96.0;
  int w=(int)(300*scale),h=(int)(280*scale);AppWindow.MoveAndResize(new RectInt32(area.X+area.Width-w-12,area.Y+area.Height-h-12,w,h));
  Activated+=(_,e)=>{if(e.WindowActivationState!=WindowActivationState.Deactivated)activated=true;else if(activated)Close();};
  Closed+=(_,_)=>Application.Current.Exit();
 }
}

