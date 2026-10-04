// Garibaldka (Russian Bank / crapette) for Windows - player vs computer.
// C++17 / Win32 / Direct2D + DirectWrite + WIC (graphics), DirectSound (audio).
// Card images, sounds, renderer, fireworks and sound system come from the
// "Pasjans Dziadkowy" project (MIT, see LICENSE).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include "game.h"
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "anim.h"
#include "layout.h"
#include "card_images_d2d.h"
#include "renderer_d2d.h"
#include "sound.h"

// ============================================================================
// Globals
// ============================================================================
static HWND                   g_hwnd=nullptr;
static ID2D1Factory*          g_d2d=nullptr;
static IDWriteFactory*        g_dw=nullptr;
static IWICImagingFactory*    g_wic=nullptr;
static ID2D1HwndRenderTarget* g_rt=nullptr;
static RendererD2D            g_ren;
static Layout                 g_lay;

static Game          g_game;
static AIContext     g_ctx;
static FireworkSystem g_fw;
static int   g_level=1;            // 0 easy, 1 normal, 2 hard
static bool  g_muted=false;
static int   g_volPct=100;
bool isSoundMuted(){ return g_muted; }

static const wchar_t* LEVEL_NAMES[]={L"Łatwy",L"Normalny",L"Trudny"};
static const float TB=58.f, SB=28.f;     // toolbar / status bar heights

// ---------------------------------------------------------------------------
static double nowSec(){
   static LARGE_INTEGER f={}; if(!f.QuadPart) QueryPerformanceFrequency(&f);
   LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart/(double)f.QuadPart;
}

// ============================================================================
// Geometry
// ============================================================================
struct Geo{ float w=1000,h=700,cw=90,ch=126,gap=10,rg=14,x0=0,yAI=0,yF=0,yT=0,yP=0,tabH=0,fan=30; } G;
static float slotX(int i){ return G.x0+i*(G.cw+G.gap); }
static float rowY(int p){ return p==1?G.yAI:G.yP; }
static void slotPos(int id,float& x,float& y){
   switch(ptype(id)){
   case PT_RES:    x=slotX(0); y=rowY(pidx(id)); break;
   case PT_HAND:   x=slotX(3); y=rowY(pidx(id)); break;
   case PT_TURNED: x=slotX(4); y=rowY(pidx(id)); break;
   case PT_WASTE:  x=slotX(6); y=rowY(pidx(id)); break;
   case PT_TAB:    x=slotX(pidx(id)); y=G.yT; break;
   default:        x=slotX(pidx(id)); y=G.yF; break;
   }
}
static void computeGeo(float w,float h){
   G.w=w; G.h=h;
   float availH=h-TB-SB;
   G.gap=std::max(6.f,std::floor(w*0.011f));
   G.rg =std::max(8.f,std::floor(G.gap*1.4f));
   float cwW=(w-24.f-7*G.gap)/8.f;
   float chH=(availH-3*G.rg-16.f)/5.1f;
   float cw=std::min(std::min(cwW,chH/1.4f),150.f);
   cw=std::max(36.f,std::floor(cw));
   G.cw=cw; G.ch=std::floor(cw*1.4f);
   G.x0=std::floor((w-(8*G.cw+7*G.gap))/2.f);
   G.yAI=TB+8.f;
   G.yP =h-SB-8.f-G.ch;
   G.yF =G.yAI+G.ch+G.rg;
   G.yT =G.yF+G.ch+G.rg;
   G.tabH=G.yP-G.rg-G.yT; if(G.tabH<G.ch) G.tabH=G.ch;
   G.fan=G.ch*0.26f;
   g_lay.cardW=(int)G.cw; g_lay.cardH=(int)G.ch; g_lay.cornerR=std::max(3,(int)G.cw/12);
}

// ============================================================================
// Card visuals (position / flip tweening)
// ============================================================================
struct Vis{
   float x=0,y=0, sx=0,sy=0, tx=0,ty=0;
   double t0=0,dur=0;
   float flip=0,fsrc=0,ftgt=0; double ft0=0,fdur=0.22;
   int z=0, pile=-1, idx=0; bool placed=false;
   bool couple=false;   // the flip is part of the move: the card lifts and turns over in the middle of the flight
   float lift=0;        // 0..1, how high the card is lifted right now (it is drawn bigger)
};
static Vis  V[104];
static Card C[104];
struct RectF4{ float l,t,r,b; };
static RectF4 PR[NP];          // hit rectangle of each pile

static std::vector<int> g_dealTops;       // magazine top cards: they land face down and are turned over afterwards (together)
// Order of the deal: red (computer's) magazine, blue magazine, red columns, blue columns, red hand, blue hand.
static double dealDelay(const Card& c,int pileId,int idx){
   static const int start[6]={0,13,26,30,34,69};
   int red=(c.deck==1)?0:1, cat=0, k=0;
   switch(ptype(pileId)){
   case PT_RES:  cat=0+red; k=idx; break;
   case PT_TAB:  cat=2+red; k=(pileId-8)%4; break;
   case PT_HAND: cat=4+red; k=idx; break;
   default: break;
   }
   return (start[cat]+k)*0.012;
}
static void relayout(bool snap=false,bool dealing=false){
   double now=nowSec();
   std::vector<int> pendingFlips; double moveEnd=now;   // cards that turn over in place wait until the moving cards have landed
   auto place=[&](const Card& c,float tx,float ty,int z,int pileId,int idx){
      Vis& v=V[c.id]; C[c.id]=c; v.pile=pileId; v.idx=idx; v.z=z;
      float ftgt=c.up?1.f:0.f;
      if(dealing && ptype(pileId)==PT_RES && idx==(int)g_game.pile[pileId].size()-1){ ftgt=0.f; g_dealTops.push_back(c.id); }
      if(!v.placed||snap){
         v.x=v.sx=v.tx=tx; v.y=v.sy=v.ty=ty; v.t0=0; v.dur=0;
         v.flip=v.fsrc=v.ftgt=ftgt; v.ft0=0; v.placed=true; return;
      }
      bool moved=std::fabs(tx-v.tx)>0.5f||std::fabs(ty-v.ty)>0.5f;
      double delay=dealing?dealDelay(c,pileId,idx):0.0;
      if(moved){
         v.sx=v.x; v.sy=v.y; v.tx=tx; v.ty=ty;
         float dist=std::hypot(tx-v.x,ty-v.y);
         v.t0=now+delay; v.dur=dealing?0.45:0.22+std::min(0.30f,dist/2400.f);
         moveEnd=std::max(moveEnd,v.t0+v.dur);
         v.fsrc=v.flip; v.ftgt=ftgt; v.couple=(v.flip!=ftgt);      // a card that turns over while moving: lifts and turns in the middle part
      } else if(ftgt!=v.ftgt){
         v.fsrc=v.flip; v.ftgt=ftgt; v.couple=false; v.ft0=now; v.fdur=0.22;   // turns over in place (started later, see pendingFlips)
         pendingFlips.push_back(c.id);
      }
   };
   float fanX=G.cw*0.2f;
   // fanned stacks: the last up-cards of the trailing "up" run are spread sideways
   auto stackFan=[&](int id,float x0,float y0){
      auto& pl=g_game.pile[id]; int n=(int)pl.size();
      int fup=n; for(int k=n-1;k>=0&&pl[k].up;k--) fup=k;
      int upn=n-fup;
      for(int i=0;i<n;i++){
         int o=i-fup;
         int f=o<0?0:std::max(0,o-std::max(0,upn-3));
         float dy=pl[i].up?0.f:-(float)std::min(i,12)*0.5f;
         place(pl[i],x0+f*fanX,y0+dy,10+i,id,i);
      }
   };
   for(int p=0;p<2;p++){
      float x,y;
      slotPos(resId(p),x,y);    stackFan(resId(p),x,y);
      slotPos(wasteId(p),x,y);  stackFan(wasteId(p),x,y);
      slotPos(handId(p),x,y);
      auto& h=g_game.pile[handId(p)];
      for(int i=0;i<(int)h.size();i++) place(h[i],x,y-(float)(i/6)*0.8f,10+i,handId(p),i);
      slotPos(turnedId(p),x,y);
      auto& t=g_game.pile[turnedId(p)];
      for(int i=0;i<(int)t.size();i++) place(t[i],x,y,10+i,turnedId(p),i);
   }
   for(int j=0;j<NUM_FND;j++){
      float x,y; slotPos(fndId(j),x,y);
      auto& f=g_game.pile[fndId(j)];
      for(int i=0;i<(int)f.size();i++) place(f[i],x,y,10+i,fndId(j),i);
   }
   for(int j=0;j<NUM_TAB;j++){
      float x,y; slotPos(tabId(j),x,y);
      auto& t=g_game.pile[tabId(j)]; int n=(int)t.size();
      float off=n>1?std::min(G.fan,(G.tabH-G.ch)/(float)(n-1)):G.fan;
      for(int i=0;i<n;i++) place(t[i],x,y+i*off,10+i,tabId(j),i);
   }
   for(int id:pendingFlips) V[id].ft0=std::max(V[id].ft0,moveEnd);   // first the card moves, only then the next one turns over
   // hit rectangles
   for(int id=0;id<NP;id++){
      float x,y; slotPos(id,x,y);
      RectF4 r={x,y,x+G.cw,y+G.ch};
      for(auto& c:g_game.pile[id]){
         const Vis& v=V[c.id];
         r.l=std::min(r.l,v.tx); r.t=std::min(r.t,v.ty);
         r.r=std::max(r.r,v.tx+G.cw); r.b=std::max(r.b,v.ty+G.ch);
      }
      PR[id]=r;
   }
}
// where the top card of a pile is (or would be) drawn
static void topPos(int id,float& x,float& y){
   auto& pl=g_game.pile[id];
   if(pl.empty()) slotPos(id,x,y);
   else { const Vis& v=V[pl.back().id]; x=v.tx; y=v.ty; }
}

