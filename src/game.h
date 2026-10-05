#pragma once
// Garibaldka (Russian Bank / crapette) - game rules and computer player.
// Pure C++17, no Windows dependencies (so tools/sim.cpp can run it headless).
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <set>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

enum Suit { Hearts=0, Diamonds=1, Clubs=2, Spades=3 };

struct Card {
   int  suit=0, rank=0;   // rank 1..13 (A..K)
   int  deck=0;           // 0 = player's deck, 1 = computer's deck
   int  id=0;             // unique 0..103  (deck*52 + suit*13 + rank-1)
   bool up=false;         // face up?
   bool isRed() const { return suit==Hearts||suit==Diamonds; }
   // key of the PNG resource (CARD_<key>), e.g. "AH", "TS", "KD"
   std::string imgKey() const {
      static const char* R[]={"","A","2","3","4","5","6","7","8","9","T","J","Q","K"};
      const char S[]="HDCS";
      return std::string(R[rank])+S[suit];
   }
};

// ---------------------------------------------------------------------------
// Piles. Every pile has a fixed integer id:
//   res p (magazyn) 0..1 | hand p (talia) 2..3 | turned p (dobrana) 4..5
//   waste p (smietnik) 6..7 | tab j (kolumny) 8..15 | fnd j (fundamenty) 16..23
// ---------------------------------------------------------------------------
// Foundations are reserved by suit, in order of seniority (as in Pasjans Dziadkowy): spades x2, hearts x2, diamonds x2, clubs x2.
inline int fndSuit(int slot){ static const int S[4]={3,0,1,2}; return S[slot/2]; }   // Suit enum: Hearts=0,Diamonds=1,Clubs=2,Spades=3
enum PType { PT_RES, PT_HAND, PT_TURNED, PT_WASTE, PT_TAB, PT_FND };
static const int NP=24, NUM_TAB=8, NUM_FND=8;
inline int resId(int p)   { return p; }
inline int handId(int p)  { return 2+p; }
inline int turnedId(int p){ return 4+p; }
inline int wasteId(int p) { return 6+p; }
inline int tabId(int j)   { return 8+j; }
inline int fndId(int j)   { return 16+j; }
inline PType ptype(int id){ return id<2?PT_RES:id<4?PT_HAND:id<6?PT_TURNED:id<8?PT_WASTE:id<16?PT_TAB:PT_FND; }
inline int pidx(int id)   { switch(ptype(id)){ case PT_TAB:return id-8; case PT_FND:return id-16; default:return id%2; } }

struct Move { int src=-1, dst=-1; };

// Portable, fully specified shuffle: the same seed gives the same permutation on EVERY compiler, standard
// library and machine. std::mt19937 is bit-for-bit standardized (same seed -> same raw 32-bit values), but
// std::shuffle and std::uniform_int_distribution are NOT (their algorithms are implementation-defined) - that
// is what gave different deals for the same seed on different computers. Here only mt19937::operator()() is
// used; the reduction to a bounded index (rejection sampling, unbiased) and the Fisher-Yates loop are our own.
inline uint32_t boundedRand(std::mt19937& g,uint32_t bound){
   uint32_t threshold=(0u-bound)%bound;                 // values below it would make r%bound biased: rejected
   for(;;){ uint32_t r=g(); if(r>=threshold) return r%bound; }
}
template<class T> inline void portableShuffle(std::vector<T>& v,std::mt19937& g){
   for(size_t i=v.size();i>1;){ --i; uint32_t j=boundedRand(g,(uint32_t)(i+1)); std::swap(v[i],v[j]); }
}

enum StepKind { ST_MOVE, ST_DRAW, ST_DISCARD, ST_PASS };
struct Step { StepKind kind=ST_PASS; Move m; };

class Game {
public:
   std::vector<Card> pile[NP];
   int  turn=0;            // whose turn: 0 = player, 1 = computer
   bool over=false;
   int  winner=-1;         // 0, 1, or -1 = draw (stalemate)
   int  idle=0;            // consecutive turns without a single move
   int  turnMoves=0;
   int  totalTurns=0;
   uint32_t seed=0;        // the seed of the deal: the same seed gives the same game everywhere
   std::mt19937 rng{std::random_device{}()};   // only for the computer player's noise, never for the deal

