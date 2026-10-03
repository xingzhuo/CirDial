using System.Globalization;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.UI.ViewManagement;

namespace CirDial.Settings;
static class L {
 public static readonly string[] Codes={"auto","zh-Hans","zh-Hant","en","ja","ko","de","fr","es","pt","ru","ar","hi"};
 public static readonly string[] Names={"🌐 System / 系统","简体中文","繁體中文","English","日本語","한국어","Deutsch","Français","Español","Português","Русский","العربية","हिन्दी"};
 static readonly Dictionary<string,string[]> Strings=JsonSerializer.Deserialize<Dictionary<string,string[]>>(File.ReadAllText(System.IO.Path.Combine(AppContext.BaseDirectory,"languages.json")))!;
 internal static int? PreviewLanguage;
 public static int Selected=>PreviewLanguage??Math.Clamp(Native.Read("Language",0),0,Codes.Length-1);
 public static int Effective {
  get {if(Selected>0)return Selected;string name=CultureInfo.CurrentUICulture.Name;
   if(name.StartsWith("zh",StringComparison.OrdinalIgnoreCase))return name.Contains("TW")||name.Contains("HK")||name.Contains("Hant")?2:1;
   int i=Array.FindIndex(Codes,c=>c!="auto"&&name.StartsWith(c,StringComparison.OrdinalIgnoreCase));return i>0?i:3;}
 }
 public static string T(string key)=>Strings.TryGetValue(key,out var values)?values[Effective-1]:key;
 public static FlowDirection Direction=>Effective==11?FlowDirection.RightToLeft:FlowDirection.LeftToRight;
}
static class SystemTheme {
 public static void Follow(FrameworkElement root) {
  var settings=new UISettings();
  void Update(){var color=settings.GetColorValue(UIColorType.Background);root.RequestedTheme=color.R+color.G+color.B<384?ElementTheme.Dark:ElementTheme.Light;root.BackgroundIfPanel(color);}
  Update();settings.ColorValuesChanged+=(_,_)=>root.DispatcherQueue.TryEnqueue(Update);
  root.Tag=settings;
 }
 static void BackgroundIfPanel(this FrameworkElement root,Windows.UI.Color color) {
  var brush=new SolidColorBrush(color);
  if(root is Microsoft.UI.Xaml.Controls.Panel panel)panel.Background=brush;
  else if(root is Microsoft.UI.Xaml.Controls.Control control)control.Background=brush;
 }
}
