// Garibaldka (Russian Bank / crapette) for Windows - player vs computer.
// C++17 / Win32 / Direct2D + DirectWrite + WIC (graphics), DirectSound (audio).
// Card images, sounds, renderer, fireworks and sound system come from the
// "Pasjans Dziadkowy" project (MIT, see LICENSE).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include "game.h"
#include "net.h"
#include <deque>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
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
#include "keys.h"
#include "fluidflame.h"

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
static bool  g_hot=false;          // hot seat: two people at one computer, the player whose turn it is plays his side
static int   g_gameLevel=1;        // the level this game was started with (for the statistics)
static bool  g_autoMoves=true;     // a click on a card moves it to its best place
static int   g_forceMode=0;        // foundation obligation: 0 = punish (the turn is lost), 1 = remind (the other action is blocked)
static int   g_setGroup=0;         // the group last selected in the settings window
static int   actor(){ return g_hot?g_game.turn:0; }       // the player at the controls
static std::wstring hotName(int p){ return p==0?L"Gracz 1":L"Gracz 2"; }
// Version of this build. A release on GitHub is tagged vMAJOR.MINOR.PATCH with the same number and carries
// an asset called Garibaldi.exe: the updater (update.h) compares the tag with this number.
static const wchar_t* APP_VERSION = L"1.2.0";
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

// ---------------------------------------------------------------------------
// Network play (see net.h). Both computers run the same deterministic game from the same seed and send only the
// player's ACTIONS. Each computer shows itself as player 0 (bottom row): the guest plays a mirrored copy of the
// game (Game::mirrored()), so piles are translated with Game::mirrorPile() when actions are sent/received.
// ---------------------------------------------------------------------------
static std::wstring g_instTag;                       // ".i2" for a second copy on the same computer (testing)
struct NetState{
   bool listening=false, connecting=false, connected=false, playing=false, host=false, rematchAsked=false;
   bool online=false, pendingCreate=false;           // online: through the server (rooms), otherwise a direct link
   std::string pendingCode, roomCode; int gameNo=0;  // gameNo: counts the games of this meeting (for the result report)
   int h2hW=0,h2hL=0,h2hD=0; bool hasH2h=false;     // my record against this opponent (from the server)
   std::wstring peerNick; std::string peerVer;
   std::deque<std::string> inbox;                    // the opponent's actions waiting to be played (animated one by one)
};
static NetState g_net;
static net::Conn g_conn;                             // direct link (local network)
static net::WsConn g_ws;                             // link to the online server
static net::Link* g_link=&g_conn;                    // the one in use
// Where the online server lives. Empty = the player has to type it in the network panel.
static const wchar_t* DEFAULT_SERVER=L"https://garibaldka.garibaldka-server.workers.dev";
// The password of the friends-only server is filled in for the player (it is in the source, so the server is only as private as that).
static const wchar_t* DEFAULT_INVITE=L"uppbskah";
static std::wstring g_serverW, g_inviteW, g_codeW;
static std::string g_secret;                         // the player's secret key: it proves that a nick is his (kept in the .ini)
static std::wstring g_nickW=L"Gracz", g_ipW;
static std::wstring oppName(){ return g_net.playing ? g_net.peerNick : std::wstring(L"Komputer"); }
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
static FluidFlame g_fire;                   // the flame: a small fluid simulation (see fluidflame.h)
static std::vector<uint32_t> g_fireImg;
static ID2D1Bitmap* g_fireBmp=nullptr;
static double g_fireAcc=0;
static std::wstring g_status; static bool g_statusErr=false; static double g_statusUntil=0;
static double g_dealUntil=0, g_aiAt=0, g_fwLast=0, g_overAt=0;
static bool   g_overShown=false, g_dirty=true;
static int    g_hoverBtn=-1;
static std::string g_log;               // move log (shown in the F9 dump)
static void logf(const char* who,int a=-1,int b=-1){ char t[96]; sprintf(t,"%.2f %s %d->%d turn=%d\n",nowSec()-0,who,a,b,g_game.turn); g_log+=t; }

// ---------------------------------------------------------------------------
// Permanent record of every game and every action, for analysing how sensible the moves were
// (file garibaldka_ruchy.log next to the program). One line per action: who did what, which moves were legal at that
// moment, and the whole game state before the action (so any position can be reloaded and examined).
// ---------------------------------------------------------------------------
static std::string recPile(int id){
   static const char* N[]={"res","hand","turned","waste"};
   char b[24];
   switch(ptype(id)){
      case PT_TAB: sprintf(b,"col%d",id-8+1); break;
      case PT_FND: sprintf(b,"fnd%d",id-16+1); break;
      default: sprintf(b,"%s.%s",N[id/2],id%2?"opp":"me"); break;
   }
   return b;
}
static std::string recCard(const Card& c){ return c.imgKey()+(c.deck?"'":""); }
static void recWrite(const std::string& line){
   static std::wstring path;
   if(path.empty()){ wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH); path=b; size_t k=path.find_last_of(L"\\/"); path=path.substr(0,k+1)+L"garibaldka_ruchy"+g_instTag+L".log"; }
   FILE* f=_wfopen(path.c_str(),L"ab"); if(!f) return;
   fwrite(line.data(),1,line.size(),f);
   long sz=ftell(f); fclose(f);
   if(sz>12*1024*1024){ std::wstring old=path+L".old"; DeleteFileW(old.c_str()); MoveFileW(path.c_str(),old.c_str()); }   // the log stays bounded
}
static std::string recStamp(){ time_t t=time(nullptr); char b[32]; strftime(b,sizeof b,"%Y-%m-%d %H:%M:%S",localtime(&t)); return b; }
static void recGameStart(bool network){
   char b[256]; sprintf(b,"\nGAME %s app=%ls seed=%u mode=%s level=%d startHow=%d\n",recStamp().c_str(),APP_VERSION,(unsigned)g_game.seed,network?"network":"computer",g_level,g_game.startHow);
   recWrite(b);
}
static void recGameEnd(){
   char b[160]; sprintf(b,"END %s winner=%d(0=me,1=opp,-1=draw) turns=%d left_me=%d left_opp=%d\n",recStamp().c_str(),g_game.winner,g_game.totalTurns,g_game.remaining(0),g_game.remaining(1));
   recWrite(b);
}
// who: 'H' human, 'A' computer, 'N' the opponent over the network. `g` = state BEFORE the action, p = the acting player.
static void recAction(const Game& g,char who,int p,StepKind kind,int src=-1,int dst=-1){
   std::string a;
   switch(kind){
      case ST_MOVE:{ const Card* c=g.srcTop(src,p); a="MOVE "+(c?recCard(*c):std::string("?"))+" "+recPile(src)+"->"+recPile(dst); break; }
      case ST_DRAW:    a="DRAW"; break;
      case ST_DISCARD:{ const Card* c=g.top(turnedId(p)); a="DISCARD "+(c?recCard(*c):std::string("?")); break; }
      default:         a="PASS"; break;
   }
   // everything that was possible (the turned card, magazine, waste, the column tops)
   std::string legal; int nLegal=0, nTabMoves=0, empties=0;
   for(int j=0;j<NUM_TAB;j++) if(g.pile[tabId(j)].empty()) empties++;
   int srcs[3+NUM_TAB]={resId(p),turnedId(p),wasteId(p)}; for(int j=0;j<NUM_TAB;j++) srcs[3+j]=tabId(j);
   for(int sId:srcs){
      const Card* c=g.srcTop(sId,p); if(!c) continue;
      for(int d=0;d<NP;d++) if(g.canMove(sId,d,p)){
         nLegal++; if(ptype(d)==PT_TAB||ptype(d)==PT_FND) nTabMoves++;
         if(nLegal<=24){ legal+=" "+recCard(*c)+":"+recPile(sId)+">"+recPile(d); }
      }
   }
   std::string flags;
   if(kind==ST_DISCARD){
      const Card* c=g.top(turnedId(p));
      bool fits=false; for(int d=0;c&&d<NP;d++) if(g.canMove(turnedId(p),d,p)) fits=true;
      if(fits) flags+=" CHECK:discard-although-the-turned-card-could-be-played";
      if(empties>0) flags+=" CHECK:discard-with-free-column";
   }
   if(kind==ST_DRAW||kind==ST_DISCARD||kind==ST_PASS){ Move m; if(g.mandatory(p,m)) flags+=" CHECK:foundation-move-left"; }
   char head[160]; sprintf(head,"T%d %c%d level=%d free_cols=%d legal=%d %s",g.totalTurns,who,p,who=='A'?g_level:-1,empties,nLegal,a.c_str());
   recWrite(std::string(head)+flags+"\n   legal:"+legal+"\n   S "+g.serialize()+"\n");
}

enum { B_NEW,B_UNDO,B_HINT,B_NET,B_HOT,B_CHAT,B_STATS,B_SETTINGS,B_RULES,B_COUNT };
struct Btn{ float x,y,w,h; };
static Btn g_btn[B_COUNT];

static void setStatus(const std::wstring& s,bool err=false,double secs=0){
   g_status=s; g_statusErr=err; g_statusUntil=secs>0?nowSec()+secs:0; g_dirty=true;
}
// A sequence being moved card by card (each single move is animated).
struct SeqPlan{ bool active=false; std::vector<Move> moves; size_t next=0; double at=0; };
static SeqPlan g_plan;
static bool humanTurn(){ return !g_game.over && (g_hot||g_game.turn==0) && nowSec()>=g_dealUntil && !g_plan.active; }

static void statusForTurn(){
   if(g_game.over) return;
   if(g_hot){
      std::wstring w=hotName(g_game.turn)+L": ";
      if(!g_game.pile[turnedId(g_game.turn)].empty()) setStatus(w+L"zagraj dobraną kartę albo odrzuć ją na śmietnik (kończy turę).");
      else setStatus(w+L"zagraj karty lub dobierz z talii.");
      return;
   }
   if(g_game.turn==1){ setStatus(oppName()+L" gra…"); return; }
   if(!g_game.pile[turnedId(0)].empty()) setStatus(L"Zagraj dobraną kartę albo odrzuć ją na śmietnik (kończy turę).");
   else setStatus(L"Twój ruch: zagraj karty lub dobierz z talii.");
}

static std::wstring iniPath(){
   wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH);
   std::wstring s=b; size_t d=s.find_last_of(L'.'); if(d!=std::wstring::npos) s.resize(d);
   return s+g_instTag+L".ini";
}
// Statistics: finished games, wins and draws, per computer level and for network games.
struct StatRow{ int games=0, wins=0, draws=0; };
static StatRow g_stats[4];                                   // 0..2 = computer: easy / normal / hard, 3 = network
static const wchar_t* STAT_KEYS[4]={L"Easy",L"Normal",L"Hard",L"Network"};
static void statsLoad(const std::wstring& ini){
   for(int i=0;i<4;i++){
      std::wstring k=STAT_KEYS[i];
      g_stats[i].games=(int)GetPrivateProfileIntW(L"Stats",(k+L"_Games").c_str(),0,ini.c_str());
      g_stats[i].wins =(int)GetPrivateProfileIntW(L"Stats",(k+L"_Wins").c_str(),0,ini.c_str());
      g_stats[i].draws=(int)GetPrivateProfileIntW(L"Stats",(k+L"_Draws").c_str(),0,ini.c_str());
   }
}
static void statsSave(const std::wstring& ini){
   for(int i=0;i<4;i++){
      std::wstring k=STAT_KEYS[i]; wchar_t b[16];
      wsprintfW(b,L"%d",g_stats[i].games); WritePrivateProfileStringW(L"Stats",(k+L"_Games").c_str(),b,ini.c_str());
      wsprintfW(b,L"%d",g_stats[i].wins);  WritePrivateProfileStringW(L"Stats",(k+L"_Wins").c_str(),b,ini.c_str());
      wsprintfW(b,L"%d",g_stats[i].draws); WritePrivateProfileStringW(L"Stats",(k+L"_Draws").c_str(),b,ini.c_str());
   }
}
static std::wstring iniPath();
static void saveSettings(){
   auto ini=iniPath(); wchar_t b[32];
   wsprintfW(b,L"%d",g_level); WritePrivateProfileStringW(L"Settings",L"Level",b,ini.c_str());
   wsprintfW(b,L"%d",g_muted?1:0); WritePrivateProfileStringW(L"Settings",L"Muted",b,ini.c_str());
   wsprintfW(b,L"%d",g_volPct); WritePrivateProfileStringW(L"Settings",L"Volume",b,ini.c_str());
   WritePrivateProfileStringW(L"Settings",L"CheckUpdatesOnStart",g_checkUpdates?L"1":L"0",ini.c_str());
   WritePrivateProfileStringW(L"Network",L"Nick",g_nickW.c_str(),ini.c_str());
   WritePrivateProfileStringW(L"Network",L"JoinAddress",g_ipW.c_str(),ini.c_str());
   WritePrivateProfileStringW(L"Network",L"Server",g_serverW.c_str(),ini.c_str());
   WritePrivateProfileStringW(L"Network",L"Invite",g_inviteW.c_str(),ini.c_str());
   WritePrivateProfileStringW(L"Network",L"Secret",net::fromUtf8(g_secret).c_str(),ini.c_str());
   wsprintfW(b,L"%d",g_autoMoves?1:0); WritePrivateProfileStringW(L"Settings",L"AutoMoves",b,ini.c_str());
   wsprintfW(b,L"%d",g_forceMode);     WritePrivateProfileStringW(L"Settings",L"ForceObligation",b,ini.c_str());
   wsprintfW(b,L"%d",g_setGroup);      WritePrivateProfileStringW(L"Settings",L"LastGroup",b,ini.c_str());
   keysSave(ini); statsSave(ini);
   for(int i=0;i<SOUND_COUNT;i++){                         // own sounds (like in Pasjans Dziadkowy)
      std::wstring key=std::wstring(L"Sound_")+std::to_wstring(i);
      WritePrivateProfileStringW(L"Sounds",key.c_str(),SoundSystem::instance().customPath(i).c_str(),ini.c_str());
      WritePrivateProfileStringW(L"Sounds",(key+L"_Muted").c_str(),SoundSystem::instance().isMuted(i)?L"1":L"0",ini.c_str());
   }
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
   g_autoMoves=GetPrivateProfileIntW(L"Settings",L"AutoMoves",1,ini.c_str())!=0;
   g_forceMode=GetPrivateProfileIntW(L"Settings",L"ForceObligation",0,ini.c_str())==1?1:0;
   g_setGroup=(int)GetPrivateProfileIntW(L"Settings",L"LastGroup",0,ini.c_str()); if(g_setGroup<0||g_setGroup>4) g_setGroup=0;
   g_hot=GetPrivateProfileIntW(L"Game",L"HotSeat",0,ini.c_str())!=0;                      // of the saved game
   g_gameLevel=(int)GetPrivateProfileIntW(L"Game",L"GameLevel",g_level,ini.c_str()); if(g_gameLevel<0||g_gameLevel>2) g_gameLevel=g_level;
   keysLoad(ini); statsLoad(ini);
   { wchar_t b[128]={}; GetPrivateProfileStringW(L"Network",L"Nick",L"Gracz",b,128,ini.c_str()); g_nickW=b; if(g_nickW.empty()) g_nickW=L"Gracz";
     GetPrivateProfileStringW(L"Network",L"JoinAddress",L"",b,128,ini.c_str()); g_ipW=b;
     GetPrivateProfileStringW(L"Network",L"Server",DEFAULT_SERVER,b,128,ini.c_str()); g_serverW=b;
     GetPrivateProfileStringW(L"Network",L"Invite",L"",b,128,ini.c_str()); g_inviteW=b; if(g_inviteW.empty()) g_inviteW=DEFAULT_INVITE;
     GetPrivateProfileStringW(L"Network",L"Secret",L"",b,128,ini.c_str()); g_secret=net::toUtf8(b);
     if(g_secret.size()<16){                           // first run: make the key that proves that the nick is mine
        std::random_device rd; char h[16]; g_secret.clear();
        for(int i=0;i<4;i++){ sprintf(h,"%08x",(unsigned)rd()); g_secret+=h; }
        WritePrivateProfileStringW(L"Network",L"Secret",net::fromUtf8(g_secret).c_str(),ini.c_str());
     } }
}
static void snd(const char* k){ playSound(k,g_volPct/100.f); }
static void applySoundSettings(){                          // own sounds and muted events from the .ini
   auto ini=iniPath();
   for(int i=0;i<SOUND_COUNT;i++){
      std::wstring key=std::wstring(L"Sound_")+std::to_wstring(i); wchar_t b[MAX_PATH]={};
      GetPrivateProfileStringW(L"Sounds",key.c_str(),L"",b,MAX_PATH,ini.c_str());
      if(b[0] && GetFileAttributesW(b)!=INVALID_FILE_ATTRIBUTES) SoundSystem::instance().setCustomPath(i,b);
      SoundSystem::instance().setMuted(i,GetPrivateProfileIntW(L"Sounds",(key+L"_Muted").c_str(),0,ini.c_str())!=0);
   }
}