static bool g_dragging=false; static int g_dragCard=-1;
static std::vector<int>   g_dragIds;      // cards being dragged: one card, or a whole sequence of a column
static std::vector<float> g_dragRel;      // their y offsets relative to the first one
static std::vector<int>   g_dragTargets;  // columns a dragged sequence can be moved to (with single moves)
static bool isDragged(int id){ if(!g_dragging) return false; for(int x:g_dragIds) if(x==id) return true; return false; }
// Every flight accelerates in the first half and slows down in the second (ease in-out).
static float easeInOut(float p){ return p<0.5f ? 4.f*p*p*p : 1.f-std::pow(-2.f*p+2.f,3.f)/2.f; }
static void tickVis(double now){
   for(int id=0;id<104;id++){
      Vis& v=V[id]; if(!v.placed) continue;
      double tEnd=v.t0+v.dur;
      bool moving=v.dur>0 && now>=v.t0 && now<tEnd;
      float p=moving?(float)((now-v.t0)/v.dur):(now<v.t0?0.f:1.f);
      if(!isDragged(id)){
         if(now<v.t0){ v.x=v.sx; v.y=v.sy; }
         else if(moving){ float e=easeInOut(p); v.x=v.sx+(v.tx-v.sx)*e; v.y=v.sy+(v.ty-v.sy)*e; }
         else { v.x=v.tx; v.y=v.ty; }
      }
      v.lift=0;
      if(v.couple){                                        // flip tied to the flight: only in its middle part, with a lift
         if(now<v.t0) v.flip=v.fsrc;
         else if(now>=tEnd){ v.flip=v.ftgt; v.couple=false; }
         else {
            float q=std::min(1.f,std::max(0.f,(p-0.25f)/0.5f)), sm=q*q*(3.f-2.f*q);
            v.flip=v.fsrc+(v.ftgt-v.fsrc)*sm;
            v.lift=std::sin(3.14159265f*p);
         }
      } else if(now<v.ft0) v.flip=v.fsrc;
      else if(now>=v.ft0+v.fdur) v.flip=v.ftgt;            // exact end value (no float drift)
      else {
         float u=(float)((now-v.ft0)/v.fdur), sm=u*u*(3.f-2.f*u);
         v.flip=v.fsrc+(v.ftgt-v.fsrc)*sm;
      }
   }
}
static bool anyAnimating(double now){
   for(int id=0;id<104;id++){
      const Vis& v=V[id]; if(!v.placed) continue;
      if(now<v.t0+v.dur) return true;
      if(now<v.ft0+v.fdur && v.fsrc!=v.ftgt) return true;
   }
   return false;
}

// ============================================================================
// UI state
// ============================================================================
static struct { bool down=false,moved=false; int idx=-1; bool badRun=false; int pile=-1,cardId=-1; float offX=0,offY=0,dx=0,dy=0; int mx=0,my=0; } g_drag;
// Hint preview (as in Pasjans Dziadkowy): the card itself flies to its destination and back, no frame.
struct Preview{ bool active=false; int cardId=-1; float sx=0,sy=0,tx=0,ty=0; double t0=0; };
static Preview g_prev;
static const double PREV_CYCLE=1.5, PREV_TOTAL=1.5;      // the hint move is shown once
// Opening animation: the winning (older) magazine card lifts, spins twice and flashes.
struct StartAnim{ bool active=false; int cardId=-1; double t0=0; };
static StartAnim g_start;
static const double START_DUR=1.0, START_FLASH=0.2;
// Tie at the start (identical magazine cards): hand cards are turned over pair by pair until one decides.
struct RevealAnim{ bool active=false; std::vector<std::pair<int,int>> pairs; double t0=0; size_t launched=0; double hideAt=0; bool hidden=true; };
static RevealAnim g_rev;
static const double REV_STEP=0.75, REV_FLIGHT=0.5;
// The deal: one pile of both decks -> two piles (red, blue) -> cards fly to magazines, columns, hands -> magazine tops turn over.
struct DealAnim{ bool active=false; int stage=0; double tSplit=0,tDeal=0,tFlip=0; };
static DealAnim g_deal;
static std::wstring g_status; static bool g_statusErr=false; static double g_statusUntil=0;
static double g_dealUntil=0, g_aiAt=0, g_fwLast=0, g_overAt=0;
static bool   g_overShown=false, g_dirty=true;
static int    g_hoverBtn=-1;
static std::string g_log;               // move log (shown in the F9 dump)
static void logf(const char* who,int a=-1,int b=-1){ char t[96]; sprintf(t,"%.2f %s %d->%d turn=%d\n",nowSec()-0,who,a,b,g_game.turn); g_log+=t; }

enum { B_NEW,B_UNDO,B_HINT,B_DRAW,B_DISCARD,B_LEVEL,B_SOUND,B_RULES,B_COUNT };
struct Btn{ float x,y,w,h; };
static Btn g_btn[B_COUNT];

static void setStatus(const std::wstring& s,bool err=false,double secs=0){
   g_status=s; g_statusErr=err; g_statusUntil=secs>0?nowSec()+secs:0; g_dirty=true;
}
// A sequence being moved card by card (each single move is animated).
struct SeqPlan{ bool active=false; std::vector<Move> moves; size_t next=0; double at=0; };
static SeqPlan g_plan;
static bool humanTurn(){ return !g_game.over && g_game.turn==0 && nowSec()>=g_dealUntil && !g_plan.active; }

static void statusForTurn(){
   if(g_game.over) return;
   if(g_game.turn==1){ setStatus(L"Komputer gra…"); return; }
   if(!g_game.pile[turnedId(0)].empty()) setStatus(L"Zagraj dobraną kartę albo odrzuć ją na śmietnik (kończy turę).");
   else setStatus(L"Twój ruch: zagraj karty lub dobierz z talii.");
}

static std::wstring iniPath(){
   wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH);
   std::wstring s=b; size_t d=s.find_last_of(L'.'); if(d!=std::wstring::npos) s.resize(d);
   return s+L".ini";
}
static void saveSettings(){
   auto ini=iniPath(); wchar_t b[32];
   wsprintfW(b,L"%d",g_level); WritePrivateProfileStringW(L"Settings",L"Level",b,ini.c_str());
   wsprintfW(b,L"%d",g_muted?1:0); WritePrivateProfileStringW(L"Settings",L"Muted",b,ini.c_str());
   wsprintfW(b,L"%d",g_volPct); WritePrivateProfileStringW(L"Settings",L"Volume",b,ini.c_str());
   WINDOWPLACEMENT wp={sizeof(wp)};
   if(GetWindowPlacement(g_hwnd,&wp)){
      wchar_t w[128]; wsprintfW(w,L"%d,%d,%d,%d,%d",wp.rcNormalPosition.left,wp.rcNormalPosition.top,
         wp.rcNormalPosition.right,wp.rcNormalPosition.bottom,(int)wp.showCmd);
      WritePrivateProfileStringW(L"Window",L"Placement",w,ini.c_str());
   }
}
static void loadSettings(){
   auto ini=iniPath();
   g_level=(int)GetPrivateProfileIntW(L"Settings",L"Level",1,ini.c_str()); if(g_level<0||g_level>2) g_level=1;
   g_muted=GetPrivateProfileIntW(L"Settings",L"Muted",0,ini.c_str())!=0;
   g_volPct=(int)GetPrivateProfileIntW(L"Settings",L"Volume",100,ini.c_str()); g_volPct=std::max(0,std::min(100,g_volPct));
}
static void snd(const char* k){ playSound(k,g_volPct/100.f); }

// ============================================================================
// Game flow
// ============================================================================
static bool uiBusy(double now){ return anyAnimating(now)||g_prev.active||g_start.active||g_rev.active||g_deal.active; }

static void onGameOver(){
   if(g_overShown) return;
   g_overShown=true; g_overAt=nowSec(); g_prev.active=false; g_start.active=false;
   SoundSystem::instance().fadeOutAll(200);
   bool all=g_game.winner>=0 && g_game.remaining(g_game.winner)==0;      // false = decided by the turn limit
   if(g_game.winner==0){ snd("sukces"); g_fw.start((int)G.w,(int)G.h); g_fwLast=nowSec();
      setStatus(all?L"Wygrywasz! Pozbyłeś się wszystkich kart.":L"Wygrywasz! Po 400 turach masz mniej kart do zagrania."); }
   else if(g_game.winner==1){ snd("koniec");
      setStatus(all?L"Komputer pozbył się wszystkich kart. Przegrana.":L"Po 400 turach komputer ma mniej kart do zagrania. Przegrana."); }
   else { snd("koniec"); setStatus(L"Remis: nikt nie może już zagrać."); }
   g_dirty=true;
}
static void afterAnyMove(){
   g_prev.active=false;
   relayout(); g_dirty=true;
   if(g_game.over) onGameOver();
}