   // Deals a new game. Each player: 13 cards magazine (12 down + 1 up),
   // 4 cards to the shared columns, the remaining 35 in the hand. Then decideStart() picks who starts.
   // forceTie (debug key F11): makes the magazine cards - and the top hand cards - of both players identical.
   void newGame(bool forceTie=false){ newGame(std::random_device{}(),forceTie); }
   void newGame(uint32_t dealSeed,bool forceTie){
      seed=dealSeed; std::mt19937 g(dealSeed);
      for(auto& p:pile) p.clear();
      over=false; winner=-1; idle=0; turnMoves=0; totalTurns=0;
      for(int pl=0;pl<2;pl++){
         std::vector<Card> dk;
         for(int s=0;s<4;s++) for(int r=1;r<=13;r++){
            Card c; c.suit=s; c.rank=r; c.deck=pl; c.id=pl*52+s*13+r-1; c.up=false; dk.push_back(c);
         }
         portableShuffle(dk,g);
         for(int i=0;i<13;i++) pile[resId(pl)].push_back(dk[i]);
         pile[resId(pl)].back().up=true;
         for(int k=0;k<4;k++){ Card c=dk[13+k]; c.up=true; pile[tabId(pl*4+k)].push_back(c); }
         for(size_t i=17;i<dk.size();i++) pile[handId(pl)].push_back(dk[i]);
      }
      if(forceTie){
         auto makeSame=[&](const Card& want,Card& target){        // swap, inside the computer's deck, the card equal to `want` into `target`
            for(auto& pl:pile) for(Card& x:pl) if(x.deck==1&&x.suit==want.suit&&x.rank==want.rank){
               bool ux=x.up, ut=target.up; std::swap(x,target); x.up=ux; target.up=ut; return;
            }
         };
         makeSame(pile[resId(0)].back(),pile[resId(1)].back());
         makeSame(pile[handId(0)].back(),pile[handId(1)].back());
      }
      decideStart();
   }

   // ---- who starts ----
   // 1. The older (higher) magazine card: A lowest ... K highest.
   // 2. Equal ranks: the suit decides (spades, hearts, diamonds, clubs).
   // 3. Identical cards: both players turn over the top card of their hand and the same comparison
   //    is made (rank, then suit); if those are identical too, the next cards, and so on.
   //    The turned-over cards are put back under their hands afterwards.
   static int suitPrecedence(int suit){ static const int P[4]={3,2,1,4}; return P[suit]; }   // Hearts,Diamonds,Clubs,Spades
   enum StartHow { SH_MAG_RANK=0, SH_MAG_SUIT=1, SH_HAND_RANK=2, SH_HAND_SUIT=3 };
   int  startHow=SH_MAG_RANK;
   int  startCardId=-1;                                // the card that decided (the starter's)
   std::vector<std::pair<int,int>> startReveals;       // hand cards turned over, in order: (player 0's id, player 1's id)
   bool startedBySuit() const { return startHow==SH_MAG_SUIT||startHow==SH_HAND_SUIT; }
   void decideStart(){
      startReveals.clear(); startCardId=-1; startHow=SH_MAG_RANK;
      int w=0; bool bySuit=false;
      auto decide=[&](const Card& a,const Card& b)->bool{
         if(a.rank!=b.rank){ w=a.rank>b.rank?0:1; bySuit=false; return true; }
         int sa=suitPrecedence(a.suit), sb=suitPrecedence(b.suit);
         if(sa!=sb){ w=sa>sb?0:1; bySuit=true; return true; }
         return false;
      };
      const Card& ma=pile[resId(0)].back(); const Card& mb=pile[resId(1)].back();
      if(decide(ma,mb)){ turn=w; startHow=bySuit?SH_MAG_SUIT:SH_MAG_RANK; startCardId=(w==0?ma.id:mb.id); return; }
      std::vector<Card> shown[2];
      turn=(int)(seed&1u);                             // only if the hands run out (cannot realistically happen)
      while(!pile[handId(0)].empty()&&!pile[handId(1)].empty()){
         Card ca=pile[handId(0)].back(); pile[handId(0)].pop_back(); ca.up=true;
         Card cb=pile[handId(1)].back(); pile[handId(1)].pop_back(); cb.up=true;
         shown[0].push_back(ca); shown[1].push_back(cb); startReveals.push_back({ca.id,cb.id});
         if(decide(ca,cb)){ turn=w; startHow=bySuit?SH_HAND_SUIT:SH_HAND_RANK; startCardId=(w==0?ca.id:cb.id); break; }
      }
      for(int pl=0;pl<2;pl++) for(Card c:shown[pl]){ c.up=false; pile[handId(pl)].insert(pile[handId(pl)].begin(),c); }   // under the hand
   }

