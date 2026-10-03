#pragma once
#include <algorithm>
#include <cmath>
namespace dial {
inline double Ease(double t) { t=std::clamp(t,0.0,1.0); return 1-std::pow(1-t,3); }
struct Tween {
 double value=0,from=0,to=0; unsigned long long started=0; unsigned duration=180;
 void Set(double next,unsigned long long now,unsigned ms=180) { if(next==to)return; from=value;to=next;started=now;duration=ms; }
 bool Tick(unsigned long long now) { double old=value; value=from+(to-from)*Ease(double(now-started)/duration); return std::abs(old-value)>.0001; }
 bool Active(unsigned long long now)const{return now-started<duration&&value!=to;}
};
// Two-stage concentric reveal, with continuous reversals and shared opacity.
struct DialReveal {
 double value=1,to=1,outer=1,inner=.39,opacity=1;
 unsigned long long started=0;unsigned duration=420;
 double startOuter=1,startInner=.39,startOpacity=1,startValue=1;bool running=false;
 DialReveal(double v=1,double=1,double target=1):value(v),to(target),outer(v),inner(.39*v),opacity(v){}
 static double Smooth(double t){t=std::clamp(t,0.0,1.0);return t*t*(3-2*t);}
 static double Spring(double t){t=std::clamp(t,0.0,1.0);double u=t-1;return 1+2.15*u*u*u+1.15*u*u;}
 bool Active(unsigned long long now)const{return running&&now-started<duration;}
 bool Tick(unsigned long long now){
  if(!running)return false;
  double t=std::clamp(double(now-started)/duration,0.0,1.0);
  auto mix=[](double a,double b,double x){return a+(b-a)*x;};
  value=mix(startValue,to,t);
  if(to==1){
   if(t<.46){double u=Smooth(t/.46);outer=mix(startOuter,1,u);inner=mix(startInner,1,u);}
   else{double u=(t-.46)/.54;outer=1+.035*std::sin(u*6.283185307179586)*std::sin(u*3.141592653589793);inner=mix(1,.39,Spring(u));}
   opacity=mix(startOpacity,1,Smooth(std::min(1.0,t/.55)));
  }else{
   if(t<.5){double u=Smooth(t/.5);outer=mix(startOuter,.39,u);inner=mix(startInner,.39,u);opacity=startOpacity;}
   else{double u=Smooth((t-.5)/.5);outer=inner=.39*(1-u);opacity=startOpacity*(1-u);}
  }
  inner=std::clamp(inner,0.0,outer);
  if(t>=1){running=false;outer=to;inner=.39*to;opacity=to;value=to;}
  return true;
 }
 void Set(double target,unsigned long long now,unsigned ms=420){
  if(target==to)return;Tick(now);startOuter=outer;startInner=inner;startOpacity=opacity;startValue=value;to=target;started=now;duration=ms;running=true;
 }
 static bool SelfTest(){
  DialReveal a(0,0,0);a.Set(1,100,1000);a.Tick(330);
  if(a.inner<=0||a.inner!=a.outer||a.opacity<=0)return false;
  a.Tick(560);if(std::abs(a.inner-1)>.001)return false;
  a.Tick(800);if(a.inner>=a.outer)return false;
  a.Tick(1100);if(a.inner!=.39||a.outer!=1||a.opacity!=1)return false;
  a.Set(0,1200,1000);a.Tick(1700);if(a.inner!=.39||a.outer!=.39)return false;
  a.Tick(1950);if(a.inner!=a.outer||a.opacity>=1)return false;
  double before=a.outer;a.Set(1,1950,1000);if(a.outer!=before)return false;
  a.Tick(2950);a.Set(0,3000,1000);a.Tick(4000);return a.outer==0&&a.inner==0&&a.opacity==0&&!a.Active(4000);
 }
};
inline unsigned char Alpha(int transparency) { return static_cast<unsigned char>(std::lround(255*(1-std::clamp(transparency,0,1000)/1000.0))); }
inline int Edge(int value,int lo,int hi,int size){return std::clamp(value,lo,std::max(lo,hi-size));}
}
