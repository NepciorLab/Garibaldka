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
#include <memory>
#include <string>
#include <vector>
#include "anim.h"
#include "layout.h"
#include "card_images_d2d.h"
#include "renderer_d2d.h"
#include "sound.h"
#include "update.h"

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
// Version of this build. A release on GitHub is tagged vMAJOR.MINOR.PATCH with the same number and carries
// an asset called Garibaldi.exe: the updater (update.h) compares the tag with this number.
static const wchar_t* APP_VERSION = L"1.0.1";
static bool  g_checkUpdates=true;     // check GitHub for a newer release at startup
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdate-time"     // the build date is meant to change with every build
#endif
static std::wstring buildDateText(){   // "5 października 2026", from the compiler's __DATE__
   const char* d=__DATE__;
   static const char* M="JanFebMarAprMayJunJulAugSepOctNovDec";
   static const wchar_t* PL[12]={L"stycznia",L"lutego",L"marca",L"kwietnia",L"maja",L"czerwca",L"lipca",L"sierpnia",L"września",L"października",L"listopada",L"grudnia"};
   int mon=0; for(int i=0;i<12;i++) if(strncmp(d,M+i*3,3)==0){ mon=i; break; }
   return std::to_wstring(atoi(d+4))+L" "+PL[mon]+L" "+std::to_wstring(atoi(d+7));
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
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
struct Geo{ float w=1000,h=700,cw=90,ch=126,gap=10,rg=14,x0=0,yAI=0,yF=0,yT=0,yP=0,tabH=0,fan=30,fs=40; } G;
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
   float cwW=(w-24.f-7*G.gap)/8.55f;                 // 8 columns + room for the flame
   float chH=(availH-3*G.rg-16.f)/5.1f;
   float cw=std::min(std::min(cwW,chH/1.4f),150.f);
   cw=std::max(36.f,std::floor(cw));
   G.cw=cw; G.ch=std::floor(cw*1.4f);
   G.fs=std::floor(G.cw*0.55f);                                 // room left of the magazines for the flame
   G.x0=std::floor((w-(G.fs+8*G.cw+7*G.gap))/2.f+G.fs);
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
// Order of the deal: red (computer's) magazine, blue magazine, red hand, blue hand, and last the cards on the table
// (columns): red, blue.
static double dealDelay(const Card& c,int pileId,int idx){
   static const int start[6]={0,13,26,61,96,100};
   int red=(c.deck==1)?0:1, cat=0, k=0;
   switch(ptype(pileId)){
   case PT_RES:  cat=0+red; k=idx; break;
   case PT_HAND: cat=2+red; k=idx; break;
   case PT_TAB:  cat=4+red; k=(pileId-8)%4; break;
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
      // A card that turns over while its spot shifts by a few pixels (the face-down cards of a stack are offset
      // by up to 6 px) is NOT a move: it turns over in place - after the card that moved has landed.
      if(moved && ftgt!=v.ftgt && now>=v.t0+v.dur && std::hypot(tx-v.tx,ty-v.ty)<14.f){
         v.x=v.sx=v.tx=tx; v.y=v.sy=v.ty=ty; moved=false;
      }
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
// The flame next to a player's magazine shows whose turn it is. It lights up when the starting player is
// known and moves (ease in-out) to the other player's magazine when the turn changes.
struct Flame{ bool lit=false; double igniteAt=0, last=0, t0=0, dur=0.65; int player=0; float scale=0, y=0, from=0, to=0; bool moving=false; };
static Flame g_flame;
static ID2D1Bitmap* g_flameSpr[8]={};      // soft round sprites, from white-hot yellow to red and to smoke
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
   WritePrivateProfileStringW(L"Settings",L"CheckUpdatesOnStart",g_checkUpdates?L"1":L"0",ini.c_str());
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
   g_checkUpdates=GetPrivateProfileIntW(L"Settings",L"CheckUpdatesOnStart",1,ini.c_str())!=0;
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
   if(g_deal.stage==1 && now>=g_deal.tDeal){           // magazines, hands, then the table (columns)
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
   g_flame=Flame(); g_flame.igniteAt=g_start.t0+START_DUR;     // the flame lights up when the winning card has landed
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
   g_flame=Flame(); g_flame.igniteAt=nowSec();
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
   // Best of ALL legal moves (see bestClickMove). Only when there is none: the turned card goes to the waste pile.
   Move best;
   if(bestClickMove(g_game,pile,best)){ humanMove(best.src,best.dst); return; }
   if(pile==turnedId(0)){ humanDiscard(); return; }
   snd("nono"); setStatus(L"Ta karta nie ma żadnego dozwolonego ruchu.",true,2.5);
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
// ---------------------------------------------------------------------------
// Rules window: a scrollable panel drawn in the style of the game (see helpDraw).
// ---------------------------------------------------------------------------
struct Help{ bool open=false; float scroll=0, contentH=0, builtW=-1; bool dragThumb=false; float grabDy=0; };
static Help g_help;
static void showRules(){ g_help.open=!g_help.open; g_help.dragThumb=false; if(g_help.open){ g_help.scroll=0; g_help.builtW=-1; } g_dirty=true; }

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
   for(auto& b:g_flameSpr) if(b){ b->Release(); b=nullptr; }
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
// The flame: burning next to the magazine of the player whose turn it is.
static float flameCenterY(int p){ return rowY(p)+G.ch*0.5f; }
static void drawFlame(double now){
   Flame& F=g_flame;
   if(!F.lit && F.igniteAt>0 && now>=F.igniteAt){ F.lit=true; F.player=g_game.turn; F.y=flameCenterY(F.player); F.moving=false; F.last=now; }
   double dt=F.last>0?std::min(0.1,now-F.last):0.0; F.last=now;
   float target=(F.lit && !(g_game.over&&g_overShown))?1.f:0.f;          // goes out when the game is over
   F.scale+=(target-F.scale)*(float)(1.0-std::exp(-dt*(target>F.scale?4.5:7.0)));
   if(F.scale<0.01f && target==0.f) return;
   if(F.lit){
      if(!g_game.over && g_game.turn!=F.player){                          // the turn changed: the flame moves on
         F.from=F.y; F.to=flameCenterY(g_game.turn); F.t0=now; F.moving=true; F.player=g_game.turn;
      }
      if(F.moving){
         float pr=(float)((now-F.t0)/F.dur); if(pr>=1.f){ pr=1.f; F.moving=false; }
         F.y=F.from+(F.to-F.from)*easeInOut(pr);                          // accelerates, then slows down
      } else F.y=flameCenterY(F.player);
   }
   const float cx=G.x0-G.fs*0.5f-2.f;
   const float H=G.ch*0.92f*F.scale, W=G.fs*0.78f*F.scale;
   const float by=F.y+G.ch*0.44f;                                         // base of the flame
   const float t=(float)now, fl=0.5f+0.5f*std::sin(t*13.f)*std::sin(t*7.3f+1.f);   // flicker 0..1
   const float a=std::min(1.f,F.scale*1.6f);
   auto P=[](float x,float y){ return D2D1::Point2F(x,y); };
   // glow on the table around the fire
   {
      ID2D1GradientStopCollection* st=nullptr; D2D1_GRADIENT_STOP gs[2];
      gs[0].position=0.f; gs[0].color=D2D1::ColorF(1.f,0.55f,0.12f,(0.26f+0.12f*fl)*a);
      gs[1].position=1.f; gs[1].color=D2D1::ColorF(1.f,0.35f,0.05f,0.f);
      if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,2,&st))){
         ID2D1RadialGradientBrush* rb=nullptr;
         float rx=W*2.1f+1.f, ry=H*0.9f+1.f;
         if(SUCCEEDED(g_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(P(cx,by-H*0.35f),P(0,0),rx,ry),st,&rb))){
            g_rt->FillEllipse(D2D1::Ellipse(P(cx,by-H*0.35f),rx,ry),rb); rb->Release();
         }
         st->Release();
      }
   }
   // The fire is a swarm of soft particles that rise, wander (turbulence), shrink, fade and cool down:
   // white-hot yellow at the base, orange and red higher up, a little dark smoke above. Everything is a function
   // of time, so nothing has to be stored between frames.
   static const float SC[8][3]={{1.f,0.97f,0.78f},{1.f,0.86f,0.32f},{1.f,0.68f,0.10f},{1.f,0.50f,0.04f},
                                {0.98f,0.34f,0.02f},{0.86f,0.20f,0.02f},{0.50f,0.09f,0.03f},{0.30f,0.28f,0.27f}};
   if(!g_flameSpr[0]){
      for(int i=0;i<8;i++){
         ID2D1BitmapRenderTarget* brt=nullptr;
         if(FAILED(g_rt->CreateCompatibleRenderTarget(D2D1::SizeF(64,64),&brt))) return;
         brt->BeginDraw(); brt->Clear(D2D1::ColorF(0,0,0,0));
         D2D1_GRADIENT_STOP gs[4];
         gs[0].position=0.00f; gs[0].color=D2D1::ColorF(SC[i][0],SC[i][1],SC[i][2],1.00f);
         gs[1].position=0.35f; gs[1].color=D2D1::ColorF(SC[i][0],SC[i][1],SC[i][2],0.55f);
         gs[2].position=0.70f; gs[2].color=D2D1::ColorF(SC[i][0],SC[i][1],SC[i][2],0.16f);
         gs[3].position=1.00f; gs[3].color=D2D1::ColorF(SC[i][0],SC[i][1],SC[i][2],0.00f);
         ID2D1GradientStopCollection* st=nullptr; ID2D1RadialGradientBrush* rb=nullptr;
         if(SUCCEEDED(brt->CreateGradientStopCollection(gs,4,&st))){
            if(SUCCEEDED(brt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(P(32,32),P(0,0),32,32),st,&rb))){
               brt->FillEllipse(D2D1::Ellipse(P(32,32),32,32),rb); rb->Release();
            }
            st->Release();
         }
         brt->EndDraw(); brt->GetBitmap(&g_flameSpr[i]); brt->Release();
      }
   }
   auto spr=[&](int stage,float x,float y,float r,float op){
      if(r<0.5f||op<=0.f||!g_flameSpr[stage]) return;
      g_rt->DrawBitmap(g_flameSpr[stage],D2D1::RectF(x-r,y-r,x+r,y+r),std::min(1.f,op),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
   };
   auto frac=[](float v){ return v-std::floor(v); };
   auto hash=[&](int j,float k){ return frac(std::sin(j*12.9898f+k*78.233f)*43758.5453f); };
   const float PI=3.14159265f;
   float wind=std::sin(t*1.6f+0.7f)*0.16f+std::sin(t*3.1f)*0.05f;           // the whole fire sways a little
   // smoke
   for(int j=0;j<6;j++){
      float age=frac(t*0.33f+j/6.f);
      float x=cx+std::sin(t*0.9f+j*2.1f)*W*0.45f*age+wind*W*age*1.5f, y=by-H*(1.0f+age*0.75f);
      spr(7,x,y,W*(0.45f+0.6f*age),0.13f*std::sin(PI*age)*a);
   }
   // body of the fire: old (cool) particles first, young (hot) ones on top
   struct Pt{ float age,x,y,r,op; int st; };
   Pt pts[72]; int n=0; const int N=64;
   for(int j=0;j<N;j++){
      float age=frac(t*0.95f+(float)j/N+hash(j,1.f)*0.02f);
      float spread=(hash(j,2.f)-0.5f)*W*0.70f*(1.f-age*0.86f);
      float turb=std::sin(t*4.3f+j*2.3f+age*6.f)*W*0.20f*age+std::sin(t*9.7f+j*1.1f)*W*0.05f*age;
      float life=std::sin(PI*std::min(1.f,age*1.1f));
      pts[n++]={age, cx+spread+turb+wind*W*age*age*1.4f, by-H*1.02f*std::pow(age,0.82f),
                W*(0.20f+0.30f*life)*(1.f-age*0.45f)+1.f,
                0.80f*std::min(1.f,age*7.f)*std::pow(1.f-age,0.8f)*(0.9f+0.1f*fl), 1+std::min(5,(int)(age*5.4f))};
   }
   std::sort(pts,pts+n,[](const Pt& p,const Pt& q){ return p.age>q.age; });
   for(int i=0;i<n;i++) spr(pts[i].st,pts[i].x,pts[i].y,pts[i].r,pts[i].op*a);
   // white-hot core at the base
   spr(2,cx,by-H*0.14f,W*0.46f,0.70f*a);
   spr(1,cx+std::sin(t*9.f)*W*0.03f,by-H*0.12f,W*0.30f,0.85f*a);
   spr(0,cx,by-H*0.10f,W*0.16f,0.90f*a);
   // sparks
   for(int j=0;j<6;j++){
      float age=frac(t*0.55f+j*0.17f);
      float ex=cx+std::sin(j*2.7f+t*2.3f)*W*(0.2f+0.4f*age)+wind*W*age, ey=by-H*(0.25f+1.05f*age), er=(1.f-age)*2.1f+0.5f;
      ID2D1SolidColorBrush* eb=nullptr;
      if(SUCCEEDED(g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.80f-0.45f*age,0.18f,(1.f-age)*0.9f*a),&eb))){ g_rt->FillEllipse(D2D1::Ellipse(P(ex,ey),er,er),eb); eb->Release(); }
   }
}