   const Card* top(int id) const { return pile[id].empty()?nullptr:&pile[id].back(); }

   // Can `c` be put on pile dst by player p (src = pile it comes from, or -1)?
   bool canPlace(const Card& c,int dst,int p,int src) const {
      PType t=ptype(dst);
      if(t==PT_FND){
         const Card* tp=top(dst);
         return tp ? (tp->suit==c.suit && c.rank==tp->rank+1) : (c.rank==1 && c.suit==fndSuit(pidx(dst)));   // an ace only starts a foundation of its own suit
      }
      if(t==PT_TAB){
         if(src==dst) return false;
         const Card* tp=top(dst);
         return !tp || (tp->isRed()!=c.isRed() && tp->rank==c.rank+1);
      }
      if((t==PT_RES||t==PT_WASTE) && pidx(dst)==1-p){      // opponent's magazine / waste
         const Card* tp=top(dst);
         return tp && tp->suit==c.suit && std::abs(tp->rank-c.rank)==1;
      }
      return false;
   }
   // The only cards a player can pick up: own magazine/waste/turned top, any column top.
   const Card* srcTop(int src,int p) const {
      PType t=ptype(src);
      if(t==PT_TAB) return top(src);
      if((t==PT_RES||t==PT_WASTE||t==PT_TURNED) && pidx(src)==p) return top(src);
      return nullptr;
   }
   bool canMove(int src,int dst,int p) const {
      const Card* c=srcTop(src,p);
      if(!c||src==dst) return false;
      if(ptype(src)==PT_TAB && ptype(dst)==PT_TAB && pile[dst].empty() && pile[src].size()==1) return false;
      return canPlace(*c,dst,p,src);
   }
   Card doMove(int src,int dst,int p){
      auto& sp=pile[src];
      Card c=sp.back(); sp.pop_back(); c.up=true;
      pile[dst].push_back(c);
      if(ptype(src)==PT_RES && !sp.empty()) sp.back().up=true;
      turnMoves++;
      if(remaining(p)==0){ over=true; winner=p; }   // won: magazine, hand, turned card and waste are all gone
      return c;
   }
   // Cards player p still has to get rid of: magazine + hand + turned card + waste.
   int remaining(int p) const {
      return (int)(pile[resId(p)].size()+pile[handId(p)].size()+pile[turnedId(p)].size()+pile[wasteId(p)].size());
   }
   bool canDraw(int p) const { return pile[turnedId(p)].empty() && (!pile[handId(p)].empty()||!pile[wasteId(p)].empty()); }
   void draw(int p){
      auto& h=pile[handId(p)];
      if(h.empty()){
         std::vector<Card> w(pile[wasteId(p)].rbegin(),pile[wasteId(p)].rend());
         for(auto& c:w) c.up=false;
         pile[handId(p)]=w; pile[wasteId(p)].clear();
      }
      Card c=pile[handId(p)].back(); pile[handId(p)].pop_back(); c.up=true;
      pile[turnedId(p)].push_back(c);
   }
   void endTurn(){
      if(turnMoves==0) idle++; else idle=0;
      turnMoves=0; turn=1-turn;
      if(idle>=24){ over=true; winner=-1; }       // nobody can play: draw
      if(++totalTurns>=400 && !over){             // endless back-and-forth: fewer cards left wins
         over=true;
         int a=remaining(0), b=remaining(1);
         winner = a<b?0 : b<a?1 : -1;
      }
   }
   void discard(int p){
      Card c=pile[turnedId(p)].back(); pile[turnedId(p)].pop_back(); c.up=true;
      pile[wasteId(p)].push_back(c);
      endTurn();
   }
   // Strict obligation: every card the player can pick up (magazine top, turned card, waste top,
   // any column top) that fits a foundation MUST be put there. Order of checking: magazine, turned
   // card, waste, columns. Whoever ends the turn (draws, discards, passes) with such a move left loses the turn.
   bool mandatory(int p,Move& out) const {
      int srcs[3+NUM_TAB]={resId(p),turnedId(p),wasteId(p)};
      for(int j=0;j<NUM_TAB;j++) srcs[3+j]=tabId(j);
      for(int src:srcs){
         if(!srcTop(src,p)) continue;
         for(int f=0;f<NUM_FND;f++) if(canMove(src,fndId(f),p)){ out.src=src; out.dst=fndId(f); return true; }
      }
      return false;
   }

