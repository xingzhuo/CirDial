#pragma once
#include <map>
#include <vector>
namespace dial {
// Physical contacts are forwarded immediately; recognition never delays delivery.
class TouchRelay {
 std::map<UINT32,POINTER_TOUCH_INFO> active;
 UINT32 lastFrame=0; bool hasFrame=false;
 using Sender=BOOL(WINAPI*)(UINT32,const POINTER_TOUCH_INFO*);
 Sender sender=InjectTouchInput;
 bool Inject(const std::vector<POINTER_TOUCH_INFO>& frame) {
  if(frame.empty())return true;
  for(int retry=0;retry<4;++retry){
   if(sender(UINT32(frame.size()),frame.data()))return true;
   if(GetLastError()!=ERROR_NOT_READY)return false;
   Sleep(1);
  }
  return false;
 }
 public:
 bool Heartbeat(){std::vector<POINTER_TOUCH_INFO> frame;for(auto& e:active){auto p=e.second;p.pointerInfo.pointerFlags=POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|POINTER_FLAG_UPDATE;frame.push_back(p);}return Inject(frame);}
 static bool SelfTest(){
  TouchRelay t;t.sender=[](UINT32 count,const POINTER_TOUCH_INFO*)->BOOL{return count>0;};
  POINTER_TOUCH_INFO p={};p.pointerInfo.pointerId=99;p.pointerInfo.pointerFlags=POINTER_FLAG_DOWN;p.pointerInfo.ptPixelLocation={100,200};
  if(!t.ForwardFrame({p})||t.active.size()!=1||t.active[99].pointerInfo.pointerId!=1)return false;
  auto q=p;q.pointerInfo.pointerId=100;q.pointerInfo.ptPixelLocation={120,200};
  p.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE;
  if(!t.ForwardFrame({p,q})||t.active.size()!=2||t.active[100].pointerInfo.pointerId!=2)return false;
  q.pointerInfo.pointerFlags=POINTER_FLAG_UP;q.pointerInfo.ptPixelLocation={150,250};
  if(!t.ForwardFrame({q})||t.active.size()!=1)return false;
  t.Cancel();return t.active.empty();
 }
 void Cancel(){std::vector<POINTER_TOUCH_INFO> frame;for(auto& entry:active){auto p=entry.second;p.pointerInfo.pointerFlags=POINTER_FLAG_UP|POINTER_FLAG_CANCELED;frame.push_back(p);}Inject(frame);active.clear();hasFrame=false;}
 bool Forward(UINT32 id,std::vector<POINTER_TOUCH_INFO>& physical){
  POINTER_INFO info={};if(!GetPointerInfo(id,&info))return false;
  if(hasFrame&&lastFrame==info.frameId)return true;
  UINT32 count=0;GetPointerFrameTouchInfo(id,&count,nullptr);
  if(!count||count>MAX_TOUCH_COUNT)return false;
  physical.resize(count);if(!GetPointerFrameTouchInfo(id,&count,physical.data()))return false;
  physical.resize(count);lastFrame=info.frameId;hasFrame=true;
  return ForwardFrame(physical);
 }
 private:
 bool ForwardFrame(const std::vector<POINTER_TOUCH_INFO>& physical){
  auto next=active;for(auto& e:next)e.second.pointerInfo.pointerFlags=POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|POINTER_FLAG_UPDATE;
  std::vector<UINT32> ended;
  for(auto& source:physical){auto& pi=source.pointerInfo;bool down=(pi.pointerFlags&POINTER_FLAG_DOWN)!=0;bool up=(pi.pointerFlags&(POINTER_FLAG_UP|POINTER_FLAG_CANCELED))!=0;
   if(!next.count(pi.pointerId)&&!down)continue;
   POINTER_TOUCH_INFO dest={};dest.pointerInfo.pointerType=PT_TOUCH;
   if(next.count(pi.pointerId))dest=next[pi.pointerId];
   else {UINT32 n=1;for(;n<=MAX_TOUCH_COUNT;++n){bool used=false;for(auto& e:next)used|=e.second.pointerInfo.pointerId==n;if(!used)break;}if(n>MAX_TOUCH_COUNT)return false;dest.pointerInfo.pointerId=n;}
   // UP must retain the last injected pixel position (Windows injection contract).
   if(!up)dest.pointerInfo.ptPixelLocation=pi.ptPixelLocation;
   dest.pointerInfo.pointerFlags=up?POINTER_FLAG_UP:(POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|(down?POINTER_FLAG_DOWN:POINTER_FLAG_UPDATE));
   if(pi.pointerFlags&POINTER_FLAG_CANCELED)dest.pointerInfo.pointerFlags|=POINTER_FLAG_CANCELED;
   dest.touchMask=TOUCH_MASK_CONTACTAREA;auto p=dest.pointerInfo.ptPixelLocation;dest.rcContact={p.x-2,p.y-2,p.x+2,p.y+2};
   next[pi.pointerId]=dest;if(up)ended.push_back(pi.pointerId);
  }
  std::vector<POINTER_TOUCH_INFO> frame;for(auto& e:next)frame.push_back(e.second);
  if(!Inject(frame))return false;
  for(auto endedId:ended)next.erase(endedId);active=std::move(next);return true;
 }
};
}