// ============================================================================
// Auto-update (GitHub Releases), the same way as in Pasjans Dziadkowy - see update.h for the network,
// version and self-replace mechanics. The check runs on a background thread (a slow network never delays
// the start); only an explicit "Tak" downloads the new exe, which is then swapped in by a helper .bat.
// ============================================================================
static const UINT WM_UPDATE_CHECK_DONE    = WM_APP+1;
static const UINT WM_UPDATE_DOWNLOAD_DONE = WM_APP+2;
static bool g_updateBusy=false;
static DWORD WINAPI updateCheckThreadProc(LPVOID param){
   HWND hwnd=(HWND)param;
   update::ReleaseInfo* info=new update::ReleaseInfo(update::checkLatest());
   PostMessageW(hwnd,WM_UPDATE_CHECK_DONE,0,(LPARAM)info);
   return 0;
}
struct UpdateDownloadJob { HWND hwnd; std::wstring url, destPath; bool ok; };
static DWORD WINAPI updateDownloadThreadProc(LPVOID param){
   UpdateDownloadJob* job=(UpdateDownloadJob*)param;
   job->ok=update::httpDownload(job->url,job->destPath);
   PostMessageW(job->hwnd,WM_UPDATE_DOWNLOAD_DONE,0,(LPARAM)job);
   return 0;
}
static void startUpdateCheck(){
   if(g_updateBusy||!g_hwnd) return;
   g_updateBusy=true;
   HANDLE t=CreateThread(nullptr,0,updateCheckThreadProc,g_hwnd,0,nullptr);
   if(t) CloseHandle(t); else g_updateBusy=false;
}
static void beginUpdateDownload(HWND hwnd,const std::wstring& url){
   wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr,exePath,MAX_PATH);
   UpdateDownloadJob* job=new UpdateDownloadJob{hwnd,url,std::wstring(exePath)+L".new",false};
   HANDLE t=CreateThread(nullptr,0,updateDownloadThreadProc,job,0,nullptr);
   if(t) CloseHandle(t); else delete job;
}