   // ---- network play: the same game seen from the other player's chair ----
   // Each computer shows ITSELF as player 0 (bottom row, blue deck). Player 1 of one computer is player 0 of the other,
   // so the whole game state has to be mirrored: players swapped (magazine/hand/turned/waste piles, decks, turn,
   // the 4+4 columns), foundations stay (their suits are fixed). mirrored() is its own inverse.
   static int mirrorPile(int id){ if(id<8) return id^1; if(id<16) return 8+((id-8)+4)%8; return id; }
   static int mirrorCardId(int id){ return (1-id/52)*52+id%52; }
   Game mirrored() const {
      Game m;
      m.seed=seed; m.turn=1-turn; m.over=over; m.winner=winner<0?-1:1-winner;
      m.idle=idle; m.turnMoves=turnMoves; m.totalTurns=totalTurns;
      m.startHow=startHow; m.startCardId=startCardId<0?-1:mirrorCardId(startCardId);
      for(auto& pr:startReveals) m.startReveals.push_back({mirrorCardId(pr.second),mirrorCardId(pr.first)});
      for(int id=0;id<NP;id++) for(const Card& c:pile[id]){
         Card d=c; d.deck=1-c.deck; d.id=mirrorCardId(c.id); m.pile[mirrorPile(id)].push_back(d);
      }
      return m;
   }
   // 64-bit fingerprint of the whole game state (FNV-1a over serialize()), to detect that two computers diverged.
   uint64_t hash() const {
      std::string t=serialize(); uint64_t h=1469598103934665603ULL;
      for(unsigned char ch:t){ h^=ch; h*=1099511628211ULL; }
      return h;
   }

   // ---- saving / loading (single line of text, all values separated by spaces) ----
   std::string serialize() const {
      std::ostringstream o;
      o<<"GARIBALDKA1 "<<turn<<' '<<(int)over<<' '<<winner<<' '<<idle<<' '<<turnMoves<<' '<<totalTurns;
      for(int id=0;id<NP;id++){
         o<<' '<<pile[id].size();
         for(const Card& c:pile[id]) o<<' '<<(c.id*2+(c.up?1:0));
      }
      return o.str();
   }
   // Returns false (and leaves the game untouched) if the text is not a complete, consistent game.
   bool deserialize(const std::string& text){
      std::istringstream in(text);
      std::string magic; in>>magic; if(magic!="GARIBALDKA1") return false;
      int t,ov,w,idl,tm,tt; if(!(in>>t>>ov>>w>>idl>>tm>>tt)) return false;
      if((t!=0&&t!=1)||w<-1||w>1) return false;
      std::vector<Card> np[NP]; bool seen[104]={};
      for(int id=0;id<NP;id++){
         int n; if(!(in>>n)||n<0||n>104) return false;
         for(int i=0;i<n;i++){
            int v; if(!(in>>v)||v<0||v>=208) return false;
            int cid=v/2; if(seen[cid]) return false; seen[cid]=true;
            Card c; c.id=cid; c.deck=cid/52; c.suit=(cid%52)/13; c.rank=cid%13+1; c.up=(v&1)!=0;
            np[id].push_back(c);
         }
      }
      for(int i=0;i<104;i++) if(!seen[i]) return false;
      for(int id=0;id<NP;id++) pile[id]=np[id];
      turn=t; over=ov!=0; winner=w; idle=idl; turnMoves=tm; totalTurns=tt;
      return true;
   }
};