// ---------------------------------------------------------------------------
// Saving: the game is saved on exit and loaded again at the next start.
// ---------------------------------------------------------------------------
static std::wstring savePath(){
   wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH);
   std::wstring s=b; size_t d=s.find_last_of(L'.'); if(d!=std::wstring::npos) s.resize(d);
   return s+L".sav";
}
static void saveGame(){
   auto path=savePath();
   if(g_game.over){ DeleteFileW(path.c_str()); return; }   // a finished game is not kept
   std::string text=g_game.serialize();
   FILE* f=_wfopen(path.c_str(),L"wb"); if(!f) return;
   fwrite(text.data(),1,text.size(),f); fclose(f);
}
static bool readSavedGame(){
   FILE* f=_wfopen(savePath().c_str(),L"rb"); if(!f) return false;
   std::string text; char buf[4096]; size_t n;
   while((n=fread(buf,1,sizeof buf,f))>0) text.append(buf,n);
   fclose(f);
   Game tmp; if(!tmp.deserialize(text)||tmp.over) return false;
   g_game.deserialize(text);
   return true;
}
static void startAiTurn(){
   g_ctx=AIContext(); g_aiAt=nowSec()+0.75; statusForTurn();
}
// Sends a card on a flight (ease in-out); a card that turns over lifts and turns in the middle of it.
static void flyTo(int id,float tx,float ty,double t0,double dur,bool faceUp,int z){
   Vis& v=V[id]; v.sx=v.x; v.sy=v.y; v.tx=tx; v.ty=ty; v.t0=t0; v.dur=dur;
   v.fsrc=v.flip; v.ftgt=faceUp?1.f:0.f; v.couple=(v.fsrc!=v.ftgt); v.z=z;
}
static void revealTick(double now){
   if(!g_rev.active) return;
   while(g_rev.launched<g_rev.pairs.size() && now>=g_rev.t0+g_rev.launched*REV_STEP){
      size_t i=g_rev.launched++;
      for(int pl=0;pl<2;pl++){
         int id=pl==0?g_rev.pairs[i].first:g_rev.pairs[i].second;
         float x,y; slotPos(turnedId(pl),x,y); x+=(float)std::min<size_t>(i,4)*G.cw*0.2f;
         flyTo(id,x,y,now,REV_FLIGHT,true,300+(int)i*2+pl);
      }
      snd("click"); g_dirty=true;
   }
   if(!g_rev.hidden && now>=g_rev.hideAt){          // the winner is known: the shown cards go under the hands
      g_rev.hidden=true; g_rev.active=false;
      relayout(); snd("click"); g_dirty=true;
   }
}
// Stages of the deal (see DealAnim).
static void dealTick(double now){
   if(!g_deal.active) return;
   float cx=G.x0+3.5f*(G.cw+G.gap), cy=(G.yF+G.yT)/2.f;
   if(g_deal.stage==0 && now>=g_deal.tSplit){          // the two decks move apart: red (computer's) left, blue (yours) right
      g_deal.stage=1;
      float lx=cx-(G.cw*0.55f+G.gap*0.5f), rx=cx+(G.cw*0.55f+G.gap*0.5f);
      for(int id=0;id<104;id++){
         if(!V[id].placed) continue;
         bool red=id>=52; int k=id%52;
         float jx=(float)((id*37)%5-2)*0.6f, jy=(float)((id*53)%5-2)*0.6f-k*0.25f;
         flyTo(id,(red?lx:rx)+jx,cy+jy,now+k*0.005,0.45,false,20+k);
      }
      snd("click"); g_dirty=true;
   }
   if(g_deal.stage==1 && now>=g_deal.tDeal){           // red -> magazine, blue -> magazine, columns, red -> hand, blue -> hand
      g_deal.stage=2;
      g_dealTops.clear();
      relayout(false,true);
      for(size_t i=0;i<g_rev.pairs.size();i++) for(int pl=0;pl<2;pl++){   // identical magazine cards: the cards to be turned over lie on top of the hands
         int id=pl==0?g_rev.pairs[i].first:g_rev.pairs[i].second;
         float hx,hy; slotPos(handId(pl),hx,hy);
         int n=(int)g_game.pile[handId(pl)].size();
         V[id].tx=hx; V[id].ty=hy-(float)((n-1)/6)*0.8f; V[id].z=250+(int)i;
      }
      g_dirty=true;
   }
   if(g_deal.stage==2 && now>=g_deal.tFlip){           // the top magazine cards of both players turn over at the same time
      g_deal.stage=3; g_deal.active=false;
      for(int id:g_dealTops){ Vis& v=V[id]; v.fsrc=v.flip; v.ftgt=1.f; v.couple=false; v.ft0=now; v.fdur=0.35; }
      snd("click"); g_dirty=true;
   }
}
static bool g_forceTie=false;     // debug key F11
static std::vector<Game> g_hist;  // snapshots for undo (one before every action of the player)
static void newGameStart(){
   SoundSystem::instance().fadeOutAll(150);
   g_fw.stop(); g_overShown=false; g_prev.active=false; g_start.active=false; g_rev.active=false; g_deal.active=false;
   g_drag=decltype(g_drag)(); g_dragging=false; g_plan.active=false; g_hist.clear();
   g_game.newGame(g_forceTie);
   for(Vis& v:V) v=Vis();
   relayout(true);
   // stage 0: both decks lie in one pile (the cards of the two decks mixed)
   float cx=G.x0+3.5f*(G.cw+G.gap), cy=(G.yF+G.yT)/2.f;
   for(int id=0;id<104;id++){
      Vis& v=V[id]; if(!v.placed) continue;
      float jx=(float)((id*37)%5-2)*0.8f, jy=(float)((id*53)%5-2)*0.8f;
      v.x=v.sx=v.tx=cx+jx; v.y=v.sy=v.ty=cy+jy; v.flip=v.fsrc=v.ftgt=0; v.t0=v.dur=0; v.couple=false;
      v.z=20+(id%52)*2+(id/52);
   }
   double now=nowSec();
   g_deal.active=true; g_deal.stage=0;
   g_deal.tSplit=now+0.4; g_deal.tDeal=g_deal.tSplit+0.95; g_deal.tFlip=g_deal.tDeal+1.85;
   double D=g_deal.tFlip+0.6;                           // the deal is over and the magazine tops are turned over
   // The card that decides who starts lifts, spins twice and flashes. With identical magazine cards the hand
   // cards are turned over first (pair after pair); the shown cards go under the hands afterwards.
   g_rev.pairs=g_game.startReveals;
   if(!g_rev.pairs.empty()){
      g_rev.active=true; g_rev.hidden=false; g_rev.t0=D; g_rev.launched=0;
      g_start.t0=D+(double)(g_rev.pairs.size()-1)*REV_STEP+REV_FLIGHT+0.3;
      g_rev.hideAt=g_start.t0+START_DUR+0.3;
      g_dealUntil=g_rev.hideAt+0.7;
   } else {
      g_start.t0=D;
      g_dealUntil=g_start.t0+START_DUR+0.15;
   }
   g_start.active=true; g_start.cardId=g_game.startCardId;
   snd("nowa");
   g_ctx=AIContext();
   const bool me=g_game.turn==0;
   switch(g_game.startHow){
   case Game::SH_MAG_SUIT:
      setStatus(me?L"Takie same figury w magazynach: zaczynasz, bo Twój kolor jest starszy (pik, kier, karo, trefl).":
                   L"Takie same figury w magazynach: komputer zaczyna, bo jego kolor jest starszy (pik, kier, karo, trefl)."); break;
   case Game::SH_HAND_RANK: case Game::SH_HAND_SUIT:
      setStatus(me?L"Identyczne karty w magazynach: rozstrzygnęły karty odkryte z talii. Zaczynasz.":
                   L"Identyczne karty w magazynach: rozstrzygnęły karty odkryte z talii. Komputer zaczyna."); break;
   default:
      setStatus(me?L"Zaczynasz: masz starszą kartę w magazynie.":L"Komputer zaczyna: ma starszą kartę w magazynie.");
   }
   if(!me) g_aiAt=g_dealUntil+0.4;
}
// Restores the game saved on exit. Returns false when there is no valid save.
static bool loadSavedGame(){
   if(!readSavedGame()) return false;
   g_fw.stop(); g_overShown=false; g_prev.active=false; g_start.active=false; g_rev.active=false; g_deal.active=false; g_drag=decltype(g_drag)(); g_dragging=false; g_plan.active=false; g_hist.clear();
   for(Vis& v:V) v=Vis();
   relayout(true);
   g_dealUntil=0; g_ctx=AIContext();
   if(g_game.turn==1) g_aiAt=nowSec()+0.8;
   setStatus(L"Wczytano ostatnią grę.",false,3.5);
   return true;
}