// ============================================================================
// Rules window
// ============================================================================
enum { HK_H=1, HK_P=2, HK_B=3, HK_NOTE=4 };      // heading, paragraph, bullet (bold lead-in), small note
struct HelpItem{ int kind; const wchar_t* lead; const wchar_t* text; };
static const HelpItem HELP_DOC[]={
 {HK_H,nullptr,L"Cel gry"},
 {HK_P,nullptr,L"Jako pierwszy pozbądź się wszystkich swoich kart: z magazynu, z talii i ze śmietnika. Samo opróżnienie magazynu nie wystarcza."},

 {HK_H,nullptr,L"Rozkład kart"},
 {HK_P,nullptr,L"Każdy gracz ma własną talię 52 kart (Ty niebieską, komputer czerwoną)."},
 {HK_B,L"Magazyn",L"12 kart zakrytych i 1 odkryta na wierzchu."},
 {HK_B,L"Talia",L"35 kart. Dobierasz je po jednej."},
 {HK_B,L"Dobrana karta",L"karta odkryta z talii. Musisz ją zagrać albo odrzucić."},
 {HK_B,L"Śmietnik",L"odrzucone karty. Gdy talia się skończy, śmietnik staje się nową talią."},
 {HK_B,L"Kolumny",L"8 wspólnych kolumn (na początku po 4 karty od każdego gracza)."},
 {HK_B,L"Fundamenty",L"8 stosów budowanych od asa do króla w jednym kolorze. Mają zarezerwowane kolory, po dwa na kolor, w kolejności starszeństwa: pik, kier, karo, trefl. As zaczyna fundament swojego koloru."},

 {HK_H,nullptr,L"Dozwolone zagrania"},
 {HK_B,L"Na fundament",L"as, a potem kolejne karty tego samego koloru."},
 {HK_B,L"Na kolumnę",L"karta o jeden niższa, w innym kolorze (np. 6♥ na 7♣). Pusta kolumna przyjmie dowolną kartę."},
 {HK_B,L"Cały sekwens",L"karty przekłada się po jednej, ale gdy da się przenieść ułożony ciąg kolejnymi ruchami (wolne kolumny lub miejsca na innych kolumnach), możesz złapać jego pierwszą kartę. Gra sama wykona i pokaże wszystkie ruchy."},
 {HK_B,L"Na magazyn lub śmietnik przeciwnika",L"karta tego samego koloru o jeden wyższa lub niższa. Taka karta zasłania przeciwnikowi jego kartę."},

 {HK_H,nullptr,L"Ścisły przymus"},
 {HK_P,nullptr,L"Każdą kartę, którą możesz zagrać na fundament (wierzch magazynu, dobrana karta, wierzch śmietnika lub kolumny), musisz tam dołożyć."},
 {HK_P,nullptr,L"Kto zapomni i spróbuje dobrać kartę, odrzucić ją lub spasować, traci turę."},

 {HK_H,nullptr,L"Przebieg tury"},
 {HK_B,L"1.",L"Graj kartami z magazynu, śmietnika i kolumn, ile chcesz."},
 {HK_B,L"2.",L"Gdy nie możesz lub nie chcesz grać dalej, dobierz kartę z talii."},
 {HK_B,L"3.",L"Zagraj ją albo odrzuć na swój śmietnik. Odrzucenie kończy turę."},
 {HK_B,L"Pas",L"możliwy tylko wtedy, gdy talia i śmietnik są puste."},
 {HK_B,L"Płomień",L"płonie obok magazynu gracza, którego jest tura."},

 {HK_H,nullptr,L"Kto zaczyna"},
 {HK_P,nullptr,L"Zaczyna ten, kto ma starszą kartę w magazynie (as jest najmłodszy, król najstarszy)."},
 {HK_P,nullptr,L"Takie same figury: decyduje kolor (pik, kier, karo, trefl). Identyczne karty: każdy odkrywa pierwszą kartę z talii i ta rozstrzyga tak samo; jeśli znów są identyczne, odkrywane są kolejne. Odkryte karty wracają pod talie."},

 {HK_H,nullptr,L"Sterowanie myszą"},
 {HK_B,L"Kliknięcie karty",L"przenosi ją na najlepsze miejsce. Dobrana karta trafia na śmietnik tylko wtedy, gdy nie ma dla niej żadnego innego ruchu."},
 {HK_B,L"Przeciąganie",L"przenosi kartę lub cały sekwens tam, gdzie chcesz. Zielone ramki pokazują dozwolone miejsca."},

 {HK_H,nullptr,L"Skróty klawiszowe"},
 {HK_B,L"Spacja",L"dobierz kartę"},
 {HK_B,L"D",L"odrzuć dobraną kartę / pas"},
 {HK_B,L"H",L"podpowiedź (karta sama pokazuje ruch)"},
 {HK_B,L"U, Ctrl+Z, Backspace",L"cofnij"},
 {HK_B,L"F2",L"nowa gra"},
 {HK_B,L"M",L"dźwięk włączony / wyłączony"},

 {HK_H,nullptr,L"Cofanie i zapis gry"},
 {HK_P,nullptr,L"Cofnij cofa Twoją ostatnią czynność: ruch, dobranie, odrzucenie, a przeniesienie całego sekwensu jako jeden krok. Jeśli ta czynność skończyła turę, cofa też ruchy komputera wykonane od tamtej pory."},
 {HK_P,nullptr,L"Gra zapisuje się przy wyjściu i wczytuje przy następnym uruchomieniu."},

 {HK_H,nullptr,L"Komputer"},
 {HK_P,nullptr,L"Komputer ma trzy poziomy trudności: Łatwy, Normalny i Trudny. Gra według dziesięciu zasad opisanych w pliku AI_RULES.md."},
};
struct HelpBlock{ IDWriteTextLayout* lay=nullptr; int kind=0; float y=0,h=0; };
static std::vector<HelpBlock> g_helpBlocks;
static void helpRelease(){ for(auto& b:g_helpBlocks) if(b.lay) b.lay->Release(); g_helpBlocks.clear(); }
// panel (px..), header, and the scrolled view (vx..)
static void helpGeom(float& px,float& py,float& pw,float& ph,float& vx,float& vy,float& vw,float& vh){
   pw=std::min(840.f,G.w-30.f); ph=G.h-30.f; px=std::floor((G.w-pw)/2.f); py=15.f;
   vx=px+28.f; vy=py+82.f; vw=pw-28.f-40.f; vh=ph-82.f-46.f;
}
static void helpBuild(float width){
   helpRelease(); float y=0;
   for(const HelpItem& it:HELP_DOC){
      float px= it.kind==HK_H?22.f : it.kind==HK_NOTE?13.f : 16.f;
      float indent= it.kind==HK_B?24.f:0.f;
      std::wstring text= it.lead ? std::wstring(it.lead)+L" – "+it.text : std::wstring(it.text);
      IDWriteTextFormat* f=nullptr;
      g_dw->CreateTextFormat(L"Segoe UI",nullptr,it.kind==HK_H?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
         it.kind==HK_NOTE?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
      if(!f) continue;
      f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,px*1.45f,px*1.12f);
      IDWriteTextLayout* lay=nullptr;
      g_dw->CreateTextLayout(text.c_str(),(UINT32)text.size(),f,width-indent,100000.f,&lay);
      f->Release(); if(!lay) continue;
      if(it.lead){ DWRITE_TEXT_RANGE r={0,(UINT32)wcslen(it.lead)}; lay->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,r); }
      DWRITE_TEXT_METRICS m{}; lay->GetMetrics(&m);
      float before= it.kind==HK_H?(y==0?0.f:24.f) : it.kind==HK_B?5.f : 8.f;
      HelpBlock b; b.lay=lay; b.kind=it.kind; b.y=y+before; b.h=m.height;
      y=b.y+b.h+(it.kind==HK_H?12.f:0.f);
      g_helpBlocks.push_back(b);
   }
   g_help.contentH=y+12.f; g_help.builtW=width;
}
static void helpClamp(float vh){ g_help.scroll=std::max(0.f,std::min(g_help.scroll,std::max(0.f,g_help.contentH-vh))); }
// scrollbar thumb: top and height
static void helpThumb(float vy,float vh,float& ty,float& th){
   float maxS=std::max(1.f,g_help.contentH-vh);
   th=std::max(40.f,vh*vh/std::max(vh,g_help.contentH));
   ty=vy+(vh-th)*(g_help.scroll/maxS);
}
static void helpDraw(){
   if(!g_help.open||!g_rt||!g_dw) return;
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   if(g_help.builtW!=vw) helpBuild(vw);
   helpClamp(vh);
   rrect(0,0,G.w,G.h,0,0,0,0,0.65f);                                   // dim the table
   rrect(px+5,py+8,pw,ph,16,0,0,0,0.40f);                              // shadow
   rrect(px,py,pw,ph,16,0.05f,0.16f,0.10f,0.99f);                      // panel (dark felt)
   rrect(px,py,pw,ph,16,0.95f,0.80f,0.30f,0.85f,false,2.f);            // gold border
   txt(L"Garibaldka",px+28,py+12,pw-120,40,30,1.f,0.86f,0.25f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   txt(std::wstring(L"Wersja ")+APP_VERSION+L"  ·  zbudowana "+buildDateText(),px+29,py+48,pw-120,22,15,0.82f,0.90f,0.84f,0.9f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   rrect(px+24,py+76,pw-48,1.5f,0,1,1,1,0.20f);                        // rule under the header
   // close button (an X made of two lines)
   float cbx=px+pw-52, cby=py+16;
   rrect(cbx,cby,34,34,8,1,1,1,0.14f); rrect(cbx,cby,34,34,8,1,1,1,0.35f,false,1.f);
   g_ren.drawLine(cbx+11,cby+11,cbx+23,cby+23,2.2f,255,255,255,230);
   g_ren.drawLine(cbx+23,cby+11,cbx+11,cby+23,2.2f,255,255,255,230);
   // scrolled content
   ID2D1SolidColorBrush *bBody=nullptr,*bGold=nullptr,*bDim=nullptr,*bRule=nullptr;
   g_rt->CreateSolidColorBrush(D2D1::ColorF(0.93f,0.96f,0.93f,1.f),&bBody);
   g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.86f,0.30f,1.f),&bGold);
   g_rt->CreateSolidColorBrush(D2D1::ColorF(0.72f,0.80f,0.74f,1.f),&bDim);
   g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.86f,0.30f,0.35f),&bRule);
   g_rt->PushAxisAlignedClip(D2D1::RectF(vx-4,vy,vx+vw+8,vy+vh),D2D1_ANTIALIAS_MODE_ALIASED);
   for(const HelpBlock& b:g_helpBlocks){
      float top=vy+b.y-g_help.scroll;
      if(top+b.h<vy-4||top>vy+vh+4) continue;
      ID2D1SolidColorBrush* br= b.kind==HK_H?bGold : b.kind==HK_NOTE?bDim : bBody;
      if(b.kind==HK_B){                                                  // bullet
         D2D1_ELLIPSE e=D2D1::Ellipse(D2D1::Point2F(vx+8.f,top+11.f),3.2f,3.2f);
         if(bGold) g_rt->FillEllipse(e,bGold);
         if(br) g_rt->DrawTextLayout(D2D1::Point2F(vx+24.f,top),b.lay,br);
      } else {
         if(br) g_rt->DrawTextLayout(D2D1::Point2F(vx,top),b.lay,br);
         if(b.kind==HK_H && bRule) g_rt->DrawLine(D2D1::Point2F(vx,top+b.h+4.f),D2D1::Point2F(vx+vw,top+b.h+4.f),bRule,1.2f);
      }
   }
   g_rt->PopAxisAlignedClip();
   for(auto* b:{bBody,bGold,bDim,bRule}) if(b) b->Release();
   // scrollbar
   if(g_help.contentH>vh+1){
      float sx=px+pw-30, ty,th; helpThumb(vy,vh,ty,th);
      rrect(sx,vy,8,vh,4,1,1,1,0.10f);
      rrect(sx,ty,8,th,4,1.f,0.86f,0.30f,g_help.dragThumb?0.95f:0.70f);
   }
   // updates: a check box (check at start)
   float fy=py+ph-38;
   rrect(px+24,fy-8,pw-48,1.5f,0,1,1,1,0.20f);
   rrect(px+26,fy+4,17,17,4,1,1,1,0.14f); rrect(px+26,fy+4,17,17,4,1,1,1,0.45f,false,1.2f);
   if(g_checkUpdates){ g_ren.drawLine(px+30,fy+13,px+34,fy+17,2.4f,255,220,80,255); g_ren.drawLine(px+34,fy+17,px+40,fy+8,2.4f,255,220,80,255); }
   txt(L"Sprawdzaj aktualizacje przy starcie",px+52,fy,320,26,14,0.90f,0.95f,0.90f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
}
// mouse handling of the rules window (all clicks are consumed while it is open)
static void helpMouseDown(float mx,float my){
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   float cbx=px+pw-52, cby=py+16;
   if(mx>=cbx&&mx<=cbx+34&&my>=cby&&my<=cby+34){ showRules(); return; }
   if(mx<px||mx>px+pw||my<py||my>py+ph){ showRules(); return; }          // a click outside the panel closes it
   float fy=py+ph-38;
   if(my>=fy&&my<=fy+26){
      if(mx>=px+24&&mx<=px+24+360){ g_checkUpdates=!g_checkUpdates; saveSettings(); g_dirty=true; return; }
   }
   if(g_help.contentH>vh+1){
      float sx=px+pw-30, ty,th; helpThumb(vy,vh,ty,th);
      if(mx>=sx-6&&mx<=sx+14&&my>=vy&&my<=vy+vh){
         if(my>=ty&&my<=ty+th){ g_help.dragThumb=true; g_help.grabDy=my-ty; SetCapture(g_hwnd); }
         else { g_help.scroll+= (my<ty?-1.f:1.f)*vh*0.9f; helpClamp(vh); }    // a click on the track: page up / down
         g_dirty=true;
      }
   }
}
static void helpMouseMove(float,float my){
   if(!g_help.dragThumb) return;
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   float ty,th; helpThumb(vy,vh,ty,th);
   float track=vh-th; if(track<1.f) return;
   g_help.scroll=((my-g_help.grabDy-vy)/track)*(g_help.contentH-vh);
   helpClamp(vh); g_dirty=true;
}
static void helpMouseUp(){ if(g_help.dragThumb){ g_help.dragThumb=false; ReleaseCapture(); g_dirty=true; } }
static void helpKey(WPARAM k){
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   switch(k){
   case VK_ESCAPE: case VK_F1: showRules(); return;
   case VK_UP:    g_help.scroll-=46; break;
   case VK_DOWN:  g_help.scroll+=46; break;
   case VK_PRIOR: g_help.scroll-=vh*0.9f; break;
   case VK_NEXT:  g_help.scroll+=vh*0.9f; break;
   case VK_HOME:  g_help.scroll=0; break;
   case VK_END:   g_help.scroll=1e9f; break;
   }
   helpClamp(vh); g_dirty=true;
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
   drawFlame(now);

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
   helpDraw();                                        // the rules window is on top of everything
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
   if(g_help.open){ helpMouseDown((float)mx,(float)my); return; }
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
   if(g_help.open){ helpMouseMove((float)mx,(float)my); return; }
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
   if(g_help.open){ helpMouseUp(); return; }
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
      g_help.builtW=-1; relayout(true); g_dirty=true; return 0;}
   case WM_GETMINMAXINFO:{ auto* m=(MINMAXINFO*)lp; m->ptMinTrackSize.x=820; m->ptMinTrackSize.y=620; return 0;}
   case WM_PAINT:{ PAINTSTRUCT ps; BeginPaint(hwnd,&ps); render(); EndPaint(hwnd,&ps); return 0;}
   case WM_ERASEBKGND: return 1;
   case WM_LBUTTONDOWN: onLDown(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_LBUTTONUP:   onLUp(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_LBUTTONDBLCLK: return 0;                       // ignored: a double click must not move two cards
   case WM_MOUSEMOVE:   onMouseMove(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)); return 0;
   case WM_MOUSEWHEEL:
      if(g_help.open){ g_help.scroll-=(float)GET_WHEEL_DELTA_WPARAM(wp)/120.f*70.f; float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh); helpClamp(vh); g_dirty=true; }
      return 0;
   case WM_KEYDOWN:
      if(g_help.open){ helpKey(wp); return 0; }
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
   case WM_UPDATE_CHECK_DONE:{
      std::unique_ptr<update::ReleaseInfo> info((update::ReleaseInfo*)lp);
      g_updateBusy=false; g_dirty=true;
      if(info->ok && update::isNewer(info->tag,APP_VERSION)){
         std::wstring m=L"Dostępna nowsza wersja gry: "+info->tag+L" (masz "+std::wstring(APP_VERSION)+L").\n\nZaktualizować teraz?";
         if(!info->notes.empty()){
            std::wstring notes=info->notes;
            if(notes.size()>500) notes=notes.substr(0,500)+L"…";
            m+=L"\n\nCo nowego:\n"+notes;
         }
         if(MessageBoxW(hwnd,m.c_str(),L"Aktualizacja dostępna",MB_YESNO|MB_ICONINFORMATION)==IDYES)
            beginUpdateDownload(hwnd,info->downloadUrl);
      }
      return 0;}
   case WM_UPDATE_DOWNLOAD_DONE:{
      std::unique_ptr<UpdateDownloadJob> job((UpdateDownloadJob*)lp);
      if(!job->ok){
         MessageBoxW(hwnd,L"Nie udało się pobrać aktualizacji. Spróbuj ponownie później.",L"Aktualizacja",MB_OK|MB_ICONERROR);
         DeleteFileW(job->destPath.c_str());
         return 0;
      }
      wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr,exePath,MAX_PATH);
      if(update::launchSelfUpdate(job->destPath,exePath)){
         DestroyWindow(hwnd);            // the game is saved on exit; the helper swaps the exe and starts it again
      } else {
         MessageBoxW(hwnd,L"Pobrano aktualizację, ale nie udało się jej zainstalować automatycznie.",L"Aktualizacja",MB_OK|MB_ICONERROR);
      }
      return 0;}
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
   std::wstring title=std::wstring(L"Garibaldka ")+APP_VERSION;
   HWND hwnd=CreateWindowExW(0,L"GaribaldkaWnd",title.c_str(),WS_OVERLAPPEDWINDOW,
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
   if(g_checkUpdates) startUpdateCheck();

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
      bool flameBurning=g_flame.lit||g_flame.scale>0.01f||g_flame.igniteAt>0;
      bool anim=anyAnimating(now)||g_plan.active||g_dragging||g_prev.active||g_start.active||g_rev.active||g_deal.active||g_fw.active()||(g_game.over&&g_overShown&&now-g_overAt<1.0);
      if(anim||g_dirty){ render(); if(!anim) continue; Sleep(1); }
      else if(flameBurning){ render(); MsgWaitForMultipleObjects(0,nullptr,FALSE,12,QS_ALLINPUT); }   // only the flame moves: ~60 fps without a busy loop
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