// ===========================================================================
// Moving a whole sequence of columns cards (several cards at once).
// Cards can only be moved one at a time, but a sequence can still be moved when the single moves
// are possible: the cards of the sequence may be parked on empty columns and on column tops
// that accept them. planSequenceMove() looks for the SHORTEST list of single-card moves between
// columns that achieves it (only the sequence's own cards are ever moved).
//   src, dst: column pile ids; idx: index in src of the first (lowest, biggest) card of the sequence.
// ===========================================================================
inline bool isRunFrom(const Game& g,int src,int idx){
   const auto& sp=g.pile[src]; int n=(int)sp.size();
   if(idx<0||idx>=n) return false;
   for(int i=idx;i<n;i++){
      if(!sp[i].up) return false;
      if(i>idx && (sp[i-1].isRed()==sp[i].isRed() || sp[i-1].rank!=sp[i].rank+1)) return false;
   }
   return true;
}
inline bool planSequenceMove(const Game& g,int src,int idx,int dst,std::vector<Move>& plan,int maxMoves=40){
   plan.clear();
   if(ptype(src)!=PT_TAB||ptype(dst)!=PT_TAB||src==dst||!isRunFrom(g,src,idx)) return false;
   const auto& sp=g.pile[src]; const int n=(int)sp.size(), k=n-idx;
   if(!g.canPlace(sp[idx],dst,0,src)) return false;                 // the first card must fit on dst
   if(k==1){ if(!g.canMove(src,dst,0)) return false; plan.push_back({src,dst}); return true; }

   Card info[104]; bool isRun[104]={};
   for(int j=0;j<NUM_TAB;j++) for(const Card& c:g.pile[tabId(j)]) info[c.id]=c;
   std::vector<int> run; for(int i=idx;i<n;i++){ run.push_back(sp[i].id); isRun[sp[i].id]=true; }
   typedef std::vector<std::vector<int>> Cols;
   struct Node{ Cols cols; int parent; Move mv; int depth; };
   Cols start(NUM_TAB);
   for(int j=0;j<NUM_TAB;j++) for(const Card& c:g.pile[tabId(j)]) start[j].push_back(c.id);
   const int sCol=src-8, dCol=dst-8;
   const size_t dstBase=start[dCol].size();
   auto key=[&](const Cols& c){ std::string k; for(auto& col:c){ for(int id:col){ k+=(char)(id+1); } k+=(char)0; } return k; };
   std::vector<Node> nodes; nodes.push_back({start,-1,Move(),0});
   std::unordered_set<std::string> seen; seen.insert(key(start));
   for(size_t head=0;head<nodes.size()&&nodes.size()<150000;head++){
      Node cur=nodes[head];                       // copy: nodes may grow below
      if(cur.cols[dCol].size()==dstBase+k){
         bool ok=true; for(int i=0;i<k;i++) if(cur.cols[dCol][dstBase+i]!=run[i]){ ok=false; break; }
         if(ok){
            std::vector<Move> rev; for(int at=(int)head;nodes[at].parent>=0;at=nodes[at].parent) rev.push_back(nodes[at].mv);
            plan.assign(rev.rbegin(),rev.rend()); return true;
         }
      }
      if(cur.depth>=maxMoves) continue;
      for(int c=0;c<NUM_TAB;c++){
         if(cur.cols[c].empty()) continue;
         int id=cur.cols[c].back(); if(!isRun[id]) continue;          // only the sequence's own cards move
         const Card& cc=info[id];
         for(int d=0;d<NUM_TAB;d++){
            if(d==c) continue;
            if(cur.cols[d].empty()){ if(cur.cols[c].size()==1) continue; }          // pointless: lone card to an empty column
            else { const Card& t=info[cur.cols[d].back()]; if(t.isRed()==cc.isRed()||t.rank!=cc.rank+1) continue; }
            Cols nc=cur.cols; nc[c].pop_back(); nc[d].push_back(id);
            if(!seen.insert(key(nc)).second) continue;
            nodes.push_back({nc,(int)head,{tabId(c),tabId(d)},cur.depth+1});
         }
      }
   }
   (void)sCol;
   return false;
}

// ===========================================================================
// Computer player. See AI_RULES.md for the 10 rules this implements.
// ===========================================================================
struct AIContext { std::set<int> moved; };   // cards already shuffled column->column this turn