static void endHumanTurnIfSwitched(){
   if(!g_game.over && g_game.turn==1) startAiTurn();
}
// Where a card put on pile `id` would land (for the hint animation).
static void landPos(int id,float& x,float& y){
   slotPos(id,x,y);
   if(ptype(id)==PT_TAB){
      int n=(int)g_game.pile[id].size();
      if(n>0){ float off=std::min(G.fan,(G.tabH-G.ch)/(float)n); y+=n*off; }
   } else if(!g_game.pile[id].empty()) topPos(id,x,y);
}
// Hint: the card itself flies to the destination and back (twice), no frame around it.
static void startPreview(const Move& m){
   const Card* c=(ptype(m.src)==PT_HAND)?g_game.top(m.src):g_game.srcTop(m.src,0);
   if(!c||m.dst<0) return;
   const Vis& v=V[c->id];
   g_prev.active=true; g_prev.cardId=c->id; g_prev.t0=nowSec();
   g_prev.sx=v.tx; g_prev.sy=v.ty;
   landPos(m.dst,g_prev.tx,g_prev.ty);
   g_dirty=true;
}
// Strict obligation: whoever ends the turn while a card still fits a foundation loses the turn.
static void pushUndo(){ g_hist.push_back(g_game); if(g_hist.size()>300) g_hist.erase(g_hist.begin()); }
static void loseTurnForForgetting(const Move& m){
   pushUndo();
   startPreview(m);                        // shows the card that should have gone to the foundation
   g_game.endTurn();
   relayout(); g_dirty=true;
   if(g_game.over){ onGameOver(); return; }
   endHumanTurnIfSwitched();
   snd("nono");
   setStatus(L"Zapomniałeś dołożyć karty do fundamentu: tracisz turę!",true,4);
}
static void humanDraw(){
   if(!humanTurn()) return;
   if(!g_game.pile[turnedId(0)].empty()){ snd("nono"); setStatus(L"Najpierw zagraj albo odrzuć dobraną kartę.",true,3); return; }
   Move m;
   if(g_game.mandatory(0,m)){ loseTurnForForgetting(m); return; }
   if(!g_game.canDraw(0)){ snd("nono"); setStatus(L"Talia i śmietnik są puste. Użyj „Pas”.",true,3); return; }
   pushUndo(); logf("humanDraw"); g_game.draw(0); snd("click"); afterAnyMove(); statusForTurn();
}
static void humanDiscard(){
   if(!humanTurn()) return;
   bool hasTurned=!g_game.pile[turnedId(0)].empty();
   if(!hasTurned && g_game.canDraw(0)) return;            // pass is only possible with nothing left to draw
   Move m;
   if(g_game.mandatory(0,m)){ loseTurnForForgetting(m); return; }
   pushUndo(); logf(hasTurned?"humanDiscard":"humanPass");
   if(hasTurned) g_game.discard(0); else g_game.endTurn();
   snd("click"); afterAnyMove(); endHumanTurnIfSwitched();
}
static void humanMove(int src,int dst,bool undoable=true){
   if(undoable) pushUndo();
   logf("humanMove",src,dst); g_game.doMove(src,dst,0); snd("click"); afterAnyMove();
   if(!g_game.over) statusForTurn();
}
// Undo: restores the state from before the player's last action (a move, drawing, discarding, passing, or a whole
// sequence move). When that action ended the turn, the computer's moves made since are taken back too.
static bool canUndo(){
   return !g_hist.empty() && !g_dragging && !g_plan.active && !g_deal.active && nowSec()>=g_dealUntil && (g_game.over||g_game.turn==0);
}
static void undoMove(){
   if(!canUndo()) return;
   g_game=g_hist.back(); g_hist.pop_back();
   g_ctx=AIContext(); g_overShown=false; g_fw.stop(); g_prev.active=false; g_start.active=false; g_rev.active=false;
   relayout(); g_dirty=true; snd("cofnij");
   statusForTurn();
}
static void doHint(){
   if(!humanTurn()) return;
   Move m; AIContext c;
   if(aiChoose(g_game,0,c,2,g_game.rng,m)){
      startPreview(m); snd("podp");
      setStatus(g_game.mandatory(0,m)?L"Podpowiedź: ta karta musi iść na fundament.":L"Podpowiedź: tak możesz zagrać.",false,3.5);
   } else if(!g_game.pile[turnedId(0)].empty()){
      startPreview({turnedId(0),wasteId(0)}); snd("podp");
      setStatus(L"Brak ruchów: odrzuć dobraną kartę na śmietnik.",false,3.5);
   } else if(g_game.canDraw(0)){
      startPreview({handId(0),turnedId(0)}); snd("podp");
      setStatus(L"Brak ruchów: dobierz kartę z talii.",false,3.5);
   } else { snd("nono"); setStatus(L"Brak ruchów: użyj „Pas”.",true,3.5); }
}
// A click on a card moves it to its best place (foundation, column, opponent's pile...).
// For the turned card the best place may be the own waste pile: then it is discarded.
static void autoClick(int pile){
   if(pile==handId(0)){ humanDraw(); return; }
   const Card* c=g_game.srcTop(pile,0);
   if(!c) return;
   AIContext none; Move best; float bestSc=-1e9f; bool found=false;
   for(int d=0;d<NP;d++){
      if(!g_game.canMove(pile,d,0)) continue;
      float sc=scoreMove(g_game,{pile,d},0,none,2);
      if(sc<=REJECTED+1) continue;                                          // undo-able / looping moves
      if((ptype(d)==PT_RES||ptype(d)==PT_WASTE) && sc<=0) continue;         // pointless on the opponent's piles
      if(sc>bestSc){ bestSc=sc; best={pile,d}; found=true; }
   }
   if(pile==turnedId(0)){
      if(found && bestSc>20) humanMove(best.src,best.dst); else humanDiscard();
      return;
   }
   if(found){ humanMove(best.src,best.dst); return; }
   snd("nono"); setStatus(L"Ta karta nie ma teraz sensownego ruchu.",true,2.5);
}
// Debug aid (F9): dump every pile to garibaldi_dump.txt next to the exe.
static void dumpState(){
   wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH);
   std::wstring path=b; path.resize(path.find_last_of(L'\\')+1); path+=L"garibaldi_dump.txt";
   FILE* f=_wfopen(path.c_str(),L"w"); if(!f) return;
   const char* names[]={"res","hand","turned","waste","tab","fnd"};
   for(int id=0;id<NP;id++){
      fprintf(f,"%s%d (%d):",names[ptype(id)],pidx(id),(int)g_game.pile[id].size());
      for(auto& c:g_game.pile[id]) fprintf(f," %s%s",c.imgKey().c_str(),c.up?"":"*");
      fprintf(f,"\n");
   }
   fprintf(f,"--- log\n%s",g_log.c_str());
   fprintf(f,"turn=%d over=%d totalTurns=%d idle=%d\n",g_game.turn,(int)g_game.over,g_game.totalTurns,g_game.idle);
   fclose(f);
}
static void showRules(){
   MessageBoxW(g_hwnd,
      L"GARIBALDKA (Russian Bank, crapette)\n\n"
      L"Cel: jako pierwszy pozbądź się wszystkich swoich kart: z magazynu, talii i śmietnika.\n\n"
      L"Układ: każdy gracz ma własną talię 52 kart. Magazyn to 12 kart zakrytych i 1 odkryta. "
      L"Po 4 karty z talii każdego gracza leżą we wspólnych kolumnach (razem 8). Reszta (35) to talia. "
      L"Pośrodku jest 8 fundamentów budowanych od asa do króla w jednym kolorze. Mają zarezerwowane kolory, po dwa na kolor, w kolejności starszeństwa: pik, kier, karo, trefl (as zaczyna fundament swojego koloru). Zaczyna ten, kto ma starszą kartę w magazynie (na początku gry unosi się i błyska). "
      L"Przy takich samych figurach decyduje kolor: pik, kier, karo, trefl. Przy identycznych kartach każdy odkrywa pierwszą kartę z talii i ta decyduje tak samo (figura, potem kolor); jeśli znów są identyczne, odkrywane są kolejne. Odkryte karty wracają pod talie.\n\n"
      L"Zagrania:\n"
      L"• Na fundament: as, a potem kolejne karty tego samego koloru.\n"
      L"• Na kolumnę: karta o jeden niższa, w innym kolorze (np. 6♥ na 7♣). Przekładasz po jednej karcie, ale gdy da się to zrobić kolejnymi ruchami (przy wolnych kolumnach i miejscach na innych kolumnach), możesz złapać cały ułożony sekwens: gra sama wykona i pokaże wszystkie ruchy. Pusta kolumna przyjmie dowolną kartę.\n"
      L"• Na magazyn lub śmietnik przeciwnika: karta tego samego koloru o jeden wyższa lub niższa.\n"
      L"• Ścisły przymus: każdą kartę, którą możesz zagrać na fundament (wierzch magazynu, dobrana karta, wierzch śmietnika lub kolumny), musisz tam dołożyć. Kto zapomni i spróbuje dobrać lub odrzucić kartę, traci turę.\n\n"
      L"Tura: graj z magazynu, śmietnika i kolumn. Gdy nie możesz lub nie chcesz grać dalej, dobierz kartę z talii. "
      L"Zagraj ją albo odrzuć na swój śmietnik. Odrzucenie kończy turę. Gdy talia się skończy, śmietnik staje się nową talią.\n\n"
      L"Sterowanie: kliknięcie karty przenosi ją automatycznie na najlepsze miejsce (dobraną kartę, jeśli nic lepszego nie ma, odrzuca na śmietnik). "
      L"Możesz też przeciągnąć kartę tam, gdzie chcesz. Kliknięcie talii dobiera kartę. "
      L"Klawisze: spacja = dobierz, D = odrzuć/pas, H = podpowiedź, U lub Ctrl+Z lub Backspace = cofnij, F2 = nowa gra, M = dźwięk.\n\n"
      L"Cofnij: cofa Twoją ostatnią czynność (ruch, dobranie, odrzucenie, przeniesienie sekwensu). Jeśli ta czynność skończyła turę, cofa też ruchy komputera wykonane od tamtej pory.\n\n"
      L"Gra zapisuje się przy wyjściu i wczytuje przy następnym uruchomieniu.\n\n"
      L"Komputer gra według 10 zasad opisanych w pliku AI_RULES.md.",
      L"Garibaldka – zasady",MB_OK|MB_ICONINFORMATION);
}

