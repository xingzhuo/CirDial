#pragma once
#include <cmath>
namespace dial {
class Shake {
 POINT anchor={};ULONGLONG start=0;int direction=0,turns=0;double travel=0;bool ready=false;
 public:
 void Reset(){ready=false;direction=turns=0;travel=0;start=0;}
 bool Move(POINT p,ULONGLONG now,double scale){
  if(!ready||now-start>900){Reset();ready=true;anchor=p;start=now;return false;}
  double dx=p.x-anchor.x,dy=p.y-anchor.y;
  if(std::hypot(dx,dy)<18*scale)return false;
  int next=std::abs(dx)>=std::abs(dy)?(dx>0?1:-1):(dy>0?2:-2);
  travel+=std::hypot(dx,dy);anchor=p;
  if(direction&&next==-direction)++turns;
  else if(direction&&std::abs(next)!=std::abs(direction)){turns=0;travel=0;}
  direction=next;
  if(turns>=3&&travel>=120*scale){Reset();return true;}return false;
 }
 static bool SelfTest(){Shake s;s.Move({0,0},100,1);s.Move({40,0},180,1);s.Move({0,0},260,1);s.Move({40,0},340,1);if(!s.Move({0,0},420,1))return false;
  for(int i=0;i<8;++i)if(s.Move({i*40,0},1000+i*80,1))return false;return true;}
};
}