inline std::vector<Move> legalMoves(const Game& g,int p){
   std::vector<Move> out;
   std::vector<int> srcs={resId(p),wasteId(p),turnedId(p)};
   for(int j=0;j<NUM_TAB;j++) srcs.push_back(tabId(j));
   std::vector<int> dsts;
   for(int f=0;f<NUM_FND;f++) dsts.push_back(fndId(f));
   for(int j=0;j<NUM_TAB;j++) dsts.push_back(tabId(j));
   dsts.push_back(resId(1-p)); dsts.push_back(wasteId(1-p));
   for(int s:srcs){
      if(!g.srcTop(s,p)) continue;
      bool seenF=false,seenT=false;
      for(int d:dsts){
         if(!g.canMove(s,d,p)) continue;
         PType dt=ptype(d);
         if(dt==PT_FND && g.pile[d].empty()){ if(seenF) continue; seenF=true; }
         if(dt==PT_TAB && g.pile[d].empty()){ if(seenT) continue; seenT=true; }
         out.push_back({s,d});
      }
   }
   return out;
}

inline bool anyFoundation(const Game& g,const Card& c,int p){
   for(int f=0;f<NUM_FND;f++) if(g.canPlace(c,fndId(f),p,-1)) return true;
   return false;
}
inline bool oppCanUse(const Game& g,const Card& c,int q){
   if(anyFoundation(g,c,q)) return true;
   for(int j=0;j<NUM_TAB;j++) if(g.canPlace(c,tabId(j),q,-1)) return true;
   return false;
}

// level: 0 = easy, 1 = normal, 2 = hard  (the game is luck-heavy, so extra "hard" tuning made no measurable difference)
static const float REJECTED=-1000.f;     // score of a move that must never be played
// Returns the value of a move WITHOUT noise; a move worth <= 0 is never played or hinted.
inline float scoreMove(const Game& g,const Move& m,int p,const AIContext& ctx,int level){
   const Card& c=*g.srcTop(m.src,p);
   PType st=ptype(m.src), dt=ptype(m.dst);
   int q=1-p; float sc=0;
   const Card* myRes=g.top(resId(p));
   // Pointless "undo-able" move: taking the top card off my magazine/waste shows the card T below it
   // (judged only when T is already face up, no peeking). If the opponent can put a card of his on T
   // at once (same suit, rank +-1; e.g. my queen leaves for the free column and he puts it back on my
   // king) AND I cannot play T away myself right now, then the position just returns to where it was
   // and I have only used up my free place and my turn: the move is rejected.
   if((st==PT_RES||st==PT_WASTE) && dt!=PT_FND){
      const auto& pl=g.pile[m.src];
      if(pl.size()>1 && pl[pl.size()-2].up){
         const Card& T=pl[pl.size()-2];
         bool playable=anyFoundation(g,T,p);
         for(int j=0;j<NUM_TAB&&!playable;j++){ int id=tabId(j); if(id!=m.dst) playable=g.canPlace(T,id,p,-1); }
         int qs[2]={resId(q),wasteId(q)};
         for(int k=0;k<2&&!playable;k++) if(qs[k]!=m.dst) playable=g.canPlace(T,qs[k],p,-1);
         if(!playable){
            auto touches=[&](const Card* x){ return x && x->suit==T.suit && std::abs(x->rank-T.rank)==1; };
            bool covered=false;                                    // cards the opponent could use, after my move
            for(int j=0;j<NUM_TAB&&!covered;j++){ int id=tabId(j); covered=touches(id==m.dst?&c:g.top(id)); }
            for(int k=0;k<2&&!covered;k++) covered=touches(qs[k]==m.dst?&c:g.top(qs[k]));
            if(covered) return REJECTED;
         }
      }
   }
   // 1-3: obligation, empty the magazine, feed the foundations
   if(st==PT_RES && dt==PT_FND) sc+=1000;
   else if(st==PT_RES){
      sc+=500;
      const auto& pl=g.pile[m.src];
      if(pl.size()>1 && !pl[pl.size()-2].up) sc+=80;
   }
   if(dt==PT_FND && st!=PT_RES) sc+=300;
   // 4: dig in the columns / 10: no loops
   if(st==PT_TAB){
      const auto& pl=g.pile[m.src];
      const Card* nt=pl.size()>1?&pl[pl.size()-2]:nullptr;
      if(dt==PT_TAB && ctx.moved.count(c.id)) return REJECTED;
      if(nt){
         if(anyFoundation(g,*nt,p)) sc+=250;
         else if(myRes && myRes->rank+1==nt->rank && myRes->isRed()!=nt->isRed()) sc+=400;
         else if(dt==PT_TAB) sc-=30;
      } else sc+=(myRes?60:20);          // the column becomes empty (free space), wherever the card goes
   }
   // 5: block the opponent's magazine
   if(dt==PT_RES && level>=2){
      if(!oppCanUse(g,c,q)){
         sc+=350;
         const Card* t=g.top(m.dst);
         if(t && anyFoundation(g,*t,q)) sc+=200;
      } else sc-=40;
   } else if(dt==PT_RES) sc+=100;
   // 6: don't cover a playable column top / 8: empty columns / 9: flat columns
   if(dt==PT_TAB){
      const Card* t=g.top(m.dst);
      if(t){
         if(anyFoundation(g,*t,p)) sc-=150;
         sc-=(float)g.pile[m.dst].size();
      } else {
         if(st==PT_RES) sc+=100; else if(c.rank==13) sc+=20; else sc-=60;
      }
   }
   // 7: waste and turned card
   if(st==PT_WASTE){
      const auto& pl=g.pile[m.src];
      if(dt==PT_FND||dt==PT_TAB) sc+=90;                                   // recycle the waste
      else if(pl.size()>1 && anyFoundation(g,pl[pl.size()-2],p)) sc+=250;   // uncovers a foundation card
   }
   if(st==PT_TURNED && dt==PT_TAB) sc+=60;
   if(dt==PT_WASTE && st==PT_TURNED) sc+=20;
   return sc;
}
inline float moveNoise(int level,std::mt19937& rng){
   return (float)(rng()%1000)/1000.f*(level<=1?60.f:3.f);
}