// ============================================================================
// Direct2D target
// ============================================================================
static bool ensureRT(){
   if(g_rt) return true; if(!g_d2d||!g_hwnd) return false;
   RECT rc; GetClientRect(g_hwnd,&rc); if(rc.right<=0||rc.bottom<=0) return false;
   HRESULT hr=g_d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(g_hwnd,D2D1::SizeU((UINT32)rc.right,(UINT32)rc.bottom)),&g_rt);
   if(FAILED(hr)){ g_rt=nullptr; return false; }
   g_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
   g_ren.setLayout(&g_lay);
   g_ren.setRT(g_rt,g_dw);
   CardImagesD2D::instance().invalidate();
   return true;
}
static void discardRT(){
   g_ren.setRT(nullptr,nullptr); CardImagesD2D::instance().invalidate();
   if(g_rt){ g_rt->Release(); g_rt=nullptr; }
}

// ============================================================================
// Drawing helpers
// ============================================================================
static void rrect(float x,float y,float w,float h,float rad,float r,float g,float b,float a,bool fill=true,float stroke=1.5f){
   ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(!br) return;
   auto rr=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),rad,rad);
   if(fill) g_rt->FillRoundedRectangle(rr,br); else g_rt->DrawRoundedRectangle(rr,br,stroke);
   br->Release();
}
static void txt(const std::wstring& s,float x,float y,float w,float h,float px,
                float r,float g,float b,float a,bool bold=false,
                DWRITE_TEXT_ALIGNMENT al=DWRITE_TEXT_ALIGNMENT_CENTER){
   if(!g_dw||s.empty()) return;
   IDWriteTextFormat* f=nullptr;
   g_dw->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
   if(!f) return;
   f->SetTextAlignment(al); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
   f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
   ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(br){ g_rt->DrawText(s.c_str(),(UINT32)s.size(),f,D2D1::RectF(x,y,x+w,y+h),br); br->Release(); }
   f->Release();
}
static void ring(float x,float y,float a,bool green=false){
   float r=(float)g_lay.cornerR+3;
   ID2D1SolidColorBrush* br=nullptr;
   g_rt->CreateSolidColorBrush(green?D2D1::ColorF(0.45f,1.f,0.6f,a):D2D1::ColorF(1.f,0.94f,0.f,a),&br);
   if(br){ g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x-3,y-3,x+G.cw+3,y+G.ch+3),r,r),br,3.f); br->Release(); }
}
// An empty foundation shows the ace of its suit, 75% transparent (opacity 25%), without a frame.
static void drawFoundationAce(int id,float x,float y){
   std::string key=std::string("A")+"HDCS"[fndSuit(pidx(id))];
   ID2D1Bitmap* bm=GetCardD2D(key,g_rt);
   if(bm) g_rt->DrawBitmap(bm,D2D1::RectF(x,y,x+G.cw,y+G.ch),0.25f);
}
static const wchar_t* slotLabel(int id){
   switch(ptype(id)){
   case PT_RES: return L"Magazyn"; case PT_HAND: return L"Talia"; case PT_TURNED: return L"Dobrana";
   case PT_WASTE: return L"Śmietnik"; case PT_FND: return L"A"; default: return L"";
   }
}
static void countPill(int id){
   int n=(int)g_game.pile[id].size(); if(n<=0) return;
   float x,y; slotPos(id,x,y);
   float w=26,h=18, px=x+G.cw-w-3, py=y+G.ch-h-3;
   rrect(px,py,w,h,9,0,0,0,0.65f);
   txt(std::to_wstring(n),px,py,w,h,12,1,1,1,1,true);
}

static void drawFireworks(){
   for(auto& fwk:g_fw.fireworks()){
      if(!fwk.exploded){
         g_ren.drawLine(fwk.px,fwk.py,fwk.x,fwk.y,1.8f,fwk.cr,fwk.cg,fwk.cb,120);
         g_ren.drawEllipse(fwk.x,fwk.y,2.5f,255,255,220,220);
      } else {
         if(fwk.flashLife>0){
            float bf=(float)fwk.flashLife/7.f, radius=70.f*bf+30.f;
            ID2D1GradientStopCollection* stops=nullptr; D2D1_GRADIENT_STOP gs[3];
            gs[0].position=0.0f; gs[0].color=D2D1::ColorF(1,1,1,bf*0.9f);
            gs[1].position=0.4f; gs[1].color=D2D1::ColorF(1,0.95f,0.8f,bf*0.55f);
            gs[2].position=1.0f; gs[2].color=D2D1::ColorF(1,0.8f,0.4f,0.f);
            if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,3,&stops))){
               ID2D1RadialGradientBrush* br=nullptr;
               auto rp=D2D1::RadialGradientBrushProperties(D2D1::Point2F(fwk.x,fwk.y),D2D1::Point2F(0,0),radius,radius);
               if(SUCCEEDED(g_rt->CreateRadialGradientBrush(rp,stops,&br))){
                  g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fwk.x,fwk.y),radius,radius),br); br->Release();
               }
               stops->Release();
            }
         }
         for(auto& p:fwk.parts){
            float t=(float)p.life/(float)p.maxLife, af=1.f-t*t;
            BYTE al=(BYTE)(af*255.f); if(al<5) continue;
            if(p.strobe&&(p.life%2)==1) continue;
            BYTE r=(BYTE)(p.r*(1.f-t)+p.er*t), g=(BYTE)(p.g*(1.f-t)+p.eg*t), b=(BYTE)(p.b*(1.f-t)+p.eb*t);
            if(!p.spark&&!p.sub){
               float dx=p.x-p.px,dy=p.y-p.py;
               if(std::sqrt(dx*dx+dy*dy)>0.4f)
                  g_ren.drawLine(p.px,p.py,p.x,p.y,std::max(0.6f,p.size*0.55f*(1.f-t*0.5f)),r,g,b,BYTE(al*0.5f));
            }
            float sz=p.size*(1.f-t*0.55f);
            if(sz>0.25f) g_ren.drawEllipse(p.x,p.y,sz,r,g,b,al);
         }
      }
   }
}

// ============================================================================
// Scene
// ============================================================================
static void layoutButtons(){
   // widths are rough estimates for Segoe UI 14px
   const wchar_t* labels[B_COUNT]={L"Nowa gra",L"Cofnij",L"Podpowiedź",L"Dobierz",L"Odrzuć",L"Poziom: Normalny",L"Dźwięk: wył.",L"Zasady"};
   bool icon[B_COUNT]={true,true,true,false,false,true,false,false};
   float wd[B_COUNT], sum=0;
   for(int i=0;i<B_COUNT;i++){ wd[i]=(float)wcslen(labels[i])*7.6f+24.f+(icon[i]?34.f:0.f); sum+=wd[i]; }
   float gap=8.f, avail=std::max(300.f,G.w-20.f-gap*(B_COUNT-1));
   float k=sum>avail?avail/sum:1.f;                          // a narrow window: the buttons shrink to fit
   float x=10;
   for(int i=0;i<B_COUNT;i++){ g_btn[i]={x,9.f,wd[i]*k,40.f}; x+=wd[i]*k+gap; }
}
static bool btnEnabled(int id){
   switch(id){
   case B_UNDO:    return canUndo();
   case B_HINT:    return humanTurn();
   case B_DRAW:    return humanTurn() && g_game.canDraw(0);
   case B_DISCARD: return humanTurn() && (!g_game.pile[turnedId(0)].empty() || !g_game.canDraw(0));
   default:        return true;
   }
}
static void drawToolbar(){
   rrect(0,0,G.w,TB,0,0,0,0,0.38f);
   for(int i=0;i<B_COUNT;i++){
      const Btn& b=g_btn[i]; bool en=btnEnabled(i), hov=(g_hoverBtn==i)&&en;
      rrect(b.x,b.y,b.w,b.h,8,1,1,1,en?(hov?0.28f:0.14f):0.05f);
      rrect(b.x,b.y,b.w,b.h,8,1,1,1,en?0.35f:0.12f,false,1.f);
      std::wstring lab;
      const char* icoKey=nullptr;
      switch(i){
      case B_NEW:     lab=L"Nowa gra"; icoKey="IMG_NEW"; break;
      case B_UNDO:    lab=L"Cofnij"; icoKey="IMG_UNDO"; break;
      case B_HINT:    lab=L"Podpowiedź"; icoKey="IMG_HINT"; break;
      case B_DRAW:    lab=L"Dobierz"; break;
      case B_DISCARD: lab=(g_game.pile[turnedId(0)].empty()&&!g_game.canDraw(0))?L"Pas":L"Odrzuć"; break;
      case B_LEVEL:   lab=std::wstring(L"Poziom: ")+LEVEL_NAMES[g_level]; icoKey="IMG_USTAWIENIA"; break;
      case B_SOUND:   lab=g_muted?L"Dźwięk: wył.":L"Dźwięk: wł."; break;
      case B_RULES:   lab=L"Zasady"; break;
      }
      float tx=b.x;
      if(icoKey){
         ID2D1Bitmap* bm=GetCardD2D(icoKey,g_rt);
         if(bm) g_rt->DrawBitmap(bm,D2D1::RectF(b.x+7,b.y+5,b.x+7+30,b.y+5+30),en?1.f:0.4f);
         tx=b.x+34;
      }
      txt(lab,tx,b.y,b.w-(tx-b.x),b.h,14,1,1,1,en?0.95f:0.4f,false);
   }
}
static void drawNamePill(int p){
   float x=slotX(1)+G.cw*0.45f, w=G.cw*1.5f+G.gap*2, h=34, y=rowY(p)+G.ch/2-h/2;
   bool act=(g_game.turn==p)&&!g_game.over;
   rrect(x,y,w,h,10,act?1.f:0.f,act?0.82f:0.f,act?0.25f:0.f,act?0.95f:0.35f);
   std::wstring n=p==0?L"TY":std::wstring(L"KOMPUTER");
   txt(n,x,y,w,h,14,act?0.1f:1.f,act?0.1f:1.f,act?0.1f:1.f,1.f,true);
}