// ============================================================================
// Game flow
// ============================================================================
static bool uiBusy(double now){ return anyAnimating(now)||g_prev.active||g_start.active||g_rev.active||g_deal.active; }

// Online: tell the server how this game ended for me (it counts a result only when both players report the same).
static void netReportResult(){
   if(!g_net.playing||!g_net.online) return;
   char b[48]; sprintf(b,"#RESULT %d %c",g_net.gameNo,g_game.winner==0?'W':g_game.winner==1?'L':'D');
   g_ws.sendLine(b);
}
static void onGameOver(){
   if(g_overShown) return;
   g_overShown=true; g_overAt=nowSec(); g_prev.active=false; g_start.active=false;
   SoundSystem::instance().fadeOutAll(200);
   bool all=g_game.winner>=0 && g_game.remaining(g_game.winner)==0;      // false = decided by the turn limit
   if(!g_hot){                                                            // statistics (hot seat games are not counted)
      StatRow& st=g_stats[g_net.playing?3:g_gameLevel];
      st.games++; if(g_game.winner==0) st.wins++; else if(g_game.winner<0) st.draws++;
      saveSettings();
   }
   if(g_hot && g_game.winner>=0){ snd("sukces"); g_fw.start((int)G.w,(int)G.h); g_fwLast=nowSec();
      setStatus(hotName(g_game.winner)+(all?L" wygrywa: pozbył się wszystkich kart!":L" wygrywa: po 400 turach ma mniej kart do zagrania."));
   }
   else if(g_game.winner==0){ snd("sukces"); g_fw.start((int)G.w,(int)G.h); g_fwLast=nowSec();
      setStatus(all?L"Wygrywasz! Pozbyłeś się wszystkich kart.":L"Wygrywasz! Po 400 turach masz mniej kart do zagrania."); }
   else if(g_game.winner==1){ snd("koniec");
      setStatus(all?oppName()+L" pozbył się wszystkich kart. Przegrana.":L"Po 400 turach "+oppName()+L" ma mniej kart do zagrania. Przegrana."); }
   else { snd("koniec"); setStatus(L"Remis: nikt nie może już zagrać."); }
   netReportResult(); recGameEnd();
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
   return s+g_instTag+L".sav";
}
static void saveGame(){
   auto path=savePath();
   if(g_net.playing) return;                                 // a network game is not saved
   if(g_game.over){ DeleteFileW(path.c_str()); return; }   // a finished game is not kept
   std::string text=g_game.serialize();
   FILE* f=_wfopen(path.c_str(),L"wb"); if(!f) return;
   fwrite(text.data(),1,text.size(),f); fclose(f);
   static int lastHot=-1, lastLv=-1;                         // hot seat / level of the saved game (kept in the .ini)
   if(lastHot!=(int)g_hot||lastLv!=g_gameLevel){
      lastHot=(int)g_hot; lastLv=g_gameLevel; auto ini=iniPath(); wchar_t b[8];
      WritePrivateProfileStringW(L"Game",L"HotSeat",g_hot?L"1":L"0",ini.c_str());
      wsprintfW(b,L"%d",g_gameLevel); WritePrivateProfileStringW(L"Game",L"GameLevel",b,ini.c_str());
   }
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
static void newGameStart(bool network=false,uint32_t netSeed=0){
   SoundSystem::instance().fadeOutAll(150);
   g_fw.stop(); g_overShown=false; g_prev.active=false; g_start.active=false; g_rev.active=false; g_deal.active=false;
   g_drag=decltype(g_drag)(); g_dragging=false; g_plan.active=false; g_hist.clear();
   if(network){                                     // both computers deal the same game from the same seed
      g_game.newGame(netSeed,false);
      if(!g_net.host) g_game=g_game.mirrored();      // the guest sees itself at the bottom
   } else g_game.newGame(g_forceTie);
   recGameStart(network);
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
   g_fire.clear(); g_flame=Flame(); g_flame.igniteAt=g_start.t0+START_DUR;     // the flame lights up when the winning card has landed
   snd("nowa");
   g_ctx=AIContext(); g_gameLevel=g_level;
   const bool me=g_game.turn==0;
   switch(g_game.startHow){
   case Game::SH_MAG_SUIT:
      setStatus(me?std::wstring(L"Takie same figury w magazynach: zaczynasz, bo Twój kolor jest starszy (pik, kier, karo, trefl).")
                  :std::wstring(L"Takie same figury w magazynach: ")+oppName()+L" zaczyna, bo jego kolor jest starszy (pik, kier, karo, trefl)."); break;
   case Game::SH_HAND_RANK: case Game::SH_HAND_SUIT:
      setStatus(me?std::wstring(L"Identyczne karty w magazynach: rozstrzygnęły karty odkryte z talii. Zaczynasz.")
                  :std::wstring(L"Identyczne karty w magazynach: rozstrzygnęły karty odkryte z talii. Zaczyna ")+oppName()+L"."); break;
   default:
      setStatus(me?std::wstring(L"Zaczynasz: masz starszą kartę w magazynie."):oppName()+L" zaczyna: ma starszą kartę w magazynie.");
   }
   if(g_hot) setStatus(hotName(g_game.turn)+L" zaczyna (starsza karta w magazynie albo rozstrzygnięcie według zasad).");
   if(!me) g_aiAt=g_dealUntil+0.4;
}
// Restores the game saved on exit. Returns false when there is no valid save.
static bool loadSavedGame(){
   if(!readSavedGame()) return false;
   g_fw.stop(); g_overShown=false; g_prev.active=false; g_start.active=false; g_rev.active=false; g_deal.active=false; g_drag=decltype(g_drag)(); g_dragging=false; g_plan.active=false; g_hist.clear();
   for(Vis& v:V) v=Vis();
   relayout(true);
   g_dealUntil=0; g_ctx=AIContext();
   g_fire.clear(); g_flame=Flame(); g_flame.igniteAt=nowSec();
   if(g_game.turn==1) g_aiAt=nowSec()+0.8;
   setStatus(L"Wczytano ostatnią grę.",false,3.5);
   return true;
}

// ============================================================================
// Network play: protocol and the opponent's actions
// ----------------------------------------------------------------------------
// Lines (UTF-8): HELLO <version> <nick> | START <seed> | M <src> <dst> | D | X <hash> | P <hash> | F <hash> |
//                CHAT <text> | EMO <n> | REMATCH | REMATCH_OK | REMATCH_NO | BYE
//   M move, D draw, X discard (turn ends), P pass (turn ends), F forgot the foundation obligation (turn lost).
//   The three turn-ending actions carry, in the SAME line, the fingerprint of the whole game after the action
//   ("X <hash>"); the other side applies the action and compares at once, before anybody can move.
// Pile ids in M are those of the SENDER; the receiver translates them with Game::mirrorPile().
// ============================================================================
struct ChatLine{ std::wstring who, text; bool mine; };
static std::vector<ChatLine> g_chatLog;
struct ChatUi{ bool open=false; std::wstring input; int unread=0; };
static ChatUi g_chat;
struct EmoteShow{ int idx; int who; double t0; };
static std::vector<EmoteShow> g_emotes;
static const int NUM_EMOTES=8;
static const wchar_t* EMOJIS[NUM_EMOTES]={L"\U0001F44D",L"\U0001F600",L"\U0001F62E",L"\U0001F622",L"\U0001F621",L"\U0001F525",L"\U0001F44F",L"\U0001F914"};

struct NetPanel{ bool open=false; int focus=0; int tab=1; std::wstring info; };
static NetPanel g_np;
static std::vector<std::string> g_myAddrs;

static uint64_t netStateHash(){ return g_net.host ? g_game.hash() : g_game.mirrored().hash(); }   // always of the CANONICAL (host's) view
static void netSend(const std::string& s){ if(g_net.connected) g_link->sendLine(s); }     // a game message to the opponent
static void serverSend(const std::string& s){ g_ws.sendLine(s); }                       // a command to the online server
// Called right after the player's own action; `turnEnded` also sends the fingerprint of the resulting game.
static void netLocal(const std::string& line,bool turnEnded){
   if(!g_net.playing) return;
   if(turnEnded){ char b[48]; sprintf(b," %llx",(unsigned long long)netStateHash()); netSend(line+b); }
   else netSend(line);
}
static bool netBusy(){ return g_net.listening||g_net.connecting||g_net.connected||g_net.playing; }
static void netReset(){ g_conn.close(); g_ws.close(); g_link=&g_conn; g_net=NetState(); g_emotes.clear(); }

static void startNetGame(uint32_t seed){
   g_hot=false; g_net.playing=true; g_net.rematchAsked=false; g_net.inbox.clear(); g_np.open=false; g_emotes.clear(); g_net.gameNo++;
   g_hist.clear();
   newGameStart(true,seed);
}
// the network game is over (connection lost / left): back to a normal game against the computer
static void netLeave(const wchar_t* message){
   bool wasPlaying=g_net.playing;
   netReset(); g_np.info=message?message:L"";
   if(wasPlaying){
      if(message) MessageBoxW(g_hwnd,message,L"Gra sieciowa",MB_OK|MB_ICONINFORMATION);
      newGameStart();
   }
   g_dirty=true;
}
static void netDesync(const wchar_t* why){
   { std::string w=net::toUtf8(why); g_log+="DESYNC: "+w+"\n"; }
   netSend("BYE");
   std::wstring m=std::wstring(L"Gra obu graczy rozeszła się (")+why+L"). Gra sieciowa została przerwana.";
   netLeave(m.c_str());
}
static void netHostStart(){
   if(netBusy()) return;
   g_net=NetState(); g_net.host=true; g_myAddrs=net::localAddresses();
   std::string err;
   if(g_conn.listenOn(g_hwnd,net::DEFAULT_PORT,err)){ g_net.listening=true; g_np.info.clear(); }
   else { g_net.host=false; g_np.info=L"Nie można nasłuchiwać: "+net::fromUtf8(err); }
   g_dirty=true;
}
static void netJoinStart(){
   if(netBusy()) return;
   std::string ip;                                      // only characters that can be in an address (no quotes, spaces...)
   for(wchar_t ch:g_ipW) if((ch>=L'0'&&ch<=L'9')||ch==L'.'||ch==L'-'||(ch>=L'a'&&ch<=L'z')||(ch>=L'A'&&ch<=L'Z')) ip.push_back((char)ch);
   if(ip.empty()){ g_np.info=L"Wpisz adres IP hosta."; g_dirty=true; return; }
   g_net=NetState(); g_net.host=false;
   if(g_conn.connectTo(g_hwnd,ip,net::DEFAULT_PORT)){ g_net.connecting=true; g_np.info.clear(); }
   else g_np.info=L"Nie można rozpocząć łączenia.";
   g_dirty=true;
}
static void netDisconnect(){
   if(g_net.connected) netSend("BYE");
   bool wasPlaying=g_net.playing;
   if(wasPlaying){ netLeave(nullptr); }
   else { netReset(); g_np.info.clear(); }
   g_dirty=true;
}
static void netHostBegin(){                                 // the host picks the seed and starts both games
   uint32_t seed=std::random_device{}();
   netSend("START "+std::to_string(seed));
   startNetGame(seed);
}
static void netRematchRequest(){
   if(!g_net.playing) return;
   g_net.rematchAsked=true; netSend("REMATCH");
   setStatus(L"Czekam, aż "+g_net.peerNick+L" zgodzi się na nową partię…");
}
// ---------------------------------------------------------------------------
// Online (through the server): log in with the nick + the secret key, open or join a room with a 4-letter code.
// After "#PAIRED" the two players talk exactly as in a local network (the server just relays their lines).
// ---------------------------------------------------------------------------
static void netOnlineStart(bool create){
   if(netBusy()) return;
   std::wstring nick=g_nickW; for(auto& ch:nick) if(ch==L' ') ch=L'_';
   if(nick.size()<3){ g_np.info=L"Nick musi mieć co najmniej 3 znaki."; g_dirty=true; return; }
   std::string server;
   for(wchar_t ch:g_serverW) if(ch>32&&ch<127) server.push_back((char)ch);
   if(server.empty()){ g_np.info=L"Wpisz adres serwera."; g_dirty=true; return; }
   std::string code; for(wchar_t ch:g_codeW) if(iswalnum(ch)&&ch<128) code.push_back((char)towupper(ch));
   if(!create&&code.size()!=4){ g_np.info=L"Kod pokoju ma 4 znaki."; g_dirty=true; return; }
   g_nickW=nick;
   g_net=NetState(); g_net.online=true; g_net.pendingCreate=create; g_net.pendingCode=code;
   g_link=&g_ws;
   if(g_ws.connectTo(g_hwnd,server)){ g_net.connecting=true; g_np.info.clear(); }
   else { g_net=NetState(); g_link=&g_conn; g_np.info=L"Nie można rozpocząć łączenia."; }
   g_dirty=true;
}
static void netServerLine(const std::string& l){
   std::vector<std::string> t; size_t pos=0;
   while(pos<=l.size()){ size_t q=l.find(' ',pos); if(q==std::string::npos) q=l.size(); t.push_back(l.substr(pos,q-pos)); pos=q+1; }
   const std::string& cmd=t[0];
   auto num=[&](size_t i){ return i<t.size()?atoi(t[i].c_str()):0; };
   if(cmd=="#OK"){                                     // logged in: now the room
      if(t.size()>1) g_nickW=net::fromUtf8(t[1]);
      saveSettings();
      serverSend(g_net.pendingCreate?std::string("#CREATE"):"#JOIN "+g_net.pendingCode);
   } else if(cmd=="#ERR"){
      std::string text; for(size_t i=2;i<t.size();i++){ if(i>2) text+=' '; text+=t[i]; }
      std::wstring m=net::fromUtf8(text); netReset(); g_np.info=m; g_np.open=true;
   } else if(cmd=="#ROOM"){
      g_net.roomCode=t.size()>1?t[1]:""; g_net.connecting=false; g_net.listening=true; g_np.info.clear();
   } else if(cmd=="#PAIRED"){
      g_net.host=(t.size()>2&&t[2]=="host"); g_net.peerNick=net::fromUtf8(t.size()>1?t[1]:"");
      g_net.connecting=false; g_net.listening=false; g_net.connected=true;
      g_np.info=L"Połączono z "+g_net.peerNick+L". Uzgadnianie…";
      netSend("HELLO "+net::toUtf8(APP_VERSION)+" "+net::toUtf8(g_nickW));
   } else if(cmd=="#H2H"){
      g_net.h2hW=num(1); g_net.h2hL=num(2); g_net.h2hD=num(3); g_net.hasH2h=true;
   } else if(cmd=="#STATS"){
      setStatus(L"Wynik zapisany. Twój bilans na serwerze: "+std::to_wstring(num(1))+L" wygranych, "+std::to_wstring(num(2))+L" przegranych, "+std::to_wstring(num(3))+L" remisów.",false,8);
   } else if(cmd=="#PEERLEFT"){
      if(g_net.playing||g_net.connected) netLeave((g_net.peerNick+L" opuścił grę.").c_str());
   } else if(cmd=="#KICK"){
      netLeave(L"Ten nick zalogował się z innego miejsca.");
   }
}
static void chatAdd(const std::wstring& who,const std::wstring& text,bool mine){
   g_chatLog.push_back({who,text,mine});
   if(g_chatLog.size()>200) g_chatLog.erase(g_chatLog.begin());
   if(!mine && !g_chat.open){ g_chat.unread++; setStatus(who+L": "+text,false,5); }
   g_dirty=true;
}
static void emoteShow(int idx,int who){ if(idx>=0&&idx<NUM_EMOTES){ g_emotes.push_back({idx,who,nowSec()}); g_dirty=true; } }

// A line of the opponent's that is not a game action.
static void netLine(const std::string& l){
   std::string cmd=l.substr(0,l.find(' ')), rest=l.size()>cmd.size()+1?l.substr(cmd.size()+1):"";
   if(cmd=="HELLO"){
      size_t sp=rest.find(' ');
      g_net.peerVer=rest.substr(0,sp); g_net.peerNick=net::fromUtf8(sp==std::string::npos?"":rest.substr(sp+1));
      if(g_net.peerNick.empty()) g_net.peerNick=L"Przeciwnik";
      if(g_net.peerVer!=net::toUtf8(APP_VERSION)){
         netSend("BYE");
         std::wstring m=L"Różne wersje gry: Ty masz "+std::wstring(APP_VERSION)+L", "+g_net.peerNick+L" ma "+net::fromUtf8(g_net.peerVer)+L".\nZaktualizujcie obie strony do tej samej wersji.";
         netReset(); g_np.info=m; g_dirty=true;
         MessageBoxW(g_hwnd,m.c_str(),L"Gra sieciowa",MB_OK|MB_ICONWARNING);
         return;
      }
      if(g_net.host) netHostBegin();
      else { g_np.info=L"Połączono z "+g_net.peerNick+L". Czekam na rozpoczęcie gry…"; g_dirty=true; }
   }
   else if(cmd=="START"){ if(!g_net.host) startNetGame((uint32_t)strtoul(rest.c_str(),nullptr,10)); }
   else if(cmd=="CHAT"){ chatAdd(g_net.peerNick,net::fromUtf8(rest),false); }
   else if(cmd=="EMO"){ emoteShow(atoi(rest.c_str()),1); }
   else if(cmd=="REMATCH"){
      if(!g_net.playing) return;
      if(g_net.rematchAsked){ if(g_net.host) netHostBegin(); }                 // both asked: the host starts
      else {
         std::wstring m=g_net.peerNick+L" proponuje nową partię. Zgadzasz się?";
         if(MessageBoxW(g_hwnd,m.c_str(),L"Nowa partia",MB_YESNO|MB_ICONQUESTION)==IDYES){
            if(g_net.host) netHostBegin(); else netSend("REMATCH_OK");
         } else netSend("REMATCH_NO");
      }
   }
   else if(cmd=="REMATCH_OK"){ if(g_net.host&&g_net.rematchAsked) netHostBegin(); }
   else if(cmd=="REMATCH_NO"){ g_net.rematchAsked=false; setStatus(g_net.peerNick+L" nie chce nowej partii.",true,4); }
   else if(cmd=="BYE"){ netLeave((g_net.peerNick+L" opuścił grę.").c_str()); }
   else if(cmd=="M"||cmd=="D"||cmd=="X"||cmd=="P"||cmd=="F"){ g_net.inbox.push_back(l); }
}
static void netEvent(int ev,const std::string& s){
   switch(ev){
   case net::EV_CONNECTED:
      if(g_net.online){                                  // connected to the server: log in first
         g_np.info=L"Połączono z serwerem. Logowanie…";
         serverSend("#AUTH "+net::toUtf8(APP_VERSION)+" "+net::toUtf8(g_nickW)+" "+g_secret+(g_inviteW.empty()?std::string():" "+net::toUtf8(g_inviteW)));
         break;
      }
      g_net.listening=false; g_net.connecting=false; g_net.connected=true;
      g_np.info=L"Połączono. Uzgadnianie…"; netSend("HELLO "+net::toUtf8(APP_VERSION)+" "+net::toUtf8(g_nickW));
      break;
   case net::EV_FAILED:
      { bool wasOnline=g_net.online; netReset();
        g_np.info=(wasOnline?L"Nie udało się połączyć z serwerem: ":L"Nie udało się połączyć: ")+net::fromUtf8(s); } break;
   case net::EV_CLOSED:
      if(g_net.playing) netLeave(L"Połączenie z przeciwnikiem zostało przerwane.");
      else if(g_net.connected||g_net.online){ bool wasOnline=g_net.online; netReset(); g_np.info=wasOnline?L"Połączenie z serwerem zostało zamknięte.":L"Połączenie zostało zamknięte."; }
      break;
   case net::EV_LINE: if(g_net.online&&!s.empty()&&s[0]=='#') netServerLine(s); else netLine(s); break;
   }
   g_dirty=true;
}
// Plays one action of the opponent (checked with the same rules - a wrong action means the games diverged).
static void netApply(const std::string& l){
   char c=l[0];
   if(g_game.turn!=1){ netDesync(L"ruch przeciwnika nie w jego turze"); return; }
   const bool endsTurn=(c=='X'||c=='P'||c=='F');
   const uint64_t theirs=endsTurn?strtoull(l.c_str()+1,nullptr,16):0;
   if(c=='M'){
      int a=-1,b=-1; if(sscanf(l.c_str()+1,"%d %d",&a,&b)!=2||a<0||a>=NP||b<0||b>=NP){ netDesync(L"błędny ruch"); return; }
      int src=Game::mirrorPile(a), dst=Game::mirrorPile(b);
      if(!g_game.canMove(src,dst,1)){ netDesync(L"niedozwolony ruch przeciwnika"); return; }
      recAction(g_game,'N',1,ST_MOVE,src,dst); g_game.doMove(src,dst,1); snd("click"); afterAnyMove();
   } else if(c=='D'){
      if(!g_game.canDraw(1)){ netDesync(L"niedozwolone dobranie"); return; }
      recAction(g_game,'N',1,ST_DRAW); g_game.draw(1); snd("click"); afterAnyMove();
   } else if(c=='X'){
      if(g_game.pile[turnedId(1)].empty()){ netDesync(L"niedozwolone odrzucenie"); return; }
      recAction(g_game,'N',1,ST_DISCARD); g_game.discard(1); snd("click"); afterAnyMove(); if(!g_game.over) statusForTurn();
   } else if(c=='P'){
      recAction(g_game,'N',1,ST_PASS); g_game.endTurn(); snd("click"); afterAnyMove(); if(!g_game.over) statusForTurn();
   } else if(c=='F'){
      g_game.endTurn(); snd("nono"); afterAnyMove();
      if(!g_game.over){ statusForTurn(); setStatus(g_net.peerNick+L" zapomniał dołożyć karty do fundamentu i traci turę.",false,4); }
   }
   if(endsTurn && g_net.playing && theirs!=netStateHash()) netDesync(L"różne stany gry po turze");   // checked right away
}
static void netTick(double now){
   if(!g_net.playing) return;
   if(g_game.over){ g_net.inbox.clear(); return; }
   if(g_net.inbox.empty()||now<g_dealUntil||now<g_aiAt||uiBusy(now)) return;
   std::string l=g_net.inbox.front(); g_net.inbox.pop_front();
   netApply(l);
   if(g_net.playing) g_aiAt=now+(l[0]=='M'?0.60:0.55);
}

static void endHumanTurnIfSwitched(){
   if(g_hot){ if(!g_game.over){ g_hist.clear(); statusForTurn(); } return; }      // hot seat: the other person is next, Undo only within a turn
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
   const Card* c=(ptype(m.src)==PT_HAND)?g_game.top(m.src):g_game.srcTop(m.src,actor());
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
   const int who=actor();
   pushUndo();
   startPreview(m);                        // shows the card that should have gone to the foundation
   g_game.endTurn();
   netLocal("F",true);
   relayout(); g_dirty=true;
   if(g_game.over){ onGameOver(); return; }
   endHumanTurnIfSwitched();
   snd("nono");
   if(g_hot) setStatus(hotName(who)+L" zapomniał dołożyć karty do fundamentu: traci turę!",true,4);
   else setStatus(L"Zapomniałeś dołożyć karty do fundamentu: tracisz turę!",true,4);
}
// The player tried something else while `must` still has to go to a foundation. Setting "Karaj": the turn is lost.
// Setting "Przypomnij": nothing happens except that the forced move is shown, so the player has to make it.
static void obligationBlocked(const Move& must){
   if(g_forceMode==1){
      startPreview(must); snd("nono"); relayout(); g_dirty=true;
      setStatus(L"Przymus: ta karta musi najpierw trafić na fundament.",true,4);
      return;
   }
   loseTurnForForgetting(must);
}
static void humanDraw(){
   if(!humanTurn()) return;
   const int a=actor();
   if(!g_game.pile[turnedId(a)].empty()){ snd("nono"); setStatus(L"Najpierw zagraj albo odrzuć dobraną kartę.",true,3); return; }
   Move m;
   if(g_game.mandatory(a,m)){ obligationBlocked(m); return; }
   if(!g_game.canDraw(a)){ snd("nono"); setStatus(L"Talia i śmietnik są puste. Kliknij talię (lub naciśnij D), żeby spasować.",true,3); return; }
   pushUndo(); logf("humanDraw"); recAction(g_game,'H',a,ST_DRAW); g_game.draw(a); netLocal("D",false); snd("click"); afterAnyMove(); statusForTurn();
}
static void humanDiscard(){
   if(!humanTurn()) return;
   const int a=actor();
   bool hasTurned=!g_game.pile[turnedId(a)].empty();
   if(!hasTurned && g_game.canDraw(a)) return;            // pass is only possible with nothing left to draw
   Move m;
   if(g_game.mandatory(a,m)){ obligationBlocked(m); return; }
   pushUndo(); logf(hasTurned?"humanDiscard":"humanPass"); recAction(g_game,'H',a,hasTurned?ST_DISCARD:ST_PASS);
   if(hasTurned) g_game.discard(a); else g_game.endTurn();
   netLocal(hasTurned?"X":"P",true);
   snd("click"); afterAnyMove(); endHumanTurnIfSwitched();
}
static void humanMove(int src,int dst,bool undoable=true){
   const int a=actor();
   if(undoable){                         // (a step of a sequence plan is not checked again: the plan start was)
      Move must;
      if(breaksObligation(g_game,a,dst) && g_game.mandatory(a,must)){ obligationBlocked(must); return; }   // forgot a foundation move
      pushUndo();
   }
   logf("humanMove",src,dst); recAction(g_game,'H',a,ST_MOVE,src,dst); g_game.doMove(src,dst,a); netLocal("M "+std::to_string(src)+" "+std::to_string(dst),false); snd("click"); afterAnyMove();
   if(!g_game.over) statusForTurn();
}
// Undo: restores the state from before the player's last action (a move, drawing, discarding, passing, or a whole
// sequence move). When that action ended the turn, the computer's moves made since are taken back too.
// Hot seat: only within the own turn (the history is cleared when the turn passes).
static bool canUndo(){
   return !g_net.playing && !g_hist.empty() && !g_dragging && !g_plan.active && !g_deal.active && nowSec()>=g_dealUntil && (g_game.over||g_hot||g_game.turn==0);
}
static void undoMove(){
   if(!canUndo()) return;
   g_game=g_hist.back(); g_hist.pop_back();
   g_ctx=AIContext(); g_overShown=false; g_fw.stop(); g_prev.active=false; g_start.active=false; g_rev.active=false;
   relayout(); g_dirty=true; snd("cofnij");
   statusForTurn();
}
static void doHint(){
   if(!humanTurn()||g_net.playing) return;     // no hints in a network game
   const int a=actor();
   Move m; AIContext c;
   if(aiChoose(g_game,a,c,2,g_game.rng,m)){
      startPreview(m); snd("podp");
      setStatus(g_game.mandatory(a,m)?L"Podpowiedź: ta karta musi iść na fundament.":L"Podpowiedź: tak możesz zagrać.",false,3.5);
   } else if(!g_game.pile[turnedId(a)].empty()){
      startPreview({turnedId(a),wasteId(a)}); snd("podp");
      setStatus(L"Brak ruchów: odrzuć dobraną kartę na śmietnik.",false,3.5);
   } else if(g_game.canDraw(a)){
      startPreview({handId(a),turnedId(a)}); snd("podp");
      setStatus(L"Brak ruchów: dobierz kartę z talii.",false,3.5);
   } else { snd("nono"); setStatus(L"Brak ruchów: kliknij talię (lub naciśnij D), żeby spasować.",true,3.5); }
}
// A click on a card moves it to its best place (foundation, column, opponent's pile...).
// For the turned card the best place may be the own waste pile: then it is discarded.
// A click on the own deck draws a card (or passes, when there is nothing to draw).
static void autoClick(int pile){
   const int a=actor();
   if(pile==handId(a)){
      if(g_game.pile[turnedId(a)].empty() && !g_game.canDraw(a)) humanDiscard(); else humanDraw();
      return;
   }
   const Card* c=g_game.srcTop(pile,a);
   if(!c) return;
   if(!g_autoMoves){ setStatus(L"Automatyczne ruchy są wyłączone (Ustawienia → Rozgrywka): przeciągnij kartę.",false,3); return; }
   // Best of ALL legal moves (see bestClickMove). Only when there is none: the turned card goes to the waste pile.
   Move best;
   if(bestClickMove(g_game,pile,best,a)){ humanMove(best.src,best.dst); return; }
   if(pile==turnedId(a)){ humanDiscard(); return; }
   snd("nono"); setStatus(L"Ta karta nie ma żadnego dozwolonego ruchu.",true,2.5);
}
// Debug aid (F6): plays ONE step for the player the way the computer would (used to test network play by script).
static void debugAutoStep(){
   if(!humanTurn()) return;
   const int a=actor();
   Move m; AIContext c;
   if(aiChoose(g_game,a,c,2,g_game.rng,m)) humanMove(m.src,m.dst);
   else if(!g_game.pile[turnedId(a)].empty()) humanDiscard();
   else if(g_game.canDraw(a)) humanDraw();
   else humanDiscard();
}
// Debug aid (F9): dump every pile to garibaldi_dump.txt next to the exe.
static void dumpState(){
   wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH);
   std::wstring path=b; path.resize(path.find_last_of(L'\\')+1); path+=L"garibaldi_dump"+g_instTag+L".txt";
   FILE* f=_wfopen(path.c_str(),L"w"); if(!f) return;
   const char* names[]={"res","hand","turned","waste","tab","fnd"};
   for(int id=0;id<NP;id++){
      fprintf(f,"%s%d (%d):",names[ptype(id)],pidx(id),(int)g_game.pile[id].size());
      for(auto& c:g_game.pile[id]) fprintf(f," %s%s",c.imgKey().c_str(),c.up?"":"*");
      fprintf(f,"\n");
   }
   fprintf(f,"--- log\n%s",g_log.c_str());
   fprintf(f,"hash=%llx playing=%d host=%d\n",(unsigned long long)(g_net.playing?netStateHash():g_game.hash()),(int)g_net.playing,(int)g_net.host);
   fprintf(f,"turn=%d over=%d totalTurns=%d idle=%d\n",g_game.turn,(int)g_game.over,g_game.totalTurns,g_game.idle);
   fclose(f);
}
// ---------------------------------------------------------------------------
// Rules window: a scrollable panel drawn in the style of the game (see helpDraw).
// ---------------------------------------------------------------------------
struct Help{ bool open=false; float scroll=0, contentH=0, builtW=-1; bool dragThumb=false; float grabDy=0; };
static Help g_help;
static void closeOverlays();
static void showRules(){ bool was=g_help.open; closeOverlays(); g_help.open=!was; if(g_help.open){ g_help.scroll=0; g_help.builtW=-1; } g_dirty=true; }

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
   if(g_fireBmp){ g_fireBmp->Release(); g_fireBmp=nullptr; }
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
static bool btnVisible(int id){ return id!=B_CHAT || g_net.playing; }                // the chat button exists only in a network game
static const wchar_t* btnLabel(int id,std::wstring& tmp){
   switch(id){
   case B_NEW:      return L"Nowa gra";
   case B_UNDO:     return L"Cofnij";
   case B_HINT:     return L"Podpowiedź";
   case B_NET:      return g_net.playing?L"Graj przez sieć ●":L"Graj przez sieć";
   case B_HOT:      return g_hot?L"Hot seat ●":L"Hot seat";
   case B_CHAT:     tmp=g_chat.unread>0?L"Czat ("+std::to_wstring(g_chat.unread)+L")":std::wstring(L"Czat"); return tmp.c_str();
   case B_STATS:    return L"Statystyki";
   case B_SETTINGS: return L"Ustawienia";
   default:         return L"Zasady";
   }
}
static void layoutButtons(){
   // widths are rough estimates for Segoe UI 14px
   bool icon[B_COUNT]={true,true,true,false,false,false,false,true,false};
   float wd[B_COUNT]={}, sum=0; int n=0;
   for(int i=0;i<B_COUNT;i++){
      if(!btnVisible(i)) continue;
      std::wstring t; const wchar_t* lab=btnLabel(i,t);
      wd[i]=(float)wcslen(lab)*7.6f+24.f+(icon[i]?34.f:0.f); sum+=wd[i]; n++;
   }
   float gap=8.f, avail=std::max(300.f,G.w-20.f-gap*(n-1));
   float k=sum>avail?avail/sum:1.f;                          // a narrow window: the buttons shrink to fit
   float x=10;
   for(int i=0;i<B_COUNT;i++){
      if(!btnVisible(i)){ g_btn[i]={-1000.f,0,0,0}; continue; }
      g_btn[i]={x,9.f,wd[i]*k,40.f}; x+=wd[i]*k+gap;
   }
}
static bool btnEnabled(int id){
   switch(id){
   case B_UNDO:    return canUndo();
   case B_HINT:    return humanTurn() && !g_net.playing;
   case B_HOT:     return !g_net.playing;
   default:        return true;
   }
}
static void drawToolbar(){
   layoutButtons();
   rrect(0,0,G.w,TB,0,0,0,0,0.38f);
   for(int i=0;i<B_COUNT;i++){
      if(!btnVisible(i)) continue;
      const Btn& b=g_btn[i]; bool en=btnEnabled(i), hov=(g_hoverBtn==i)&&en;
      rrect(b.x,b.y,b.w,b.h,8,1,1,1,en?(hov?0.28f:0.14f):0.05f);
      rrect(b.x,b.y,b.w,b.h,8,1,1,1,en?0.35f:0.12f,false,1.f);
      std::wstring tmp; std::wstring lab=btnLabel(i,tmp);
      const char* icoKey=nullptr;
      switch(i){
      case B_NEW:      icoKey="IMG_NEW"; break;
      case B_UNDO:     icoKey="IMG_UNDO"; break;
      case B_HINT:     icoKey="IMG_HINT"; break;
      case B_SETTINGS: icoKey="IMG_USTAWIENIA"; break;
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
   if(F.scale<0.01f && target==0.f && g_fire.j1<g_fire.j0) return;
   if(F.lit){
      if(!g_game.over && g_game.turn!=F.player){                          // the turn changed: the flame moves on
         F.from=F.y; F.to=flameCenterY(g_game.turn); F.t0=now; F.moving=true; F.player=g_game.turn;
         snd("plomien");                                                      // whoosh of the torch, as long as the move (~0.65 s)
      }
      if(F.moving){
         float pr=(float)((now-F.t0)/F.dur); if(pr>=1.f){ pr=1.f; F.moving=false; }
         F.y=F.from+(F.to-F.from)*easeInOut(pr);                          // accelerates, then slows down
      } else F.y=flameCenterY(F.player);
   }
   const float sc=std::max(0.4f,G.ch/130.f);                             // design px -> screen px (a card is 130 high)
   const float cx=std::max(G.x0-40.f*sc,36.f*sc+4.f);                     // clear of the cards, but on the screen
   const float by=F.y+G.ch*0.44f;                                         // bottom of the bowl
   const float bowlH=24.f*sc, rimY=by-bowlH;                              // the flame comes out of the rim
   const float H=G.ch*0.92f*F.scale*0.8f, W=G.fs*0.78f*F.scale;
   const float t=(float)now, fl=0.5f+0.5f*std::sin(t*13.f)*std::sin(t*7.3f+1.f);   // flicker 0..1
   const float a=std::min(1.f,F.scale*1.6f);
   auto P=[](float x,float y){ return D2D1::Point2F(x,y); };
   // soft glow on the table around the fire
   {
      ID2D1GradientStopCollection* st=nullptr; D2D1_GRADIENT_STOP gs[2];
      gs[0].position=0.f; gs[0].color=D2D1::ColorF(1.f,0.55f,0.12f,(0.20f+0.10f*fl)*a);
      gs[1].position=1.f; gs[1].color=D2D1::ColorF(1.f,0.35f,0.05f,0.f);
      if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,2,&st))){
         ID2D1RadialGradientBrush* rb=nullptr;
         float rx=W*2.1f+1.f, ry=H*0.9f+1.f;
         if(SUCCEEDED(g_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(P(cx,rimY-H*0.35f),P(0,0),rx,ry),st,&rb))){
            g_rt->FillEllipse(D2D1::Ellipse(P(cx,rimY-H*0.35f),rx,ry),rb); rb->Release();
         }
         st->Release();
      }
   }
   // The golden bowl the flame comes out of: back (body + dark inside) now, the front lip after the fire.
   const float bw=64.f*sc, bh=bowlH, ex=bw*0.5f, ery=6.f*sc, ba=std::min(1.f,F.scale*2.5f);
   auto gold=[&](float x0,float x1,float y0,float y1,float al,bool vertical)->ID2D1LinearGradientBrush*{
      ID2D1GradientStopCollection* st=nullptr; D2D1_GRADIENT_STOP gs[5]={
         {0.00f,D2D1::ColorF(0.45f,0.30f,0.05f,al)},{0.22f,D2D1::ColorF(0.93f,0.72f,0.20f,al)},{0.45f,D2D1::ColorF(1.00f,0.92f,0.55f,al)},
         {0.70f,D2D1::ColorF(0.80f,0.55f,0.10f,al)},{1.00f,D2D1::ColorF(0.38f,0.24f,0.04f,al)}};
      ID2D1LinearGradientBrush* br=nullptr;
      if(SUCCEEDED(g_rt->CreateGradientStopCollection(gs,5,&st))){
         g_rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(P(vertical?x0:x0,y0),P(vertical?x0:x1,vertical?y1:y0)),st,&br); st->Release(); }
      return br; };
   if(ba>0.01f && g_d2d){
      // body: a bowl (half ellipse) on a short foot
      ID2D1PathGeometry* body=nullptr;
      if(SUCCEEDED(g_d2d->CreatePathGeometry(&body))){
         ID2D1GeometrySink* sk=nullptr;
         if(SUCCEEDED(body->Open(&sk))){
            sk->BeginFigure(P(cx-ex,rimY),D2D1_FIGURE_BEGIN_FILLED);
            sk->AddBezier(D2D1::BezierSegment(P(cx-ex,rimY+bh*0.62f),P(cx-ex*0.45f,rimY+bh*0.78f),P(cx-ex*0.22f,rimY+bh*0.80f)));
            sk->AddLine(P(cx-ex*0.40f,by)); sk->AddLine(P(cx+ex*0.40f,by)); sk->AddLine(P(cx+ex*0.22f,rimY+bh*0.80f));
            sk->AddBezier(D2D1::BezierSegment(P(cx+ex*0.45f,rimY+bh*0.78f),P(cx+ex,rimY+bh*0.62f),P(cx+ex,rimY)));
            sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close(); sk->Release();
            ID2D1LinearGradientBrush* gb=gold(cx-ex,cx+ex,rimY,rimY,ba,false);
            if(gb){ g_rt->FillGeometry(body,gb); gb->Release(); }
            ID2D1SolidColorBrush* ob=nullptr;
            if(SUCCEEDED(g_rt->CreateSolidColorBrush(D2D1::ColorF(0.30f,0.19f,0.03f,0.85f*ba),&ob))){ g_rt->DrawGeometry(body,ob,1.2f); ob->Release(); }
         }
         body->Release();
      }
      // inside of the bowl (dark, lit by the fire from above)
      { ID2D1SolidColorBrush* ib=nullptr;
        if(SUCCEEDED(g_rt->CreateSolidColorBrush(D2D1::ColorF(0.40f,0.20f,0.03f,ba),&ib))){ g_rt->FillEllipse(D2D1::Ellipse(P(cx,rimY),ex*0.97f,ery),ib); ib->Release(); } }
   }
   // The fire: a fluid simulation in design pixels (a card is 130 high), drawn as a soft bitmap. The burner moves with
   // the flame's turn marker; the burning gas stays where it was, so a moving flame leaves a curved trail that rises.
   static float kS=0,kH=0,kX=0;
   if(!g_fire.ready()||std::fabs(kS-sc)>0.001f||std::fabs(kH-G.h)>0.5f||std::fabs(kX-cx)>0.5f){
      kS=sc; kH=G.h; kX=cx; g_fire.init(140.f,G.h/sc,3.f);
      g_fireImg.assign((size_t)g_fire.nx*g_fire.ny,0u);
      if(g_fireBmp){ g_fireBmp->Release(); g_fireBmp=nullptr; }
   }
   g_fireAcc=std::min(g_fireAcc+dt,0.1);
   const float strength=F.lit?F.scale:0.f;
   while(g_fireAcc>=1.0/60.0){
      g_fire.step(1.f/60.f,70.f,(rimY+3.f*sc)/sc,50.f*(0.55f+0.45f*F.scale),strength);
      g_fireAcc-=1.0/60.0;
   }
   g_fire.render(g_fireImg.data());
   if(!g_fireBmp){
      D2D1_BITMAP_PROPERTIES bp=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
      g_rt->CreateBitmap(D2D1::SizeU((UINT32)g_fire.nx,(UINT32)g_fire.ny),nullptr,0,bp,&g_fireBmp);
   }
   if(g_fireBmp){
      g_fireBmp->CopyFromMemory(nullptr,g_fireImg.data(),(UINT32)g_fire.nx*4);
      const float ox=cx-70.f*sc;
      g_rt->DrawBitmap(g_fireBmp,D2D1::RectF(ox,0.f,ox+g_fire.nx*g_fire.h*sc,g_fire.ny*g_fire.h*sc),1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
   }
   if(ba>0.01f && g_d2d){
      // front lip of the rim: the lower half of the rim ellipse, a thick gold band over the foot of the flame
      ID2D1PathGeometry* lip=nullptr;
      if(SUCCEEDED(g_d2d->CreatePathGeometry(&lip))){
         ID2D1GeometrySink* sk=nullptr;
         if(SUCCEEDED(lip->Open(&sk))){
            sk->BeginFigure(P(cx-ex-1.5f*sc,rimY-1.f*sc),D2D1_FIGURE_BEGIN_FILLED);
            sk->AddArc(D2D1::ArcSegment(P(cx+ex+1.5f*sc,rimY-1.f*sc),D2D1::SizeF(ex+1.5f*sc,ery+1.5f*sc),0.f,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            sk->AddLine(P(cx+ex*0.97f,rimY)); 
            sk->AddArc(D2D1::ArcSegment(P(cx-ex*0.97f,rimY),D2D1::SizeF(ex*0.97f,ery),0.f,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close(); sk->Release();
            ID2D1LinearGradientBrush* gb=gold(cx-ex,cx+ex,rimY,rimY,ba,false);
            if(gb){ g_rt->FillGeometry(lip,gb); gb->Release(); }
            ID2D1SolidColorBrush* ob=nullptr;
            if(SUCCEEDED(g_rt->CreateSolidColorBrush(D2D1::ColorF(0.30f,0.19f,0.03f,0.8f*ba),&ob))){ g_rt->DrawGeometry(lip,ob,1.1f); ob->Release(); }
         }
         lip->Release();
      }
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
 {HK_P,nullptr,L"Dopóki jakaś karta może iść na fundament, wolno zagrywać tylko na fundamenty. Kto zapomni i zagra inaczej (na kolumnę, na stos przeciwnika), dobierze kartę, odrzuci ją lub spasuje, traci turę. Takie zagranie nie zostaje wykonane."},
 {HK_P,nullptr,L"W Ustawieniach (Rozgrywka) możesz zamiast „Karaj” wybrać „Przypomnij”: wtedy nie ma kary, tylko gra pokazuje obowiązkowy ruch na fundament i blokuje zagranie, które chciałeś wykonać."},

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
 {HK_B,L"Kliknięcie karty",L"przenosi ją na najlepsze miejsce (opcja „Automatyczne ruchy” w Ustawieniach). Dobrana karta trafia na śmietnik tylko wtedy, gdy nie ma dla niej żadnego innego ruchu."},
 {HK_B,L"Kliknięcie własnej talii",L"dobiera kartę; gdy nie ma czego dobrać, spasowuje."},
 {HK_B,L"Przeciąganie",L"przenosi kartę lub cały sekwens tam, gdzie chcesz. Zielone ramki pokazują dozwolone miejsca."},

 {HK_H,nullptr,L"Skróty klawiszowe"},
 {HK_B,L"Spacja",L"dobierz kartę"},
 {HK_B,L"D",L"odrzuć dobraną kartę / pas"},
 {HK_B,L"H",L"podpowiedź (karta sama pokazuje ruch)"},
 {HK_B,L"Backspace, U, Ctrl+Z",L"cofnij"},
 {HK_B,L"F2",L"nowa gra"},
 {HK_B,L"F1 / F3 / F4",L"zasady / ustawienia / statystyki"},
 {HK_B,L"F5 / F7",L"graj przez sieć / hot seat"},
 {HK_B,L"M",L"dźwięk włączony / wyłączony"},
 {HK_NOTE,nullptr,L"Podane są skróty domyślne. Wszystkie (poza Ctrl+Z) możesz zmienić w Ustawieniach → Sterowanie."},

 {HK_H,nullptr,L"Gra sieciowa"},
 {HK_P,nullptr,L"Przycisk „Graj przez sieć”: jeden gracz klika „Hostuj grę” i podaje znajomemu adres IP swojego komputera, drugi wpisuje go i klika „Połącz”. W zakładce „Internet” jeden gracz klika „Utwórz pokój” i podaje kod, drugi wpisuje kod i klika „Dołącz”. Obie kopie gry muszą mieć tę samą wersję. W grze sieciowej nie ma cofania ani podpowiedzi."},
 {HK_B,L"Czat",L"przycisk „Czat” (widoczny tylko w grze sieciowej) lub Enter; pod polem wiadomości są emotki."},
 {HK_B,L"Nowa partia",L"przycisk „Nowa gra” proponuje ją przeciwnikowi, który musi się zgodzić."},

 {HK_H,nullptr,L"Hot seat"},
 {HK_P,nullptr,L"Przycisk „Hot seat” zaczyna grę dwóch osób przy jednym komputerze. Gracz 1 siedzi na dole, Gracz 2 na górze; zagrywa ten, czyja jest tura (płomień obok magazynu). Obaj widzą wszystkie karty, które leżą na stole. Cofać można tylko ruchy z własnej tury, a podpowiedź działa dla gracza, którego jest ruch. Takie partie nie wchodzą do statystyk. Ponowne kliknięcie przycisku wraca do gry z komputerem."},

 {HK_H,nullptr,L"Statystyki i ustawienia"},
 {HK_B,L"Statystyki",L"liczba rozegranych i wygranych partii oraz procent wygranych osobno dla każdego poziomu komputera i dla gry przez sieć."},
 {HK_B,L"Ustawienia",L"aktualizacje, poziom gry, automatyczne ruchy, przymus fundamentu, głośność i własne dźwięki, skróty klawiszowe. Gra pamięta ostatnio wybraną grupę."},

 {HK_H,nullptr,L"Cofanie i zapis gry"},
 {HK_P,nullptr,L"Cofnij cofa Twoją ostatnią czynność: ruch, dobranie, odrzucenie, a przeniesienie całego sekwensu jako jeden krok. Jeśli ta czynność skończyła turę, cofa też ruchy komputera wykonane od tamtej pory."},
 {HK_P,nullptr,L"Stan gry zapisuje się na bieżąco i wczytuje przy następnym uruchomieniu (również po awarii)."},

 {HK_H,nullptr,L"Komputer"},
 {HK_P,nullptr,L"Komputer ma trzy poziomy trudności: Łatwy, Normalny i Trudny (zmieniasz je w Ustawieniach). Gra według dziesięciu zasad opisanych w pliku AI_RULES.md."},
};
struct HelpBlock{ IDWriteTextLayout* lay=nullptr; int kind=0; float y=0,h=0; };
static std::vector<HelpBlock> g_helpBlocks;
static void helpRelease(){ for(auto& b:g_helpBlocks) if(b.lay) b.lay->Release(); g_helpBlocks.clear(); }
// panel (px..), header, and the scrolled view (vx..)
static void helpGeom(float& px,float& py,float& pw,float& ph,float& vx,float& vy,float& vw,float& vh){
   pw=std::min(840.f,G.w-30.f); ph=G.h-30.f; px=std::floor((G.w-pw)/2.f); py=15.f;
   vx=px+28.f; vy=py+82.f; vw=pw-28.f-40.f; vh=ph-82.f-24.f;
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
}
// mouse handling of the rules window (all clicks are consumed while it is open)
static void helpMouseDown(float mx,float my){
   float px,py,pw,ph,vx,vy,vw,vh; helpGeom(px,py,pw,ph,vx,vy,vw,vh);
   float cbx=px+pw-52, cby=py+16;
   if(mx>=cbx&&mx<=cbx+34&&my>=cby&&my<=cby+34){ showRules(); return; }
   if(mx<px||mx>px+pw||my<py||my>py+ph){ showRules(); return; }          // a click outside the panel closes it
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

// ============================================================================
// Network panel (lobby) and chat
// ============================================================================
static void txtWrap(const std::wstring& s,float x,float y,float w,float h,float px,float r,float g,float b,float a,bool bold=false){
   if(!g_dw||s.empty()) return;
   IDWriteTextFormat* f=nullptr;
   g_dw->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
   if(!f) return;
   f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
   ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(r,g,b,a),&br);
   if(br){ g_rt->DrawText(s.c_str(),(UINT32)s.size(),f,D2D1::RectF(x,y,x+w,y+h),br); br->Release(); }
   f->Release();
}
static void txtEmoji(const wchar_t* s,float x,float y,float w,float h,float px,float a){
   if(!g_dw) return;
   IDWriteTextFormat* f=nullptr;
   g_dw->CreateTextFormat(L"Segoe UI Emoji",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,px,L"",&f);
   if(!f) return;
   f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
   ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,1.f,1.f,a),&br);
   if(br){ g_rt->DrawText(s,(UINT32)wcslen(s),f,D2D1::RectF(x,y,x+w,y+h),br,(D2D1_DRAW_TEXT_OPTIONS)4/*ENABLE_COLOR_FONT*/); br->Release(); }
   f->Release();
}
static std::wstring clipboardText(){
   std::wstring r;
   if(OpenClipboard(g_hwnd)){
      HANDLE h=GetClipboardData(CF_UNICODETEXT);
      if(h){ const wchar_t* p=(const wchar_t*)GlobalLock(h); if(p){ r=p; GlobalUnlock(h); } }
      CloseClipboard();
   }
   for(auto& ch:r) if(ch==L'\r'||ch==L'\n'||ch==L'\t') ch=L' ';
   return r;
}
// ============================================================================
// Settings and statistics windows: drawn like the rules window. Immediate mode: the controls register their
// clickable areas while they are drawn, the next mouse click looks them up.
// ============================================================================
struct SHit{ float x,y,w,h; int kind,a; };
enum { SH_CLOSE=1, SH_GROUP, SH_CHECK, SH_LEVEL, SH_FORCE, SH_SLIDER, SH_SBROWSE, SH_SPLAY, SH_SMUTE, SH_SDEF, SH_KEY, SH_KEYCLR, SH_KEYDEF, SH_STATRESET };
struct SetUi{ bool open=false; int capture=-1; bool dragSlider=false; float sx=0, sw=1; std::vector<SHit> hits; };
static SetUi g_set;
struct StatUi{ bool open=false; std::vector<SHit> hits; };
static StatUi g_stat;
static const wchar_t* SET_GROUPS[5]={L"Ogólne",L"Rozgrywka",L"Grafika",L"Dźwięk",L"Sterowanie"};

static void closeOverlays(){ g_help.open=false; g_help.dragThumb=false; g_np.open=false; g_set.open=false; g_set.capture=-1; g_set.dragSlider=false; g_stat.open=false; g_dirty=true; }
static void showSettings(){ bool was=g_set.open; closeOverlays(); g_set.open=!was; }
static void showStats(){ bool was=g_stat.open; closeOverlays(); g_stat.open=!was; }

static void uiHit(std::vector<SHit>& v,float x,float y,float w,float h,int kind,int a=0){ v.push_back({x,y,w,h,kind,a}); }
static void uiPanel(float px,float py,float pw,float ph,const wchar_t* title,const std::wstring& subtitle,std::vector<SHit>& hits){
   rrect(0,0,G.w,G.h,0,0,0,0,0.65f);                                   // dim the table
   rrect(px+5,py+8,pw,ph,16,0,0,0,0.40f);                              // shadow
   rrect(px,py,pw,ph,16,0.05f,0.16f,0.10f,0.99f);                      // panel (dark felt)
   rrect(px,py,pw,ph,16,0.95f,0.80f,0.30f,0.85f,false,2.f);            // gold border
   txt(title,px+28,py+12,pw-120,40,30,1.f,0.86f,0.25f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   txt(subtitle,px+29,py+48,pw-120,22,15,0.82f,0.90f,0.84f,0.9f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   rrect(px+24,py+76,pw-48,1.5f,0,1,1,1,0.20f);
   float cbx=px+pw-52, cby=py+16;
   rrect(cbx,cby,34,34,8,1,1,1,0.14f); rrect(cbx,cby,34,34,8,1,1,1,0.35f,false,1.f);
   g_ren.drawLine(cbx+11,cby+11,cbx+23,cby+23,2.2f,255,255,255,230);
   g_ren.drawLine(cbx+23,cby+11,cbx+11,cby+23,2.2f,255,255,255,230);
   uiHit(hits,cbx,cby,34,34,SH_CLOSE);
}
static void uiButton(std::vector<SHit>& v,float x,float y,float w,float h,const std::wstring& label,int kind,int a,bool active=false,bool enabled=true,float px=14.f){
   rrect(x,y,w,h,8,1,1,1,enabled?(active?0.26f:0.14f):0.05f);
   if(active) rrect(x,y,w,h,8,1.f,0.86f,0.30f,0.95f,false,2.f); else rrect(x,y,w,h,8,1,1,1,enabled?0.38f:0.12f,false,1.f);
   txt(label,x+4,y,w-8,h,px,1,1,1,enabled?1.f:0.4f,active);
   if(enabled) uiHit(v,x,y,w,h,kind,a);
}
static void uiCheck(std::vector<SHit>& v,float x,float y,float w,const std::wstring& label,bool on,int id){
   rrect(x,y+4,20,20,5,1,1,1,0.14f); rrect(x,y+4,20,20,5,1,1,1,0.50f,false,1.3f);
   if(on){ g_ren.drawLine(x+4,y+14,x+9,y+19,2.6f,255,220,80,255); g_ren.drawLine(x+9,y+19,x+17,y+9,2.6f,255,220,80,255); }
   txt(label,x+32,y,w-32,28,15,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   uiHit(v,x,y,w,28,SH_CHECK,id);
}
static void uiHeading(const std::wstring& t,float x,float y,float w){
   txt(t,x,y,w,28,20,1.f,0.86f,0.30f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   rrect(x,y+31,w,1.2f,0,1.f,0.86f,0.30f,0.35f);
}
static void uiNote(const std::wstring& t,float x,float y,float w,float h){ txtWrap(t,x,y,w,h,13.5f,0.76f,0.84f,0.78f,1.f); }

static std::wstring fileBase(const std::wstring& p){ size_t k=p.find_last_of(L"\\/"); return k==std::wstring::npos?p:p.substr(k+1); }

static void settingsDraw(){
   if(!g_set.open||!g_rt||!g_dw) return;
   g_set.hits.clear();
   const float pw=std::min(880.f,G.w-30.f), ph=std::min(620.f,G.h-30.f), px=std::floor((G.w-pw)/2.f), py=std::floor((G.h-ph)/2.f);
   uiPanel(px,py,pw,ph,L"Ustawienia",L"Zmiany działają od razu i są zapamiętywane w pliku .ini",g_set.hits);
   // groups (left)
   const float nx=px+24, ny=py+92, nw=176;
   for(int i=0;i<5;i++){
      float y=ny+i*52.f;
      bool act=(g_setGroup==i);
      rrect(nx,y,nw,44,8,1,1,1,act?0.22f:0.07f);
      if(act) rrect(nx,y,nw,44,8,1.f,0.86f,0.30f,0.95f,false,2.f); else rrect(nx,y,nw,44,8,1,1,1,0.20f,false,1.f);
      txt(SET_GROUPS[i],nx+14,y,nw-20,44,16,1,1,1,1.f,act,DWRITE_TEXT_ALIGNMENT_LEADING);
      uiHit(g_set.hits,nx,y,nw,44,SH_GROUP,i);
   }
   rrect(nx+nw+14,ny,1.2f,ph-92-24,0,1,1,1,0.18f);
   const float cx=nx+nw+30, cw=px+pw-28-cx; float y=ny;
   uiHeading(SET_GROUPS[g_setGroup],cx,y,cw); y+=48;
   switch(g_setGroup){
   case 0:{                                                                  // General
      uiCheck(g_set.hits,cx,y,cw,L"Sprawdzaj aktualizacje przy starcie gry",g_checkUpdates,0); y+=34;
      uiNote(L"Gra pyta w serwisie GitHub, czy jest nowsza wersja, i proponuje jej zainstalowanie.",cx+32,y,cw-32,40); y+=56;
      uiNote(std::wstring(L"Wersja ")+APP_VERSION+L", zbudowana "+buildDateText()+L".",cx,y,cw,22); y+=34;
      uiNote(L"Obok programu leżą pliki: Garibaldi.ini (ustawienia i statystyki), Garibaldi.sav (zapis gry, tworzony na bieżąco) i garibaldka_ruchy.log (zapis wszystkich ruchów do analizy).",cx,y,cw,70);
      break;}
   case 1:{                                                                  // Gameplay
      txt(L"Poziom gry (komputer)",cx,y,260,36,15,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
      uiButton(g_set.hits,cx+270,y,220,36,std::wstring(L"Poziom: ")+LEVEL_NAMES[g_level],SH_LEVEL,0);
      y+=44; uiNote(L"Kliknij przycisk, żeby zmienić poziom. Łatwy i Normalny czasem pomijają dobry ruch, Trudny gra zawsze najlepiej, jak potrafi.",cx,y,cw,44); y+=62;
      uiCheck(g_set.hits,cx,y,cw,L"Automatyczne ruchy",g_autoMoves,2); y+=34;
      uiNote(L"Kliknięcie karty przenosi ją na najlepsze miejsce (kliknięcie karty w kolumnie przenosi cały sekwens). Po wyłączeniu kartę można przenosić tylko przeciąganiem; kliknięcie własnej talii nadal dobiera kartę.",cx+32,y,cw-32,62); y+=80;
      txt(L"Przymus fundamentu",cx,y,260,36,15,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
      uiButton(g_set.hits,cx+270,y,120,36,L"Karaj",SH_FORCE,0,g_forceMode==0);
      uiButton(g_set.hits,cx+398,y,120,36,L"Przypomnij",SH_FORCE,1,g_forceMode==1);
      y+=44;
      uiNote(g_forceMode==0
         ? L"Karaj: kto pominie ruch na fundament, natychmiast traci turę, a jego zagranie nie zostaje wykonane."
         : L"Przypomnij: zamiast kary gra pokazuje obowiązkowy ruch na fundament i blokuje zagranie, które chciałeś wykonać. Dopóki go nie zrobisz, nie zagrasz niczego innego.",cx,y,cw,62);
      break;}
   case 2:                                                                   // Graphics (empty for now)
      uiNote(L"Na razie brak ustawień grafiki.",cx,y,cw,24);
      break;
   case 3:{                                                                  // Sound
      uiCheck(g_set.hits,cx,y,cw,L"Dźwięk włączony",!g_muted,1); y+=40;
      txt(L"Głośność",cx,y,100,30,15,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
      g_set.sx=cx+110; g_set.sw=cw-110-70;
      rrect(g_set.sx,y+12,g_set.sw,6,3,1,1,1,0.20f);
      rrect(g_set.sx,y+12,g_set.sw*g_volPct/100.f,6,3,1.f,0.86f,0.30f,0.95f);
      { float kx=g_set.sx+g_set.sw*g_volPct/100.f; D2D1_ELLIPSE e=D2D1::Ellipse(D2D1::Point2F(kx,y+15),10.f,10.f);
        ID2D1SolidColorBrush* br=nullptr; g_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.93f,0.55f,1.f),&br); if(br){ g_rt->FillEllipse(e,br); br->Release(); } }
      uiHit(g_set.hits,g_set.sx-10,y,g_set.sw+20,30,SH_SLIDER);
      txt(std::to_wstring(g_volPct)+L"%",cx+cw-60,y,60,30,15,1,1,1,1.f,false,DWRITE_TEXT_ALIGNMENT_TRAILING);
      y+=44;
      txt(L"Własne dźwięki (WAV lub MP3)",cx,y,cw,26,15,1.f,0.86f,0.30f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING); y+=32;
      for(int i=0;i<SOUND_COUNT;i++,y+=38){
         const bool mut=SoundSystem::instance().isMuted(i); const std::wstring& cp=SoundSystem::instance().customPath(i);
         txt(SOUND_LABELS[i],cx,y,190,32,14.5f,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         std::wstring cur=mut?L"(bez dźwięku)":cp.empty()?L"domyślny":fileBase(cp);
         rrect(cx+194,y+2,cw-194-196,28,6,0,0,0,0.30f);
         txt(cur,cx+202,y+2,cw-194-196-16,28,13.5f,mut?0.75f:1.f,mut?0.75f:1.f,mut?0.75f:0.92f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         float bx=cx+cw-190;
         uiButton(g_set.hits,bx,y+1,70,30,L"Wybierz",SH_SBROWSE,i,false,true,13.f);
         uiButton(g_set.hits,bx+76,y+1,36,30,L"▶",SH_SPLAY,i,false,!mut,13.f);
         uiButton(g_set.hits,bx+116,y+1,36,30,L"✕",SH_SMUTE,i,mut,true,13.f);
         uiButton(g_set.hits,bx+156,y+1,36,30,L"↺",SH_SDEF,i,false,mut||!cp.empty(),13.f);
      }
      break;}
   case 4:{                                                                  // Controls
      uiNote(L"Kliknij pole skrótu i naciśnij klawisz. Każda akcja ma dwa skróty. Klawisz przypisany do jednej akcji jest zabierany innej.",cx,y-4,cw,40); y+=38;
      for(int i=0;i<KA_COUNT;i++,y+=32){
         txt(KA_LABELS[i],cx,y,240,30,14.5f,0.95f,0.97f,0.95f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
         for(int s=0;s<2;s++){
            float bx=cx+250+s*160; int id=i*2+s;
            bool cap=(g_set.capture==id);
            uiButton(g_set.hits,bx,y,104,28,cap?std::wstring(L"Naciśnij…"):vkName(g_keys[i].key[s]),SH_KEY,id,cap,true,13.5f);
            uiButton(g_set.hits,bx+108,y,28,28,L"✕",SH_KEYCLR,id,false,g_keys[i].key[s]!=0,12.f);
         }
      }
      y+=6;
      uiButton(g_set.hits,cx,y,200,34,L"Przywróć domyślne",SH_KEYDEF,0);
      uiNote(L"Stałe: Ctrl+Z (cofnij), Enter (czat), Esc (zamknij okno).",cx+212,y+4,cw-212,30);
      break;}
   }
}
static void settingsSetVolume(float mx){
   float v=(mx-g_set.sx)/std::max(1.f,g_set.sw)*100.f;
   g_volPct=std::max(0,std::min(100,(int)std::lround(v))); g_dirty=true;
}
static void settingsMouseDown(float mx,float my){
   const float pw=std::min(880.f,G.w-30.f), ph=std::min(620.f,G.h-30.f), px=std::floor((G.w-pw)/2.f), py=std::floor((G.h-ph)/2.f);
   if(g_set.capture>=0){ g_set.capture=-1; g_dirty=true; }                         // a click cancels the waiting for a key
   for(const SHit& h:g_set.hits){
      if(mx<h.x||mx>h.x+h.w||my<h.y||my>h.y+h.h) continue;
      switch(h.kind){
      case SH_CLOSE: showSettings(); return;
      case SH_GROUP: g_setGroup=h.a; saveSettings(); break;
      case SH_CHECK:
         if(h.a==0) g_checkUpdates=!g_checkUpdates;
         else if(h.a==1){ g_muted=!g_muted; if(g_muted) SoundSystem::instance().fadeOutAll(100); else snd("click"); }
         else if(h.a==2) g_autoMoves=!g_autoMoves;
         saveSettings(); break;
      case SH_LEVEL: g_level=(g_level+1)%3; saveSettings(); break;
      case SH_FORCE: g_forceMode=h.a; saveSettings(); break;
      case SH_SLIDER: g_set.dragSlider=true; SetCapture(g_hwnd); settingsSetVolume(mx); break;
      case SH_SBROWSE:{
         wchar_t path[MAX_PATH]={};
         OPENFILENAMEW ofn={}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
         ofn.lpstrFilter=L"Pliki dźwiękowe (*.wav;*.mp3)\0*.wav;*.mp3\0WAV (*.wav)\0*.wav\0MP3 (*.mp3)\0*.mp3\0Wszystkie pliki\0*.*\0";
         ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
         wchar_t title[64]={}; wcsncpy(title,SOUND_LABELS[h.a],63); ofn.lpstrTitle=title;
         if(GetOpenFileNameW(&ofn)){
            SoundSystem::instance().setCustomPath(h.a,path); SoundSystem::instance().setMuted(h.a,false);
            SoundSystem::instance().fadeOutAll(100); SoundSystem::instance().playIdx(h.a,g_volPct/100.f);
            saveSettings();
         }
         break;}
      case SH_SPLAY: SoundSystem::instance().fadeOutAll(150); SoundSystem::instance().playIdx(h.a,g_volPct/100.f); break;
      case SH_SMUTE: SoundSystem::instance().setMuted(h.a,true); SoundSystem::instance().fadeOutAll(100); saveSettings(); break;
      case SH_SDEF:  SoundSystem::instance().setMuted(h.a,false); SoundSystem::instance().setCustomPath(h.a,L""); saveSettings(); break;
      case SH_KEY:   g_set.capture=h.a; break;
      case SH_KEYCLR: g_keys[h.a/2].key[h.a%2]=0; saveSettings(); break;
      case SH_KEYDEF: keysDefaults(); saveSettings(); break;
      }
      g_dirty=true; return;
   }
   if(mx<px||mx>px+pw||my<py||my>py+ph){ showSettings(); return; }                // a click outside the panel closes it
}
static void settingsMouseMove(float mx){ if(g_set.dragSlider) settingsSetVolume(mx); }
static void settingsMouseUp(){
   if(g_set.dragSlider){ g_set.dragSlider=false; ReleaseCapture(); saveSettings(); snd("click"); g_dirty=true; }
}
static void settingsKey(WPARAM k){
   if(g_set.capture>=0){
      if(k==VK_ESCAPE){ g_set.capture=-1; }
      else if(keyBindable((DWORD)k)){ keySet(g_set.capture/2,g_set.capture%2,(DWORD)k); g_set.capture=-1; saveSettings(); }
      g_dirty=true; return;
   }
   if(k==VK_ESCAPE||keyAction((DWORD)k)==KA_SETTINGS) showSettings();
}

static void statsDraw(){
   if(!g_stat.open||!g_rt||!g_dw) return;
   g_stat.hits.clear();
   const float pw=std::min(820.f,G.w-30.f), ph=std::min(520.f,G.h-30.f), px=std::floor((G.w-pw)/2.f), py=std::floor((G.h-ph)/2.f);
   uiPanel(px,py,pw,ph,L"Statystyki",L"Rozegrane i zakończone partie z tego komputera",g_stat.hits);
   const float x0=px+30, w=pw-60;
   const float cols[6]={0.26f,0.14f,0.14f,0.14f,0.12f,0.20f};          // widths as fractions
   const wchar_t* heads[6]={L"Tryb gry",L"Rozegrane",L"Wygrane",L"Przegrane",L"Remisy",L"% wygranych"};
   float y=py+92;
   { float x=x0; for(int c=0;c<6;c++){ txt(heads[c],x+(c==0?0.f:0.f),y,w*cols[c],30,15,1.f,0.86f,0.30f,1.f,true,c==0?DWRITE_TEXT_ALIGNMENT_LEADING:DWRITE_TEXT_ALIGNMENT_CENTER); x+=w*cols[c]; } }
   y+=34; rrect(x0,y,w,1.5f,0,1.f,0.86f,0.30f,0.45f); y+=8;
   const wchar_t* names[5]={L"Komputer: Łatwy",L"Komputer: Normalny",L"Komputer: Trudny",L"Gra przez sieć",L"Razem"};
   StatRow tot; for(int i=0;i<4;i++){ tot.games+=g_stats[i].games; tot.wins+=g_stats[i].wins; tot.draws+=g_stats[i].draws; }
   for(int r=0;r<5;r++){
      const StatRow& s= r<4?g_stats[r]:tot;
      if(r==4){ rrect(x0,y,w,1.2f,0,1,1,1,0.25f); y+=8; }
      float x=x0; const bool b=(r==4);
      int losses=s.games-s.wins-s.draws;
      std::wstring cell[6]={names[r],std::to_wstring(s.games),std::to_wstring(s.wins),std::to_wstring(losses),std::to_wstring(s.draws),L"—"};
      float pct=0.f; if(s.games>0){ pct=100.f*s.wins/s.games; wchar_t pb[24]; swprintf(pb,24,L"%.1f%%",pct); cell[5]=pb; }
      for(int c=0;c<6;c++){
         txt(cell[c],x,y,w*cols[c],38,16,0.95f,0.97f,0.95f,1.f,b,c==0?DWRITE_TEXT_ALIGNMENT_LEADING:DWRITE_TEXT_ALIGNMENT_CENTER);
         if(c==5 && s.games>0){ float bw=w*cols[5]-40; rrect(x+20,y+34,bw,4,2,1,1,1,0.15f); rrect(x+20,y+34,bw*pct/100.f,4,2,1.f,0.86f,0.30f,0.95f); }
         x+=w*cols[c];
      }
      y+=46;
   }
   y+=10;
   uiNote(L"% wygranych to wygrane podzielone przez rozegrane partie (remis liczy się jako rozegrana, a niewygrana). Partie z komputerem liczą się na poziomie, na którym je rozpoczęto, i tylko gdy zostały dokończone. Gry hot seat nie są liczone.",x0,y,w,60);
   uiButton(g_stat.hits,x0,py+ph-60,190,36,L"Wyzeruj statystyki",SH_STATRESET,0);
}
static void statsMouseDown(float mx,float my){
   const float pw=std::min(820.f,G.w-30.f), ph=std::min(520.f,G.h-30.f), px=std::floor((G.w-pw)/2.f), py=std::floor((G.h-ph)/2.f);
   for(const SHit& h:g_stat.hits){
      if(mx<h.x||mx>h.x+h.w||my<h.y||my>h.y+h.h) continue;
      if(h.kind==SH_CLOSE){ showStats(); return; }
      if(h.kind==SH_STATRESET){
         if(MessageBoxW(g_hwnd,L"Wyzerować wszystkie statystyki? Tego nie można cofnąć.",L"Statystyki",MB_YESNO|MB_ICONQUESTION)==IDYES){
            for(auto& s:g_stats) s=StatRow(); saveSettings();
         }
         g_dirty=true; return;
      }
   }
   if(mx<px||mx>px+pw||my<py||my>py+ph) showStats();
}

// --- lobby panel: two tabs - local network / internet ---
struct NpField{ const wchar_t* label; std::wstring* text; size_t maxLen; int kind; float x,y,w,h; };   // kind: 0 text, 1 address, 2 room code
struct NpLayout{
   float px,py,pw,ph, cbx,cby, tabx[2],taby,tabw,tabh, bx[3],by,bw,bh, infoY;
   std::vector<NpField> f;
};
static NpLayout npLayout(){
   NpLayout L; L.pw=std::min(660.f,G.w-30.f); L.ph=std::min(570.f,G.h-24.f);
   L.px=std::floor((G.w-L.pw)/2.f); L.py=std::max(12.f,std::floor((G.h-L.ph)/2.f));
   L.cbx=L.px+L.pw-52; L.cby=L.py+14;
   L.tabw=(L.pw-60-12)/2; L.tabh=38; L.taby=L.py+86; L.tabx[0]=L.px+30; L.tabx[1]=L.tabx[0]+L.tabw+12;
   const float fx=L.px+30, fw=L.pw-60, fy=L.py+168, fh=36, gap=76;
   auto add=[&](const wchar_t* lab,std::wstring* t,size_t mx,int kind,float x,float y,float w){ L.f.push_back({lab,t,mx,kind,x,y,w,fh}); };
   add(L"Twój nick",&g_nickW,16,0,fx,fy,fw);
   if(g_np.tab==0){
      add(L"Adres IP hosta (do połączenia)",&g_ipW,45,1,fx,fy+gap,fw);
      L.by=fy+2*gap+2;
   } else {
      add(L"Adres serwera",&g_serverW,100,1,fx,fy+gap,fw);
      const float w1=(fw-12)*0.62f;
      add(L"Hasło serwera (od znajomych)",&g_inviteW,40,0,fx,fy+2*gap,w1);
      add(L"Kod pokoju (do dołączenia)",&g_codeW,4,2,fx+w1+12,fy+2*gap,fw-w1-12);
      L.by=fy+3*gap+2;
   }
   L.bh=42; L.bw=(fw-24)/3;
   for(int i=0;i<3;i++) L.bx[i]=fx+i*(L.bw+12);
   L.infoY=L.by+L.bh+14;
   return L;
}
static void npButton(float x,float y,float w,float h,const wchar_t* label,bool en){
   rrect(x,y,w,h,8,1,1,1,en?0.16f:0.06f); rrect(x,y,w,h,8,1,1,1,en?0.42f:0.12f,false,1.f);
   txt(label,x,y,w,h,15,1,1,1,en?1.f:0.4f,true);
}
static void npField(const NpField& f,bool focus,bool enabled){
   txt(f.label,f.x,f.y-24,f.w,22,14,0.92f,0.95f,0.92f,enabled?1.f:0.6f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   rrect(f.x,f.y,f.w,f.h,8,0,0,0,0.35f);
   if(focus&&enabled) rrect(f.x,f.y,f.w,f.h,8,1.f,0.86f,0.30f,0.95f,false,2.f); else rrect(f.x,f.y,f.w,f.h,8,1,1,1,0.30f,false,1.f);
   bool caret=focus&&enabled&&((int)(nowSec()*2.0)%2==0);
   txt(*f.text+(caret?L"|":L""),f.x+10,f.y,f.w-20,f.h,16,1,1,1,enabled?1.f:0.55f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
}
static std::wstring npInfoText(){
   if(g_net.playing) return L"Trwa gra z: "+g_net.peerNick+L".";
   if(g_net.connected) return L"Połączono"+(g_net.peerNick.empty()?std::wstring():L" z "+g_net.peerNick)+L". Uzgadnianie…";
   if(g_net.listening && g_net.online) return L"Pokój otwarty. Podaj znajomemu ten kod i adres serwera. Czekam, aż dołączy…";
   if(g_net.listening){
      std::wstring s=L"Czekam na znajomego (port "+std::to_wstring(net::DEFAULT_PORT)+L").\nPodaj mu jeden z adresów swojego komputera:\n";
      for(auto& a:g_myAddrs) s+=L"      "+net::fromUtf8(a)+L"\n";
      if(g_myAddrs.empty()) s+=L"      (nie udało się ustalić adresu)\n";
      return s;
   }
   if(g_net.connecting) return g_net.online ? L"Łączenie z serwerem…" : L"Łączenie…";
   if(!g_np.info.empty()) return g_np.info;
   if(g_np.tab==1) return L"Jeden z Was klika „Utwórz pokój” i podaje znajomemu kod, drugi wpisuje kod i klika „Dołącz”. Nick jest zastrzeżony na serwerze tylko dla Ciebie.";
   return L"Jeden z Was klika „Hostuj grę”, drugi wpisuje adres IP hosta i klika „Połącz”.\nW sieci lokalnej adres hosta to np. 192.168.0.12.";
}
static void drawNetPanel(){
   if(!g_np.open||!g_rt||!g_dw) return;
   NpLayout L=npLayout();
   rrect(0,0,G.w,G.h,0,0,0,0,0.65f);
   rrect(L.px+5,L.py+8,L.pw,L.ph,16,0,0,0,0.40f);
   rrect(L.px,L.py,L.pw,L.ph,16,0.05f,0.16f,0.10f,0.99f);
   rrect(L.px,L.py,L.pw,L.ph,16,0.95f,0.80f,0.30f,0.85f,false,2.f);
   txt(L"Gra z drugim graczem",L.px+28,L.py+14,L.pw-120,40,28,1.f,0.86f,0.25f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   txt(g_np.tab==1?L"Przez internet: serwer łączy Was kodem pokoju":L"W sieci lokalnej: jeden gracz hostuje, drugi się łączy",L.px+29,L.py+52,L.pw-60,22,14,0.82f,0.90f,0.84f,0.9f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   rrect(L.px+24,L.py+80,L.pw-48,1.5f,0,1,1,1,0.20f);
   rrect(L.cbx,L.cby,34,34,8,1,1,1,0.14f); rrect(L.cbx,L.cby,34,34,8,1,1,1,0.35f,false,1.f);
   g_ren.drawLine(L.cbx+11,L.cby+11,L.cbx+23,L.cby+23,2.2f,255,255,255,230); g_ren.drawLine(L.cbx+23,L.cby+11,L.cbx+11,L.cby+23,2.2f,255,255,255,230);
   const bool busy=netBusy();
   const wchar_t* tabNames[2]={L"Sieć lokalna",L"Internet"};
   for(int i=0;i<2;i++){
      bool act=(g_np.tab==i);
      rrect(L.tabx[i],L.taby,L.tabw,L.tabh,8,1,1,1,act?0.22f:0.06f);
      rrect(L.tabx[i],L.taby,L.tabw,L.tabh,8,act?1.f:1.f,act?0.86f:1.f,act?0.30f:1.f,act?0.9f:0.25f,false,act?2.f:1.f);
      txt(tabNames[i],L.tabx[i],L.taby,L.tabw,L.tabh,15,1,1,1,busy&&!act?0.4f:1.f,true);
   }
   if(g_np.focus>=(int)L.f.size()) g_np.focus=0;
   for(size_t i=0;i<L.f.size();i++) npField(L.f[i],(int)i==g_np.focus,!busy);
   const wchar_t* b0=g_np.tab==1?L"Utwórz pokój":L"Hostuj grę", *b1=g_np.tab==1?L"Dołącz":L"Połącz";
   npButton(L.bx[0],L.by,L.bw,L.bh,b0,!busy);
   npButton(L.bx[1],L.by,L.bw,L.bh,b1,!busy);
   npButton(L.bx[2],L.by,L.bw,L.bh,g_net.playing?L"Opuść grę":L"Rozłącz",busy);
   float iy=L.infoY;
   if(g_net.online && g_net.listening && !g_net.roomCode.empty()){          // the room code, big
      txt(L"Kod pokoju",L.px+30,iy,L.pw-60,22,14,0.92f,0.95f,0.92f,1.f,true);
      txt(net::fromUtf8(g_net.roomCode),L.px+30,iy+16,L.pw-60,70,60,1.f,0.86f,0.25f,1.f,true);
      iy+=92;
   }
   txtWrap(npInfoText(),L.px+30,iy,L.pw-60,L.py+L.ph-iy-12,14.5f,0.93f,0.96f,0.93f,1.f);
}
static void npMouseDown(float mx,float my){
   NpLayout L=npLayout();
   auto in=[&](float x,float y,float w,float h){ return mx>=x&&mx<=x+w&&my>=y&&my<=y+h; };
   if(in(L.cbx,L.cby,34,34)||!in(L.px,L.py,L.pw,L.ph)){ g_np.open=false; g_dirty=true; return; }
   const bool busy=netBusy();
   if(!busy){
      for(int i=0;i<2;i++) if(in(L.tabx[i],L.taby,L.tabw,L.tabh)&&g_np.tab!=i){ g_np.tab=i; g_np.focus=0; g_np.info.clear(); g_dirty=true; return; }
      for(size_t i=0;i<L.f.size();i++) if(in(L.f[i].x,L.f[i].y,L.f[i].w,L.f[i].h)){ g_np.focus=(int)i; g_dirty=true; return; }
   }
   if(in(L.bx[0],L.by,L.bw,L.bh)&&!busy){ if(g_nickW.empty()) g_nickW=L"Gracz"; saveSettings(); if(g_np.tab==1) netOnlineStart(true); else netHostStart(); }
   else if(in(L.bx[1],L.by,L.bw,L.bh)&&!busy){ if(g_nickW.empty()) g_nickW=L"Gracz"; saveSettings(); if(g_np.tab==1) netOnlineStart(false); else netJoinStart(); }
   else if(in(L.bx[2],L.by,L.bw,L.bh)&&busy){ netDisconnect(); }
   g_dirty=true;
}
static void npChar(wchar_t c){
   if(c==27){ g_np.open=false; g_dirty=true; return; }
   if(netBusy()) return;
   NpLayout L=npLayout();
   if(g_np.focus>=(int)L.f.size()) g_np.focus=0;
   const NpField& fld=L.f[g_np.focus]; std::wstring& f=*fld.text;
   if(c==9){ g_np.focus=(g_np.focus+1)%(int)L.f.size(); }
   else if(c==13){
      if(g_np.focus+1<(int)L.f.size()) g_np.focus++;
      else { if(g_nickW.empty()) g_nickW=L"Gracz"; saveSettings(); if(g_np.tab==1) netOnlineStart(g_codeW.empty()); else netJoinStart(); }   // the last field: act
   }
   else if(c==8){ if(!f.empty()) f.pop_back(); }
   else if(c==22){ std::wstring t=clipboardText(); for(wchar_t ch:t) if(f.size()<fld.maxLen) f.push_back(ch); }
   else if(c>=32&&c!=127&&f.size()<fld.maxLen){
      if(fld.kind==1 && !((c>=L'0'&&c<=L'9')||c==L'.'||c==L':'||c==L'/'||c==L'-'||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'))) return;   // an address
      if(fld.kind==2){ if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'))) return; c=(wchar_t)towupper(c); }                 // a room code
      f.push_back(c);
   }
   g_dirty=true;
}
// --- chat ---
struct ChatRects{ float x,y,w,h, ex,ey,ew,eh, ix,iy,iw,ih, mx,my,mw,mh; };
static ChatRects chatRects(){
   ChatRects r; r.w=std::min(360.f,G.w-24.f); r.h=290; r.x=G.w-r.w-14; r.y=G.h-SB-r.h-10;
   r.mx=r.x+12; r.my=r.y+34; r.mw=r.w-24; r.mh=r.h-34-96;
   r.ex=r.x+12; r.ey=r.y+r.h-90; r.ew=(r.w-24)/NUM_EMOTES; r.eh=38;
   r.ix=r.x+12; r.iy=r.y+r.h-44; r.iw=r.w-24; r.ih=32;
   return r;
}
static void drawChat(){
   if(!g_chat.open||!g_net.playing||!g_rt) return;
   ChatRects r=chatRects();
   rrect(r.x,r.y,r.w,r.h,12,0.04f,0.13f,0.09f,0.95f); rrect(r.x,r.y,r.w,r.h,12,0.95f,0.80f,0.30f,0.7f,false,1.5f);
   txt(L"Czat z "+g_net.peerNick,r.x+14,r.y+4,r.w-60,28,15,1.f,0.86f,0.25f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   g_ren.drawLine(r.x+r.w-26,r.y+10,r.x+r.w-14,r.y+22,2.f,255,255,255,220); g_ren.drawLine(r.x+r.w-14,r.y+10,r.x+r.w-26,r.y+22,2.f,255,255,255,220);
   // the newest lines at the bottom of the log area
   g_rt->PushAxisAlignedClip(D2D1::RectF(r.mx,r.my,r.mx+r.mw,r.my+r.mh),D2D1_ANTIALIAS_MODE_ALIASED);
   float lineH=20.f; int maxLines=(int)(r.mh/lineH);
   int start=std::max(0,(int)g_chatLog.size()-maxLines);
   float y=r.my+r.mh-(float)((int)g_chatLog.size()-start)*lineH;
   for(int i=start;i<(int)g_chatLog.size();i++,y+=lineH){
      const ChatLine& c=g_chatLog[i];
      txt(c.who+L": "+c.text,r.mx,y,r.mw,lineH,13.5f,c.mine?0.75f:1.f,c.mine?0.90f:0.92f,c.mine?1.f:0.6f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   }
   g_rt->PopAxisAlignedClip();
   for(int i=0;i<NUM_EMOTES;i++){
      float x=r.ex+i*r.ew;
      rrect(x+2,r.ey,r.ew-4,r.eh,8,1,1,1,0.10f);
      txtEmoji(EMOJIS[i],x+2,r.ey,r.ew-4,r.eh,r.ew>=40?24.f:20.f,1.f);
   }
   rrect(r.ix,r.iy,r.iw,r.ih,8,0,0,0,0.35f); rrect(r.ix,r.iy,r.iw,r.ih,8,1.f,0.86f,0.30f,0.8f,false,1.5f);
   bool caret=((int)(nowSec()*2.0)%2==0);
   std::wstring shown=g_chat.input; if(shown.size()>34) shown=shown.substr(shown.size()-34);
   txt(g_chat.input.empty()?std::wstring(L"Napisz wiadomość i naciśnij Enter")+(caret?L"":L""):shown+(caret?L"|":L""),r.ix+8,r.iy,r.iw-16,r.ih,14,
       g_chat.input.empty()?0.65f:1.f,g_chat.input.empty()?0.72f:1.f,g_chat.input.empty()?0.66f:1.f,1.f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
}
static bool chatMouseDown(float mx,float my){
   if(!g_chat.open||!g_net.playing) return false;
   ChatRects r=chatRects();
   if(mx<r.x||mx>r.x+r.w||my<r.y||my>r.y+r.h) return false;           // outside: the game gets the click
   if(mx>=r.x+r.w-34&&my<=r.y+30){ g_chat.open=false; g_dirty=true; return true; }
   if(my>=r.ey&&my<=r.ey+r.eh&&mx>=r.ex&&mx<r.ex+r.ew*NUM_EMOTES){
      int i=(int)((mx-r.ex)/r.ew); if(i>=0&&i<NUM_EMOTES){ netSend("EMO "+std::to_string(i)); emoteShow(i,0); }
   }
   return true;
}
static void chatChar(wchar_t c){
   if(c==27){ g_chat.open=false; g_dirty=true; return; }
   if(c==13){
      if(g_chat.input.empty()) return;
      netSend("CHAT "+net::toUtf8(g_chat.input)); chatAdd(g_nickW,g_chat.input,true); g_chat.input.clear(); return;
   }
   if(c==8){ if(!g_chat.input.empty()) g_chat.input.pop_back(); }
   else if(c==22){ std::wstring t=clipboardText(); for(wchar_t ch:t) if(g_chat.input.size()<200) g_chat.input.push_back(ch); }
   else if(c>=32&&c!=127&&g_chat.input.size()<200) g_chat.input.push_back(c);
   g_dirty=true;
}
// floating emotes above the magazines and the nicknames in a network game
static void drawNetExtras(double now){
   if(!g_net.playing && !g_hot) return;
   for(int p=0;p<2;p++){
      std::wstring n= g_hot ? hotName(p) : p==0 ? g_nickW : g_net.peerNick;
      txt(n,slotX(1)+G.cw*0.30f,rowY(p)+G.ch*0.5f-14,G.cw*1.9f,28,15,1,1,1,0.92f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   }
   if(g_net.online && g_net.hasH2h){                                       // my record against this opponent
      std::wstring r=L"Bilans z "+g_net.peerNick+L": "+std::to_wstring(g_net.h2hW)+L":"+std::to_wstring(g_net.h2hL);
      if(g_net.h2hD>0) r+=L"  (remisy: "+std::to_wstring(g_net.h2hD)+L")";
      txt(r,slotX(1)+G.cw*0.30f,rowY(0)+G.ch*0.5f+10,G.cw*3.2f,24,13,0.85f,0.92f,0.85f,0.9f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   }
   for(size_t i=0;i<g_emotes.size();){
      double age=now-g_emotes[i].t0;
      if(age>3.2){ g_emotes.erase(g_emotes.begin()+i); continue; }
      float a=age<2.4?1.f:(float)(1.0-(age-2.4)/0.8), rise=(float)(age*10.0);
      float cx=slotX(1)+G.cw*0.55f, cy=rowY(g_emotes[i].who)+G.ch*0.18f-rise;
      txtEmoji(EMOJIS[g_emotes[i].idx],cx-G.cw*0.5f,cy-G.ch*0.25f,G.cw*1.4f,G.ch*0.6f,std::min(54.f,G.cw*0.62f),a);
      ++i;
   }
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
   drawNetExtras(now);

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
   } else if(g_dragging && g_game.srcTop(g_drag.pile,actor())){
      for(int d=0;d<NP;d++){
         bool ok=g_game.canMove(g_drag.pile,d,actor()) || (g_drag.pile==turnedId(actor())&&d==wasteId(actor()));
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
      std::wstring t=g_hot?(g_game.winner<0?std::wstring(L"Remis"):hotName(g_game.winner)+L" wygrywa"):g_game.winner==0?L"Wygrywasz!":g_game.winner==1?L"Komputer wygrał":L"Remis";
      txt(t,0,G.h/2-70,G.w,80,64,1.f,0.86f,0.2f,1.f,true);
      txt(L"Kliknij „Nowa gra” (F2), aby zagrać ponownie",0,G.h/2+10,G.w,40,20,1,1,1,0.9f,false);
   }
   // status bar
   rrect(0,G.h-SB,G.w,SB,0,0,0,0,0.38f);
   if(g_statusUntil>0 && now>g_statusUntil){ g_statusUntil=0; statusForTurn(); }
   if(g_statusErr) txt(g_status,12,G.h-SB,G.w-24,SB,14,1.f,0.5f,0.5f,1.f,true,DWRITE_TEXT_ALIGNMENT_LEADING);
   else            txt(g_status,12,G.h-SB,G.w-24,SB,14,1,1,1,0.85f,false,DWRITE_TEXT_ALIGNMENT_LEADING);
   drawChat();
   drawNetPanel();
   settingsDraw();
   statsDraw();
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
      if(ptype(id)==PT_HAND&&pidx(id)!=actor()) continue;  // the other player's deck is not clickable (his magazine and waste can be targets)
      const RectF4& r=PR[id];
      if(x>=r.l&&x<=r.r&&y>=r.t&&y<=r.b) return id;
   }
   return -1;
}
static int hitButton(float x,float y){
   layoutButtons();
   for(int i=0;i<B_COUNT;i++){
      if(!btnVisible(i)) continue;
      const Btn& b=g_btn[i];
      if(x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h) return i;
   }
   return -1;
}
static void showSettings();
static void showStats();
static void closeOverlays();
static void doButton(int id){
   switch(id){
   case B_NEW:     if(g_net.playing) netRematchRequest(); else newGameStart(); break;
   case B_UNDO:    undoMove(); break;
   case B_HINT:    doHint(); break;
   case B_NET:     closeOverlays(); g_np.open=true; g_np.focus=0; if(g_myAddrs.empty()) g_myAddrs=net::localAddresses(); break;
   case B_HOT:
      if(g_net.playing) break;
      if(!g_game.over && g_game.totalTurns>0 &&
         MessageBoxW(g_hwnd,g_hot?L"Zakończyć grę dwóch graczy i zacząć nową grę z komputerem?\nBieżąca partia zostanie porzucona.":L"Zacząć nową grę w trybie hot seat (dwóch graczy przy jednym komputerze)?\nBieżąca partia zostanie porzucona.",
                     L"Hot seat",MB_YESNO|MB_ICONQUESTION)!=IDYES) break;
      g_hot=!g_hot; newGameStart(); break;
   case B_CHAT:    g_chat.open=!g_chat.open; if(g_chat.open) g_chat.unread=0; break;
   case B_STATS:   showStats(); break;
   case B_SETTINGS: showSettings(); break;
   case B_RULES:   showRules(); break;
   }
   g_dirty=true;
}
// Drop target for a dragged card: the valid pile under the cursor, else the valid pile it overlaps most.
static int dropTarget(int src,float mx,float my,float cardX,float cardY){
   auto valid=[&](int d){ return g_game.canMove(src,d,actor())||(src==turnedId(actor())&&d==wasteId(actor())); };
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
   if(src==turnedId(actor())&&dst==wasteId(actor())){ humanDiscard(); return; }
   humanMove(src,dst);
}

// ---------------------------------------------------------------------------
// Moving a whole sequence of a column: done card by card, each move animated.
// ---------------------------------------------------------------------------
static bool seqTarget(int src,int idx,int dst,std::vector<Move>& plan){
   if(idx==0 && g_game.pile[dst].empty()) return false;          // moving a whole column to an empty one is pointless
   return planSequenceMove(g_game,src,idx,dst,plan,40,actor());
}
static void startSeqPlan(const std::vector<Move>& plan){
   { Move must; if(g_game.mandatory(actor(),must)){ obligationBlocked(must); return; } }   // a column move while a foundation move is pending
   pushUndo();                                                  // the whole sequence is a single undo step
   g_plan.active=true; g_plan.moves=plan; g_plan.next=0; g_plan.at=nowSec()+0.22;   // let the dragged cards return first
   g_prev.active=false;
   setStatus(plan.size()>1?L"Przenoszę sekwens kolejnymi ruchami…":L"Przenoszę kartę…");
}
static void planTick(double now){
   if(!g_plan.active||now<g_plan.at||anyAnimating(now)) return;
   if(g_plan.next>=g_plan.moves.size()){ g_plan.active=false; statusForTurn(); return; }
   Move m=g_plan.moves[g_plan.next];
   if(!g_game.canMove(m.src,m.dst,actor())){ g_plan.active=false; relayout(); statusForTurn(); return; }   // cannot happen; stay safe
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
   if(g_set.open){ settingsMouseDown((float)mx,(float)my); return; }
   if(g_stat.open){ statsMouseDown((float)mx,(float)my); return; }
   if(g_np.open){ npMouseDown((float)mx,(float)my); return; }
   if(chatMouseDown((float)mx,(float)my)) return;
   int b=hitButton((float)mx,(float)my);
   if(b>=0){ if(btnEnabled(b)) doButton(b); return; }
   if(!humanTurn()) return;
   int p=hitPile((float)mx,(float)my);
   g_drag=decltype(g_drag)(); g_drag.down=true; g_drag.pile=p; g_drag.mx=mx; g_drag.my=my;
   if(p>=0 && p!=handId(actor())){
      const Card* c=g_game.srcTop(p,actor());
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
   if(g_set.open){ settingsMouseMove((float)mx); return; }
   if(g_stat.open||g_np.open) return;
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
   if(g_set.open){ settingsMouseUp(); return; }
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
            if(h>=0 && ptype(h)==PT_TAB && h!=src && g_game.canPlace(g_game.pile[src][idx],h,actor(),src)){
               snd("nono"); setStatus(L"Za mało wolnego miejsca, żeby przenieść ten sekwens.",true,3);
            } else if(h>=0 && h!=src) snd("nono");
            relayout();
         }
      }
      g_dirty=true; return;
   }
   int p=hitPile((float)mx,(float)my);
   if(p>=0 && p==g_drag.pile && humanTurn()){
      if(g_drag.idx>=0){ if(g_autoMoves) autoSeq(p,g_drag.idx); else setStatus(L"Automatyczne ruchy są wyłączone (Ustawienia → Rozgrywka): przeciągnij kartę.",false,3); }   // click inside a column: move the sequence from that card
      else if(g_drag.badRun){ snd("nono"); setStatus(L"To nie jest sekwens: przenosić można tylko ułożone karty (malejąco, na przemian kolory).",true,3); }
      else autoClick(p);                                          // a plain click moves the card to its best place
   }
}
// ============================================================================
// Computer's turn
// ============================================================================
static void aiTick(double now){
   if(g_net.playing||g_hot) return;             // the opponent is a person: over the network, or at this computer
   if(g_game.over||g_game.turn!=1||now<g_dealUntil||now<g_aiAt) return;
   if(uiBusy(now)) return;
   Game before=g_game;
   Step s=aiStep(g_game,1,g_ctx,g_level);
   recAction(before,'A',1,s.kind,s.m.src,s.m.dst);
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
      applySoundSettings();
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
   case WM_CHAR:
      if(g_help.open||g_set.open||g_stat.open) return 0;
      if(g_np.open){ npChar((wchar_t)wp); return 0; }
      if(g_chat.open && g_net.playing){ chatChar((wchar_t)wp); return 0; }
      return 0;
   case net::WM_NET:{
      std::unique_ptr<std::string> text((std::string*)lp);
      netEvent((int)wp,*text);
      return 0;}
   case WM_KEYDOWN:
      if(g_help.open){ helpKey(wp); return 0; }
      if(g_set.open){ settingsKey(wp); return 0; }
      if(g_stat.open){ if(wp==VK_ESCAPE||keyAction((DWORD)wp)==KA_STATS) showStats(); return 0; }
      if(g_np.open||(g_chat.open&&g_net.playing)) return 0;            // typing: the letters go to WM_CHAR
      if(wp==VK_RETURN && g_net.playing){ g_chat.open=true; g_chat.unread=0; return 0; }
      if(wp==VK_F9){ dumpState(); return 0; }
      if(wp==VK_F6){ debugAutoStep(); return 0; }
      if(wp==VK_F11){ g_forceTie=true; newGameStart(); g_forceTie=false; return 0; }   // debug: a game that starts with identical magazine cards
      if(wp==VK_F10){ if(!g_game.over){ g_game.over=true; g_game.winner=0; afterAnyMove(); } return 0; }   // debug: force a win
      if(wp=='Z' && (GetKeyState(VK_CONTROL)&0x8000)){ undoMove(); return 0; }
      if(GetKeyState(VK_CONTROL)&0x8000) return 0;
      switch(keyAction((DWORD)wp)){
      case KA_NEW:      doButton(B_NEW); break;
      case KA_UNDO:     undoMove(); break;
      case KA_HINT:     doHint(); break;
      case KA_DRAW:     humanDraw(); break;
      case KA_DISCARD:  humanDiscard(); break;
      case KA_NET:      doButton(B_NET); break;
      case KA_HOT:      doButton(B_HOT); break;
      case KA_STATS:    showStats(); break;
      case KA_SETTINGS: showSettings(); break;
      case KA_RULES:    showRules(); break;
      case KA_MUTE:     g_muted=!g_muted; saveSettings(); if(g_muted) SoundSystem::instance().fadeOutAll(100); else snd("click"); break;
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
      if(g_net.connected) netSend("BYE");
      g_conn.close(); g_ws.close();
      saveGame(); saveSettings(); PostQuitMessage(0); return 0;
   }
   return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nShow){
   // Command line (also handy as shortcuts): /host = host a network game at once, /join=ADDRESS = join one,
   // /i2 = a second copy on the same computer (own settings/save files; for testing the network play).
   std::wstring cmdLine=GetCommandLineW();
   bool autoHost=cmdLine.find(L"/host")!=std::wstring::npos;
   std::wstring autoJoin;
   { size_t jp=cmdLine.find(L"/join="); if(jp!=std::wstring::npos){ size_t e=cmdLine.find(L' ',jp); autoJoin=cmdLine.substr(jp+6,e==std::wstring::npos?std::wstring::npos:e-jp-6); } }
   if(cmdLine.find(L"/i2")!=std::wstring::npos) g_instTag=L".i2";
   auto argValue=[&](const wchar_t* key){ std::wstring v; size_t k=cmdLine.find(key); if(k!=std::wstring::npos){ size_t e=cmdLine.find(L' ',k); v=cmdLine.substr(k+wcslen(key),e==std::wstring::npos?std::wstring::npos:e-k-wcslen(key)); } return v; };
   std::wstring argServer=argValue(L"/server="), argInvite=argValue(L"/invite="), argCode=argValue(L"/joincode=");
   bool autoCreate=cmdLine.find(L"/create")!=std::wstring::npos;
   HANDLE mutex=CreateMutexW(nullptr,TRUE,g_instTag.empty()?L"Garibaldka_SingleInstance":L"Garibaldka_SingleInstance_i2");
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
   if(!loadSavedGame()){ g_hot=false; newGameStart(); }
   if(g_checkUpdates) startUpdateCheck();
   { std::wstring n=argValue(L"/nick="); if(!n.empty()) g_nickW=n; }
   if(!argServer.empty()) g_serverW=argServer;
   if(!argInvite.empty()) g_inviteW=argInvite;
   if(autoHost) { g_np.tab=0; netHostStart(); } else if(!autoJoin.empty()){ g_np.tab=0; g_ipW=autoJoin; netJoinStart(); }
   else if(autoCreate){ g_np.tab=1; netOnlineStart(true); } else if(!argCode.empty()){ g_np.tab=1; g_codeW=argCode; netOnlineStart(false); }

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
      netTick(now);
      { static double lastSave=0; static std::string lastText;       // autosave: a killed program or a crash loses nothing
        if(now-lastSave>0.4){ lastSave=now;
           std::string t=g_net.playing?std::string():g_game.over?std::string("over"):g_game.serialize();
           if(t!=lastText){ lastText=t; saveGame(); } } }
      { static double lastPing=0;                      // the server connection is kept alive (answered by the server with #PONG)
        if(g_net.online&&g_ws.connected()&&now-lastPing>20.0){ g_ws.sendLine("#PING"); lastPing=now; } }
      aiTick(now);
      if(g_fw.active() && now-g_fwLast>=0.028){ g_fw.tick((int)G.w,(int)G.h); g_fwLast=now; g_dirty=true; }
      bool flameBurning=g_flame.lit||g_flame.scale>0.01f||g_flame.igniteAt>0;
      bool anim=anyAnimating(now)||g_plan.active||g_dragging||g_prev.active||g_start.active||g_rev.active||g_deal.active||g_fw.active()||!g_emotes.empty()||(g_game.over&&g_overShown&&now-g_overAt<1.0);
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