inline bool aiChoose(const Game& g,int p,const AIContext& ctx,int level,std::mt19937& rng,Move& out){
   // Obligation (rule 1): any card that fits a foundation goes there first (strict obligation).
   if(g.mandatory(p,out)) return true;
   float best=0; bool found=false;
   for(const Move& m:legalMoves(g,p)){
      float base=scoreMove(g,m,p,ctx,level);
      if(base<=0) continue;                       // pointless move: never played
      float sc=base+moveNoise(level,rng);
      if(sc>best){ best=sc; out=m; found=true; }
   }
   // weaker levels sometimes overlook a useful optional move
   if(found && level<2 && rng()%(level==0?2:4)==0) return false;
   return found;
}

// Strict obligation, checked at the moment of a move: while some card can go to a foundation, the only
// allowed moves are foundation moves. Any other move (to a column, onto the opponent's piles...) means the
// player forgot, and loses the turn (the move itself is not carried out).
inline bool breaksObligation(const Game& g,int p,int dst){
   Move m;
   return ptype(dst)!=PT_FND && g.mandatory(p,m);
}

// What a click on the card at the top of `src` does for the player (player 0): the best of ALL legal moves.
// The computer's valuation only ranks them; even a move the computer would never choose is played when it is
// the only one (the click is the player's decision, and there is Undo). false = no legal move at all
// (for the turned card the caller then discards it to the waste pile).
inline bool bestClickMove(const Game& g,int src,Move& out){
   AIContext none; float best=-1e9f; bool found=false;
   for(int d=0;d<NP;d++){
      if(!g.canMove(src,d,0)) continue;
      float sc=scoreMove(g,{src,d},0,none,2);
      if(sc>best){ best=sc; out={src,d}; found=true; }
   }
   return found;
}

// One computer action: a move, else turn a card over, else discard / pass.
inline Step aiStep(Game& g,int p,AIContext& ctx,int level){
   Step s; Move m;
   if(aiChoose(g,p,ctx,level,g.rng,m)){
      int id=g.srcTop(m.src,p)->id;
      g.doMove(m.src,m.dst,p);
      if(ptype(m.src)==PT_TAB && ptype(m.dst)==PT_TAB) ctx.moved.insert(id);
      s.kind=ST_MOVE; s.m=m; return s;
   }
   if(g.top(turnedId(p))){ g.discard(p); s.kind=ST_DISCARD; return s; }
   if(g.canDraw(p)){ g.draw(p); s.kind=ST_DRAW; return s; }
   g.endTurn(); s.kind=ST_PASS; return s;
}