static void render(){
   if(!ensureRT()) return;
   double now=nowSec();
   tickVis(now);
   g_rt->BeginDraw();
   g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
   // felt (same green as Pasjans Dziadkowy) with a soft vignette
   {
      ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(20/255.f,100/255.f,40/255.f),&br);
      if(br){ g_rt->FillRectangle(D2D1::RectF(0,0,G.w,G.h),br); br->Release(); }
      ID2D1GradientStopCollection* st=nullptr; D2D1_GRADIENT_STOP gs[2];
      gs[0].position=0.f; gs[0].color=D2D1::ColorF(1,1,1,0.07f);
      gs[1].position=1.f; gs[1].color=D2D1::ColorF(0,0,0,0.30f);
      if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,2,&st))){
         ID2D1RadialGradientBrush* rb=nullptr;
         float R=std::max(G.w,G.h)*0.75f;
         if(SUCCEEDED(g_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(G.w/2,G.h/2),D2D1::Point2F(0,0),R,R),st,&rb))){
            g_rt->FillRectangle(D2D1::RectF(0,0,G.w,G.h),rb); rb->Release();
         }
         st->Release();
      }
   }
   drawToolbar();                    // before the cards, so a lifted or dragged card is drawn over it
   // empty slots
   for(int id=0;id<NP;id++){
      if(ptype(id)==PT_TAB && !g_game.pile[id].empty()) continue;
      float x,y; slotPos(id,x,y);
      if(ptype(id)==PT_FND) drawFoundationAce(id,x,y);
      else g_ren.drawEmpty(x,y,slotLabel(id));
   }
   drawNamePill(0); drawNamePill(1);

   // cards, back to front
   struct Item{ int z; int id; };
   std::vector<Item> items; items.reserve(104);
   // A pile with a card in flight is drawn above the others as a whole. (Raising only the flying card
   // let an earlier, longer flight stay on top of a later card landing on the same column.)
   bool pileFlying[NP]={};
   for(int id=0;id<104;id++){ const Vis& v=V[id]; if(v.placed&&v.pile>=0&&now<v.t0+v.dur) pileFlying[v.pile]=true; }
   for(int id=0;id<104;id++){
      const Vis& v=V[id]; if(!v.placed||v.pile<0) continue;
      if(isDragged(id)) continue;
      if(g_prev.active&&g_prev.cardId==id&&now<g_prev.t0+PREV_TOTAL) continue;           // drawn separately, on top
      if(g_start.active&&g_start.cardId==id&&now>=g_start.t0&&now<g_start.t0+START_DUR) continue;
      items.push_back({v.z+(pileFlying[v.pile]?1000:0),id});
   }
   std::sort(items.begin(),items.end(),[](const Item& a,const Item& b){ return a.z<b.z; });
   auto drawOne=[&](int id,float x,float y,bool selected){
      const Vis& v=V[id]; const Card& c=C[id];
      int backDeck=c.deck==0?1:0;                  // player = blue back, computer = red back
      if(v.flip>=0.999f && v.lift<0.001f) g_ren.drawCard(x,y,c,selected,false);
      else g_ren.drawCardFlip(x,y,c,backDeck,1.f+0.3f*v.lift,v.flip);
   };
   for(auto& it:items){
      const Vis& v=V[it.id];
      drawOne(it.id,std::floor(v.x),std::floor(v.y),false);
   }
   // pile counters
   for(int p=0;p<2;p++){ countPill(resId(p)); countPill(handId(p)); countPill(wasteId(p)); }

   // valid drop targets while a card is being dragged
   if(g_dragging && g_drag.idx>=0){
      for(int d:g_dragTargets){ float x,y; landPos(d,x,y); ring(x,y,0.9f,true); }
   } else if(g_dragging && g_game.srcTop(g_drag.pile,0)){
      for(int d=0;d<NP;d++){
         bool ok=g_game.canMove(g_drag.pile,d,0) || (g_drag.pile==turnedId(0)&&d==wasteId(0));
         if(!ok) continue;
         float x,y; topPos(d,x,y); ring(x,y,0.9f,true);
      }
   }
   // hint: the card itself flies to its destination and back (no frame)
   if(g_prev.active){
      double t=now-g_prev.t0;
      if(t>=PREV_TOTAL) g_prev.active=false;
      else {
         t=std::fmod(t,PREV_CYCLE);
         auto sm=[](double u){ return (float)(u*u*(3.0-2.0*u)); };
         float u= t<0.55 ? sm(t/0.55) : t<0.75 ? 1.f : t<1.25 ? 1.f-sm((t-0.75)/0.5) : 0.f;
         drawOne(g_prev.cardId,std::floor(g_prev.sx+(g_prev.tx-g_prev.sx)*u),std::floor(g_prev.sy+(g_prev.ty-g_prev.sy)*u),false);
      }
   }
   // opening animation: the older magazine card lifts (+60%), spins twice and flashes at the top
   if(g_start.active){
      double t=now-g_start.t0;
      if(t>=START_DUR) g_start.active=false;
      else if(t>=0){
         const Vis& v=V[g_start.cardId]; const Card& c=C[g_start.cardId];
         float u=(float)(t/START_DUR);
         float sc=1.f+0.6f*std::sin(3.14159265f*u);                 // up and down: +60% at the middle
         float cs=std::cos(4.f*3.14159265f*u);                      // two full turns around the vertical axis
         float cx=v.x+G.cw/2.f, cy=v.y+G.ch/2.f;
         D2D1::Matrix3x2F old; g_rt->GetTransform(&old);
         g_rt->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(sc*std::max(0.03f,std::fabs(cs)),sc),D2D1::Point2F(cx,cy))*old);
         if(cs>=0) g_ren.drawCard(std::floor(v.x),std::floor(v.y),c,false,false);
         else      g_ren.drawBack(std::floor(v.x),std::floor(v.y),c.deck==0?1:0);
         // flash: START_FLASH (200 ms) around the middle of the animation, when the card is largest
         float fl=1.f-(float)std::fabs(t-START_DUR/2.0)/(float)(START_FLASH/2.0);
         if(fl>0){
            auto rr=D2D1::RoundedRect(D2D1::RectF(v.x,v.y,v.x+G.cw,v.y+G.ch),(float)g_lay.cornerR,(float)g_lay.cornerR);
            ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.98f,0.85f,0.9f*fl),&br);
            if(br){ g_rt->FillRoundedRectangle(rr,br); br->Release(); }
         }
         g_rt->SetTransform(old);
         if(fl>0){                       // the light reaches one card beyond the card in every direction
            float rx=G.cw*(0.5f*sc+1.f), ry=G.ch*(0.5f*sc+1.f);
            ID2D1GradientStopCollection* st=nullptr; D2D1_GRADIENT_STOP gs[4];
            gs[0].position=0.f;   gs[0].color=D2D1::ColorF(1.f,1.f,0.95f,0.95f*fl);
            gs[1].position=0.45f; gs[1].color=D2D1::ColorF(1.f,0.97f,0.75f,0.70f*fl);
            gs[2].position=0.75f; gs[2].color=D2D1::ColorF(1.f,0.93f,0.60f,0.35f*fl);
            gs[3].position=1.f;   gs[3].color=D2D1::ColorF(1.f,0.90f,0.50f,0.f);
            if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,4,&st))){
               ID2D1RadialGradientBrush* rb=nullptr;
               if(SUCCEEDED(g_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx,cy),D2D1::Point2F(0,0),rx,ry),st,&rb))){
                  g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),rx,ry),rb); rb->Release();
               }
               st->Release();
            }
         }
      }
   }
   // dragged card on top
   if(g_dragging) for(int id:g_dragIds){ const Vis& v=V[id]; drawOne(id,std::floor(v.x),std::floor(v.y),false); }
   // game-over overlay + fireworks
   if(g_game.over && g_overShown){
      float dim=(float)std::min(1.0,(now-g_overAt)/0.6)*0.45f;
      rrect(0,TB,G.w,G.h-TB-SB,0,0,0,0,dim);
      drawFireworks();
      std::wstring t=g_game.winner==0?L"Wygrywasz!":g_game.winner==1?L"Komputer wygrał":L"Remis";
      txt(t,0,G.h/2-70,G.w,80,64,1.f,0.86f,0.2f,1.f,true);
      txt(L"Kliknij „Nowa gra” (F2), aby zagrać ponownie",0,G.h/2+10,G.w,40,20,1,1,1,0.9f,false);
   }
   // status bar
   rrect(0,G.h-SB,G.w,SB,0,0,0,0,0.38f);
   if(g_statusUntil>0 && now>g_statusUntil){ g_statusUntil=0; statusForTurn(); }
   if(g_statusErr) txt(g_status,12,G.h-SB,G.w-24,SB,14,1.f,0.5f,0.5f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   else            txt(g_status,12,G.h-SB,G.w-24,SB,14,1,1,1,0.85f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   HRESULT hr=g_rt->EndDraw();
   if(hr==D2DERR_RECREATE_TARGET) discardRT();
   g_dirty=false;
}

// ============================================================================
// Input
// ============================================================================
static int hitPile(float x,float y){
   for(int id=NP-1;id>=0;id--){
      ptype(id);
      if(ptype(id)==PT_HAND&&pidx(id)==1) continue;       // computer's piles can only be targets (magazine, waste)
      const RectF4& r=PR[id];
      if(x>=r.l&&x<=r.r&&y>=r.t&&y<=r.b) return id;
   }
   return -1;
}
static int hitButton(float x,float y){
   for(int i=0;i<B_COUNT;i++){
      const Btn& b=g_btn[i];
      if(x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h) return i;
   }
   return -1;
}
static void doButton(int id){
   switch(id){
   case B_NEW:     newGameStart(); break;
   case B_UNDO:    undoMove(); break;
   case B_HINT:    doHint(); break;
   case B_DRAW:    humanDraw(); break;
   case B_DISCARD: humanDiscard(); break;
   case B_LEVEL:   g_level=(g_level+1)%3; saveSettings(); setStatus(std::wstring(L"Poziom komputera: ")+LEVEL_NAMES[g_level],false,3); break;
   case B_SOUND:   g_muted=!g_muted; saveSettings(); if(g_muted) SoundSystem::instance().fadeOutAll(100); else snd("click"); break;
   case B_RULES:   showRules(); break;
   }
   g_dirty=true;
}
// Drop target for a dragged card: the valid pile under the cursor, else the valid pile it overlaps most.
static int dropTarget(int src,float mx,float my,float cardX,float cardY){
   auto valid=[&](int d){ return g_game.canMove(src,d,0)||(src==turnedId(0)&&d==wasteId(0)); };
   int h=hitPile(mx,my);
   if(h>=0&&h!=src&&valid(h)) return h;
   int best=-1; float bestA=G.cw*G.ch*0.12f;
   for(int d=0;d<NP;d++){
      if(d==src||!valid(d)) continue;
      float x,y; topPos(d,x,y);
      float ox=std::min(cardX+G.cw,x+G.cw)-std::max(cardX,x), oy=std::min(cardY+G.ch,y+G.ch)-std::max(cardY,y);
      if(ox>0&&oy>0&&ox*oy>bestA){ bestA=ox*oy; best=d; }
   }
   return best;
}
static void applyHumanTarget(int src,int dst){
   if(src==turnedId(0)&&dst==wasteId(0)){ humanDiscard(); return; }
   humanMove(src,dst);
}

// ---------------------------------------------------------------------------
// Moving a whole sequence of a column: done card by card, each move animated.
// ---------------------------------------------------------------------------
static bool seqTarget(int src,int idx,int dst,std::vector<Move>& plan){
   if(idx==0 && g_game.pile[dst].empty()) return false;          // moving a whole column to an empty one is pointless
   return planSequenceMove(g_game,src,idx,dst,plan);
}
static void startSeqPlan(const std::vector<Move>& plan){
   pushUndo();                                                  // the whole sequence is a single undo step
   g_plan.active=true; g_plan.moves=plan; g_plan.next=0; g_plan.at=nowSec()+0.22;   // let the dragged cards return first
   g_prev.active=false;
   setStatus(plan.size()>1?L"Przenoszę sekwens kolejnymi ruchami…":L"Przenoszę kartę…");
}
static void planTick(double now){
   if(!g_plan.active||now<g_plan.at||anyAnimating(now)) return;
   if(g_plan.next>=g_plan.moves.size()){ g_plan.active=false; statusForTurn(); return; }
   Move m=g_plan.moves[g_plan.next];
   if(!g_game.canMove(m.src,m.dst,0)){ g_plan.active=false; relayout(); statusForTurn(); return; }   // cannot happen; stay safe
   humanMove(m.src,m.dst,false);
   g_plan.next++; g_plan.at=now+0.05;
   if(g_plan.next>=g_plan.moves.size()) g_plan.active=false;
   else if(!g_game.over) setStatus(L"Przenoszę sekwens kolejnymi ruchami…");
}
// A click on a card inside a column: the whole sequence from that card goes to the best column.
static void autoSeq(int src,int idx){
   std::vector<Move> best; int bestDst=-1; bool bestEmpty=true;
   for(int j=0;j<NUM_TAB;j++){
      int d=tabId(j); if(d==src) continue;
      std::vector<Move> pl; if(!seqTarget(src,idx,d,pl)) continue;
      bool empty=g_game.pile[d].empty();
      // prefer a column with cards on it, then the shorter plan
      if(bestDst<0 || (bestEmpty&&!empty) || (empty==bestEmpty && pl.size()<best.size())){ best=pl; bestDst=d; bestEmpty=empty; }
   }
   if(bestDst<0){ snd("nono"); setStatus(L"Ten sekwens nie ma gdzie przejść (albo brakuje wolnego miejsca).",true,3); return; }
   startSeqPlan(best);
}
// index of the topmost card of column `col` under the point, or -1
static int hitTabCard(int col,float x,float y){
   const auto& pl=g_game.pile[col];
   for(int i=(int)pl.size()-1;i>=0;i--){
      const Vis& v=V[pl[i].id];
      if(x>=v.tx&&x<=v.tx+G.cw&&y>=v.ty&&y<=v.ty+G.ch) return i;
   }
   return -1;
}

static void onLDown(int mx,int my){
   int b=hitButton((float)mx,(float)my);
   if(b>=0){ if(btnEnabled(b)) doButton(b); return; }
   if(!humanTurn()) return;
   int p=hitPile((float)mx,(float)my);
   g_drag=decltype(g_drag)(); g_drag.down=true; g_drag.pile=p; g_drag.mx=mx; g_drag.my=my;
   if(p>=0 && p!=handId(0)){
      const Card* c=g_game.srcTop(p,0);
      int pick=c?c->id:-1;
      if(ptype(p)==PT_TAB){
         const auto& pl=g_game.pile[p]; int n=(int)pl.size();
         int i=hitTabCard(p,(float)mx,(float)my); if(i<0) i=n-1;
         if(i<n-1){                                            // a card below the top: only a proper sequence can be picked
            if(isRunFrom(g_game,p,i)){ g_drag.idx=i; pick=pl[i].id; }
            else { g_drag.badRun=true; pick=-1; }
         }
      }
      if(pick>=0){
         g_drag.cardId=pick;
         g_drag.offX=(float)mx-V[pick].x; g_drag.offY=(float)my-V[pick].y;
      }
   }
   SetCapture(g_hwnd);
}
static void onMouseMove(int mx,int my){
   int hb=hitButton((float)mx,(float)my);
   if(hb!=g_hoverBtn){ g_hoverBtn=hb; g_dirty=true; }
   if(!g_drag.down) return;
   if(!g_dragging && g_drag.cardId>=0 && std::abs(mx-g_drag.mx)+std::abs(my-g_drag.my)>6){
      g_dragging=true; g_prev.active=false;
      g_dragIds.clear(); g_dragRel.clear(); g_dragTargets.clear();
      if(g_drag.idx>=0){                                       // a sequence: remember where it can go
         const auto& pl=g_game.pile[g_drag.pile];
         for(int i=g_drag.idx;i<(int)pl.size();i++) g_dragIds.push_back(pl[i].id);
         for(int j=0;j<NUM_TAB;j++){ int d=tabId(j); std::vector<Move> tmp; if(d!=g_drag.pile && seqTarget(g_drag.pile,g_drag.idx,d,tmp)) g_dragTargets.push_back(d); }
      } else {
         g_dragIds.push_back(g_drag.cardId);
         g_drag.pile=V[g_drag.cardId].pile;
      }
      g_dragCard=g_dragIds[0];
      float y0=V[g_dragCard].y; for(int id:g_dragIds) g_dragRel.push_back(V[id].y-y0);
   }
   if(g_dragging){
      for(size_t j=0;j<g_dragIds.size();j++){ Vis& v=V[g_dragIds[j]]; v.x=(float)mx-g_drag.offX; v.y=(float)my-g_drag.offY+g_dragRel[j]; }
      g_dirty=true;
   }
}
static void onLUp(int mx,int my){
   if(!g_drag.down) return;
   ReleaseCapture(); g_drag.down=false;
   if(g_dragging){
      std::vector<int> ids=g_dragIds; int src=g_drag.pile, idx=g_drag.idx; bool run=idx>=0;
      std::vector<int> targets=g_dragTargets;
      g_dragging=false; g_dragCard=-1; g_dragIds.clear(); g_dragTargets.clear();
      Vis& v=V[ids[0]];
      double now=nowSec();
      for(int id:ids){ Vis& w=V[id]; w.sx=w.x; w.sy=w.y; w.t0=now; w.dur=0.18; }   // the cards keep flying from the drop point
      if(!run){
         int dst=dropTarget(src,(float)mx,(float)my,v.x,v.y);
         if(dst>=0){ applyHumanTarget(src,dst); }
         else {
            if(hitPile((float)mx,(float)my)>=0 && hitPile((float)mx,(float)my)!=src) snd("nono");
            relayout();
         }
      } else {
         int dst=-1; int h=hitPile((float)mx,(float)my);
         for(int d:targets) if(d==h) dst=d;
         if(dst<0){                                              // else: the target column it overlaps most
            float bestA=G.cw*G.ch*0.12f;
            for(int d:targets){
               float x,y; landPos(d,x,y);
               float ox=std::min(v.x+G.cw,x+G.cw)-std::max(v.x,x), oy=std::min(v.y+G.ch,y+G.ch)-std::max(v.y,y);
               if(ox>0&&oy>0&&ox*oy>bestA){ bestA=ox*oy; dst=d; }
            }
         }
         std::vector<Move> plan;
         if(dst>=0 && seqTarget(src,idx,dst,plan)) startSeqPlan(plan);
         else {
            if(h>=0 && ptype(h)==PT_TAB && h!=src && g_game.canPlace(g_game.pile[src][idx],h,0,src)){
               snd("nono"); setStatus(L"Za mało wolnego miejsca, żeby przenieść ten sekwens.",true,3);
            } else if(h>=0 && h!=src) snd("nono");
            relayout();
         }
      }
      g_dirty=true; return;
   }
   int p=hitPile((float)mx,(float)my);
   if(p>=0 && p==g_drag.pile && humanTurn()){
      if(g_drag.idx>=0) autoSeq(p,g_drag.idx);                    // click inside a column: move the sequence from that card
      else if(g_drag.badRun){ snd("nono"); setStatus(L"To nie jest sekwens: przenosić można tylko ułożone karty (malejąco, na przemian kolory).",true,3); }
      else autoClick(p);                                          // a plain click moves the card to its best place
   }
}
// ============================================================================
// Computer's turn
// ============================================================================
static void aiTick(double now){
   if(g_game.over||g_game.turn!=1||now<g_dealUntil||now<g_aiAt) return;
   if(uiBusy(now)) return;
   Step s=aiStep(g_game,1,g_ctx,g_level);
   logf("ai",s.m.src,s.m.dst);
   snd("click");
   afterAnyMove();
   g_aiAt=now+(s.kind==ST_MOVE?0.60:0.55);
   if(!g_game.over && g_game.turn==0){ g_ctx=AIContext(); statusForTurn(); }
}

// ============================================================================
// Window
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
   switch(msg){
   case WM_CREATE:
      g_hwnd=hwnd;
      SoundSystem::instance().init(hwnd);
      CardImagesD2D::instance().init(((CREATESTRUCT*)lp)->hInstance,g_wic);
      return 0;
   case WM_SIZE:{
      int w=LOWORD(lp), h=HIWORD(lp);
      if(wp==SIZE_MINIMIZED||w<=0||h<=0) return 0;
      if(g_rt) g_rt->Resize(D2D1::SizeU((UINT32)w,(UINT32)h));
      computeGeo((float)w,(float)h); layoutButtons();
      relayout(true); g_dirty=true; return 0;}
   case WM_GETMINMAXINFO:{ auto* m=(MINMAXINFO*)lp; m->ptMinTrackSize.x=820; m->ptMinTrackSize.y=620; return 0;}
   case WM_PAINT:{ PAINTSTRUCT ps; BeginPaint(hwnd,&ps); render(); EndPaint(hwnd,&ps); return 0;}
   case WM_ERASEBKGND: return 1;
   case WM_LBUTTONDOWN: onLDown(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_LBUTTONUP:   onLUp(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_LBUTTONDBLCLK: return 0;                       // ignored: a double click must not move two cards
   case WM_MOUSEMOVE:   onMouseMove(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_KEYDOWN:
      switch(wp){
      case VK_F2:     doButton(B_NEW); break;
      case VK_BACK:   undoMove(); break;
      case 'U':       undoMove(); break;
      case 'Z':       if(GetKeyState(VK_CONTROL)&0x8000) undoMove(); break;
      case VK_F1:     showRules(); break;
      case VK_F9:     dumpState(); break;
      case VK_F11:    g_forceTie=true; newGameStart(); g_forceTie=false; break;   // debug: a game that starts with identical magazine cards
      case VK_F10:    if(!g_game.over){ g_game.over=true; g_game.winner=0; afterAnyMove(); } break;   // debug: force a win
      case VK_SPACE:  humanDraw(); break;
      case 'D':       humanDiscard(); break;
      case 'H':       doHint(); break;
      case 'M':       doButton(B_SOUND); break;
      }
      return 0;
   case WM_ENDSESSION: if(wp) { saveGame(); saveSettings(); } return 0;
   case WM_DESTROY:
      saveGame(); saveSettings(); PostQuitMessage(0); return 0;
   }
   return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nShow){
   HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Garibaldka_SingleInstance");
   if(GetLastError()==ERROR_ALREADY_EXISTS){
      HWND ex=FindWindowW(L"GaribaldkaWnd",nullptr);
      if(ex){ if(IsIconic(ex)) ShowWindow(ex,SW_RESTORE); SetForegroundWindow(ex); }
      CloseHandle(mutex); return 0;
   }
   { typedef BOOL(WINAPI* PFN)(); auto f=(PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDPIAware"); if(f) f(); }
   timeBeginPeriod(1);
   CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
   if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&g_d2d))){
      MessageBoxW(nullptr,L"Błąd inicjalizacji Direct2D.",L"Garibaldka",MB_ICONERROR); return 1; }
   DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&g_dw);
   CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g_wic));
   loadSettings();

   WNDCLASSEXW wc={sizeof(wc)};
   wc.lpfnWndProc=WndProc; wc.hInstance=hInst; wc.lpszClassName=L"GaribaldkaWnd";
   wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
   wc.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;
   wc.hIcon=(HICON)LoadImageW(hInst,L"APPICON",IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
   if(!wc.hIcon) wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
   wc.hIconSm=(HICON)LoadImageW(hInst,L"APPICON",IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
   RegisterClassExW(&wc);
   HWND hwnd=CreateWindowExW(0,L"GaribaldkaWnd",L"Garibaldka 1.0.0",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,1180,860,nullptr,nullptr,hInst,nullptr);
   if(!hwnd){ MessageBoxW(nullptr,L"Nie można utworzyć okna.",L"Garibaldka",MB_ICONERROR); return 1; }
   {  // restore window placement
      wchar_t buf[128]={}; auto ini=iniPath();
      GetPrivateProfileStringW(L"Window",L"Placement",L"",buf,128,ini.c_str());
      int l,t,r,b,cmd=SW_SHOWNORMAL;
      if(buf[0] && swscanf(buf,L"%d,%d,%d,%d,%d",&l,&t,&r,&b,&cmd)==5){
         RECT rc={l,t,r,b};
         if(MonitorFromRect(&rc,MONITOR_DEFAULTTONULL)){
            WINDOWPLACEMENT wp={sizeof(wp)}; wp.showCmd=SW_HIDE; wp.rcNormalPosition=rc; SetWindowPlacement(hwnd,&wp);
            ShowWindow(hwnd,cmd==SW_MAXIMIZE?SW_MAXIMIZE:SW_SHOWNORMAL);
         } else ShowWindow(hwnd,SW_SHOWNORMAL);
      } else ShowWindow(hwnd,SW_SHOWNORMAL);
   }
   UpdateWindow(hwnd);
   {  RECT rc; GetClientRect(hwnd,&rc); computeGeo((float)rc.right,(float)rc.bottom); layoutButtons(); }
   if(!loadSavedGame()) newGameStart();

   MSG msg={};
   for(;;){
      while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){
         if(msg.message==WM_QUIT) goto done;
         TranslateMessage(&msg); DispatchMessage(&msg);
      }
      double now=nowSec();
      planTick(now);
      dealTick(now);
      revealTick(now);
      aiTick(now);
      if(g_fw.active() && now-g_fwLast>=0.028){ g_fw.tick((int)G.w,(int)G.h); g_fwLast=now; g_dirty=true; }
      bool anim=anyAnimating(now)||g_plan.active||g_dragging||g_prev.active||g_start.active||g_rev.active||g_deal.active||g_fw.active()||(g_game.over&&g_overShown&&now-g_overAt<1.0);
      if(anim||g_dirty){ render(); if(!anim) continue; Sleep(1); }
      else MsgWaitForMultipleObjects(0,nullptr,FALSE,25,QS_ALLINPUT);
   }
done:
   discardRT();
   if(g_wic){ g_wic->Release(); g_wic=nullptr; }
   if(g_dw){ g_dw->Release(); g_dw=nullptr; }
   if(g_d2d){ g_d2d->Release(); g_d2d=nullptr; }
   SoundSystem::instance().shutdown();
   CoUninitialize();
   timeEndPeriod(1);
   return (int)msg.wParam;
}
