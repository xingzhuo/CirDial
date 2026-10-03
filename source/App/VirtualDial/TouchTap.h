#pragma once
#include <map>
#include <cmath>
namespace dial {
class TouchTap {
 struct Finger {POINT start,last;};
 std::map<UINT32,Finger> active,seen;
 ULONGLONG started=0;
 bool invalid=false,matched=false,inside=true,anyInside=false;
 unsigned required=3;
 double spacing=800,scale=1;
 POINT center={};
 public:
 struct Result {bool fired=false,allInside=false;POINT center={};bool anyInside=false;};
 void Reset(){active.clear();seen.clear();invalid=matched=false;inside=true;anyInside=false;started=0;}
 void Configure(unsigned count,double distance,double dpiScale){Reset();required=count;spacing=distance;scale=dpiScale;}
 Result Feed(UINT32 id,POINT p,bool down,bool up,bool canceled,bool inDial,ULONGLONG now){
  Result result;
  if(down&&!active.empty()&&now-started>1500)Reset();
  if(down){
   if(active.empty()){Reset();started=now;}
   if(seen.count(id)||now-started>220)invalid=true;
   active[id]={p,p};seen[id]={p,p};inside=inside&&inDial;anyInside=anyInside||inDial;
  }
  auto it=active.find(id);if(it==active.end())return result;
  it->second.last=p;seen[id].last=p;
  if(canceled||now-started>500||seen.size()>required||std::hypot(double(p.x-it->second.start.x),double(p.y-it->second.start.y))>14*scale)invalid=true;
  for(auto& a:seen)for(auto& b:seen)if(std::hypot(double(a.second.start.x-b.second.start.x),double(a.second.start.y-b.second.start.y))>spacing*scale)invalid=true;
  if(active.size()==required&&!invalid){matched=true;long long x=0,y=0;for(auto& f:active){x+=f.second.start.x;y+=f.second.start.y;}center={LONG(x/required),LONG(y/required)};}
  if(up||canceled){active.erase(id);if(active.empty()){result={matched&&!invalid,inside,center,anyInside};Reset();}}
  return result;
 }
 static bool SelfTest(){
  TouchTap t;t.Configure(2,120,1);
  t.Feed(1,{100,100},true,false,false,true,100);t.Feed(2,{130,100},true,false,false,true,130);
  t.Feed(1,{100,100},false,true,false,true,180);auto r=t.Feed(2,{130,100},false,true,false,true,200);
  if(!r.fired||!r.allInside||r.center.x!=115)return false;
  t.Feed(1,{100,100},true,false,false,true,300);t.Feed(2,{300,100},true,false,false,true,320);
  t.Feed(1,{100,100},false,true,false,true,350);if(t.Feed(2,{300,100},false,true,false,true,360).fired)return false;
  t.Feed(1,{100,100},true,false,false,true,1000);t.Feed(2,{130,100},true,false,false,true,1020);
  t.Feed(1,{140,100},false,false,false,true,1050);t.Feed(1,{140,100},false,true,false,true,1060);if(t.Feed(2,{130,100},false,true,false,true,1070).fired)return false;
  t.Feed(1,{100,100},true,false,false,true,1100);t.Feed(2,{130,100},true,false,false,true,1120);
  t.Feed(1,{100,100},false,true,true,true,1150);if(t.Feed(2,{130,100},false,true,false,true,1170).fired)return false;
  t.Configure(3,120,1);for(UINT32 i=1;i<=3;++i)t.Feed(i,{LONG(100+i*10),100},true,false,false,i!=3,400+i*10);
  for(UINT32 i=1;i<=3;++i)r=t.Feed(i,{LONG(100+i*10),100},false,true,false,true,500+i*10);
  if(!r.fired||r.allInside||!r.anyInside)return false;
  t.Configure(3,3000,1);
  for(UINT32 i=1;i<=3;++i)t.Feed(i,{LONG(i*900),100},true,false,false,false,2000+i*10);
  for(UINT32 i=1;i<=3;++i)r=t.Feed(i,{LONG(i*900),100},false,true,false,false,2100+i*10);
  if(!r.fired||r.center.x!=1800||r.anyInside)return false;
  t.Configure(2,3000,1);
  t.Feed(1,{0,100},true,false,false,false,3000);t.Feed(2,{3001,100},true,false,false,false,3010);
  t.Feed(1,{0,100},false,true,false,false,3100);
  return !t.Feed(2,{3001,100},false,true,false,false,3110).fired;
 }
};
}
