// Regression tests for the computer player's judgement (no graphics).
//   python -m ziglang c++ -std=c++17 -O2 tools/test_rules.cpp -o build/test_rules.exe && build/test_rules.exe
#include "../src/game.h"
#include <cstdio>
// Fingerprints of the deals of seeds 1, 2 and 2026 (recorded once; the shuffle was also cross-checked with an
// independent implementation of MT19937 + the same Fisher-Yates in Python).
#define GOLDEN_SEED_1 5427993316304309282ULL
#define GOLDEN_SEED_2 3493877772122777702ULL
#define GOLDEN_SEED_2026 2286541856716091818ULL
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("FAIL: %s\n",msg); fails++; } else printf("ok:   %s\n",msg); }while(0)

static Card mk(int suit,int rank,int deck,bool up=true){
   Card c; c.suit=suit; c.rank=rank; c.deck=deck; c.id=deck*52+suit*13+rank-1; c.up=up; return c;
}
static Game empty(){ Game g; for(auto& p:g.pile) p.clear(); g.turn=0; return g; }

int main(){
   AIContext ctx; std::mt19937 rng(1);
   // --- 1. Player's magazine (bottom->top): Q# , Kdiamonds, Qdiamonds. One free column.
   //        Moving the top Qdiamonds to the free column would uncover Kdiamonds, and the
   //        opponent could just put that same queen back on it (free space for them).
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Diamonds,12,0),mk(Diamonds,13,0),mk(Diamonds,12,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      for(int j=0;j<7;j++) g.pile[tabId(j)]={mk(Clubs,2+j%3,1)};   // 7 occupied columns
      // column 7 is empty
      Move m{resId(0),tabId(7)};
      CHECK(g.canMove(m.src,m.dst,0),"Qdiamonds -> empty column is a legal move");
      CHECK(scoreMove(g,m,0,ctx,2)<=0,"...but it is NOT worth playing (opponent re-covers the king)");
      Move out; bool found=aiChoose(g,0,ctx,2,rng,out);
      CHECK(!found||!(out.src==resId(0)&&out.dst==tabId(7)),"hint/AI does not choose it");
   }
   // --- 1b. With TWO free columns the king can follow into the second one, so the first move is useful.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Diamonds,12,0),mk(Diamonds,13,0),mk(Diamonds,12,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      for(int j=0;j<6;j++) g.pile[tabId(j)]={mk(Clubs,2+j%3,1)};   // columns 6 and 7 are empty
      Move m{resId(0),tabId(6)};
      CHECK(scoreMove(g,m,0,ctx,2)>0,"two free columns: Qdiamonds -> free column is allowed (the king can follow)");
   }
   // --- 2. Same move is fine when nobody can cover the uncovered king.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Diamonds,13,0),mk(Diamonds,5,0)};
      g.pile[resId(1)]={mk(Hearts,9,1,false),mk(Hearts,9,1)};
      for(int j=0;j<7;j++) g.pile[tabId(j)]={mk(Clubs,2,1)};
      Move m{resId(0),tabId(7)};
      CHECK(scoreMove(g,m,0,ctx,2)>0,"magazine card to empty column is played when the uncovered card is safe");
   }
   // --- 2b. A free column is kept: a card that fits onto another card goes there (click and computer), whether it
   //         comes from the magazine or is the turned card.
   for(int srcKind=0;srcKind<2;srcKind++){
      Game g=empty();
      Card c=mk(Hearts,6,0);                                  // red 6: fits onto the black 7 of column 1
      if(srcKind==0) g.pile[resId(0)]={mk(Spades,3,0,false),c}; else { g.pile[resId(0)]={mk(Spades,3,0)}; g.pile[turnedId(0)]={c}; }
      g.pile[resId(1)]={mk(Hearts,9,1,false),mk(Hearts,9,1)};
      g.pile[tabId(0)]={mk(Clubs,7,1)};
      for(int j=1;j<4;j++) g.pile[tabId(j)]={mk(Clubs,2+j,1)};   // columns 5-8 are empty
      int src=srcKind==0?resId(0):turnedId(0);
      Move best; bool f=bestClickMove(g,src,best);
      CHECK(f&&best.dst==tabId(0),srcKind==0?"click: magazine card goes onto the card, not onto the free column":"click: turned card goes onto the card, not onto the free column");
      AIContext cx; Move out; bool found=false;
      for(const Move& m:legalMoves(g,0)) if(m.src==src && ptype(m.dst)==PT_TAB && g.pile[m.dst].empty()) found|=scoreMove(g,m,0,cx,2)>scoreMove(g,{src,tabId(0)},0,cx,2);
      CHECK(!found,"the free column never beats the card in the valuation");
   }
   // --- 2c. The magazine has priority: 5 clubs on top of 6 clubs, red 6 and red 7 on the table: BOTH cards go,
   //         on Normal and Hard every single time (the weaker levels used to give up at random).
   for(int lvl=1;lvl<=2;lvl++){
      int both=0;
      for(int i=0;i<200;i++){
         Game g=empty(); g.rng.seed(1000+i);
         g.pile[resId(0)]={mk(Spades,3,0,false),mk(Clubs,6,0,false),mk(Clubs,5,0,true)};
         g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,9,1)};
         g.pile[tabId(0)]={mk(Hearts,6,1)}; g.pile[tabId(1)]={mk(Diamonds,7,1)};
         for(int j=2;j<8;j++) g.pile[tabId(j)]={mk(Spades,2+j,1)};
         g.pile[handId(0)]={mk(Hearts,2,0,false)};
         AIContext cx; int moved=0;
         for(int st=0;st<10&&g.turn==0;st++){ Step r=aiStep(g,0,cx,lvl); if(r.kind!=ST_MOVE) break; if(r.m.src==resId(0)) moved++; }   // (in any order: other moves may come in between)
         if(moved>=2) both++;
      }
      CHECK(both==200,lvl==1?"Normal plays both magazine cards (5 clubs, then 6 clubs) every time":"Hard plays both magazine cards every time");
   }
   // --- 2d. The screenshot position (computer on Hard): 6 clubs from the magazine onto the 7 of diamonds (taking it
   //         off the column frees nothing, so the cover rule must not apply), then the 10 and the lone 9 of clubs
   //         onto the player's waste, which empties a column.
   {
      int did6=0, did10=0, did9=0, N=200, level=2;
      for(int i=0;i<N;i++){
         Game g; for(auto& p:g.pile) p.clear(); g.turn=1; g.rng.seed(500+i);
         g.pile[resId(1)]={mk(Clubs,6,1,true),mk(Clubs,5,0,true),mk(Clubs,6,0,true)};            // computer's magazine, top 6 clubs
         g.pile[wasteId(0)]={mk(Spades,11,0),mk(Diamonds,13,0),mk(Clubs,9,0)};                   // the player's waste, top 9 clubs
         g.pile[wasteId(1)]={mk(Spades,11,1)};
         g.pile[tabId(0)]={mk(Clubs,12,0),mk(Hearts,11,0)};
         g.pile[tabId(1)]={mk(Diamonds,13,0),mk(Clubs,12,1),mk(Hearts,11,1),mk(Clubs,10,0)};
         g.pile[tabId(2)]={mk(Diamonds,3,0)};
         g.pile[tabId(3)]={mk(Spades,8,0),mk(Diamonds,7,0)};
         g.pile[tabId(4)]={mk(Clubs,3,0),mk(Diamonds,2,0)};
         g.pile[tabId(5)]={mk(Clubs,9,1)};
         g.pile[tabId(6)]={mk(Clubs,13,0)};
         g.pile[tabId(7)]={mk(Diamonds,8,0),mk(Spades,7,0),mk(Diamonds,6,0),mk(Clubs,5,1)};
         g.pile[handId(1)]={mk(Hearts,2,1,false)}; g.pile[handId(0)]={mk(Hearts,3,0,false)};
         AIContext ctx; bool a=false,b=false,c=false;
         for(int s=0;s<8&&g.turn==1;s++){
            Move m; Step st; int srcCard=-1;
            st=aiStep(g,1,ctx,level);
            if(st.kind!=ST_MOVE) break;
            // identify by destination (after the move the card is on top there)
            const Card* t=g.top(st.m.dst);
            if(t&&t->suit==Clubs&&t->rank==6&&st.m.dst==tabId(3)) a=true;
            if(t&&t->suit==Clubs&&t->rank==10&&st.m.dst==wasteId(0)) b=true;
            if(t&&t->suit==Clubs&&t->rank==9&&st.m.dst==wasteId(0)) c=true;
         }
         did6+=a; did10+=b; did9+=c;
      }

      CHECK(did6==N,"Hard: 6 clubs from the magazine onto the 7 of diamonds (nothing to cover)");
      CHECK(did10==N&&did9==N,"Hard: 10 and 9 of clubs onto the player's waste to free a column");
   }
   // --- 3. Obligation: a turned ace is always played on the foundation (every level).
   for(int lvl=0;lvl<3;lvl++){
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[turnedId(0)]={mk(Spades,1,0)};
      for(int j=0;j<8;j++) g.pile[tabId(j)]={mk(Clubs,9,1)};
      int hits=0; for(int k=0;k<200;k++){ Move o; if(aiChoose(g,0,ctx,lvl,rng,o)&&ptype(o.dst)==PT_FND&&o.src==turnedId(0)) hits++; }
      char b[80]; sprintf(b,"turned ace goes to the foundation, level %d (200/200)",lvl);
      CHECK(hits==200,b);
   }
   // --- 4. A column card put on the opponent's pile with no benefit is not worth playing.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[tabId(0)]={mk(Clubs,9,1),mk(Hearts,7,1)};   // 7 hearts on 9 clubs: uncovers nothing useful
      g.pile[tabId(1)]={mk(Clubs,8,1)};                  // opponent can put the 7hearts back on this 8clubs
      for(int j=2;j<8;j++) g.pile[tabId(j)]={mk(Clubs,2,1)};
      Move m{tabId(0),resId(1)};
      CHECK(g.canMove(m.src,m.dst,0),"7hearts -> opponent's 6hearts is legal");
      CHECK(scoreMove(g,m,0,ctx,2)<=0,"...but pointless: uncovers nothing, frees no column, opponent just takes it back");
   }
   // --- 5. Win condition: magazine, hand, turned card AND waste must all be gone.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Hearts,1,0)};                         // last magazine card: ace
      g.pile[wasteId(0)]={mk(Spades,5,0)};                       // ...but there is still a waste card
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.doMove(resId(0),fndId(2),0);                              // ace of hearts: a hearts foundation
      CHECK(!g.over,"emptying only the magazine does not win (waste card left)");
      g.pile[wasteId(0)].clear(); g.pile[handId(0)]={mk(Hearts,2,0)};
      g.pile[turnedId(0)]={mk(Spades,1,0)};
      g.doMove(turnedId(0),fndId(0),0);                           // ace of spades: a spades foundation
      CHECK(!g.over,"...nor with a card left in the hand");
      g.draw(0); g.doMove(turnedId(0),fndId(2),0);
      CHECK(g.over&&g.winner==0,"getting rid of every card wins");
   }
   // --- 6. Strict obligation: ANY visible card fitting a foundation is obligatory (also a column top).
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[fndId(6)]={mk(Clubs,1,1)};
      g.pile[tabId(0)]={mk(Clubs,9,1),mk(Clubs,2,1)};            // 2 clubs on top of a column: must go on the ace
      for(int j=1;j<8;j++) g.pile[tabId(j)]={mk(Hearts,13,1)};
      Move m;
      CHECK(g.mandatory(0,m)&&m.src==tabId(0)&&m.dst==fndId(6),"a column top that fits a foundation is obligatory");
      for(int lvl=0;lvl<3;lvl++){ Move o; bool f=aiChoose(g,0,ctx,lvl,rng,o); char b[80]; sprintf(b,"computer obeys it at level %d",lvl); CHECK(f&&o.src==tabId(0)&&o.dst==fndId(6),b); }
   }
   // --- 7. Saving and loading round-trips exactly (also rejects broken text).
   {
      Game a; a.newGame(); a.draw(0);
      std::string text=a.serialize();
      Game b; b.newGame();
      CHECK(b.deserialize(text),"a saved game loads");
      bool same=b.serialize()==text&&b.turn==a.turn;
      CHECK(same,"...and is identical after loading");
      Game c; c.newGame(); std::string before=c.serialize();
      CHECK(!c.deserialize("GARIBALDKA1 0 0 -1 0 0 0 5"),"truncated save is rejected");
      CHECK(c.serialize()==before,"...and leaves the current game untouched");
   }
   // --- 8. Moving a whole sequence with the help of single moves (the user's example).
   //        Columns: 9hearts | Jdiamonds | 10clubs,9diamonds,8spades | four kings | one EMPTY column.
   {
      Game g=empty();
      g.pile[tabId(0)]={mk(Hearts,9,0)};
      g.pile[tabId(1)]={mk(Diamonds,11,0)};
      g.pile[tabId(2)]={mk(Clubs,10,1),mk(Diamonds,9,1),mk(Spades,8,1)};
      for(int j=3;j<7;j++) g.pile[tabId(j)]={mk(j-3,13,1)};
      std::vector<Move> plan;
      bool ok=planSequenceMove(g,tabId(2),0,tabId(1),plan);
      CHECK(ok,"10clubs-9diamonds-8spades can be moved onto the jack of diamonds");
      CHECK(plan.size()==5,"...in the 5 single moves of the example (8s->9h, 9d->free, 10c->J, 9d->10c, 8s->9d)");
      Game h=g; bool legal=true;
      for(const Move& m:plan){ if(!h.canMove(m.src,m.dst,0)){ legal=false; break; } h.doMove(m.src,m.dst,0); }
      CHECK(legal,"every single move of the plan is legal");
      bool same=h.pile[tabId(1)].size()==4&&h.pile[tabId(1)][1].rank==10&&h.pile[tabId(1)][2].rank==9&&h.pile[tabId(1)][3].rank==8
                &&h.pile[tabId(2)].empty()&&h.pile[tabId(0)].size()==1&&h.pile[tabId(0)][0].rank==9;
      CHECK(same,"...and the result is the whole sequence on the jack, everything else as before");
      // no free column: nowhere to park the 9 of diamonds
      Game f=g; f.pile[tabId(7)]={mk(Spades,13,0)};
      CHECK(!planSequenceMove(f,tabId(2),0,tabId(1),plan),"without the free column the sequence cannot be moved");
      // not a sequence (wrong colour order) and a destination that does not fit
      Game w=g; w.pile[tabId(2)]={mk(Clubs,10,1),mk(Spades,9,1)};
      CHECK(!planSequenceMove(w,tabId(2),0,tabId(1),plan),"a broken sequence is not movable");
      CHECK(!planSequenceMove(g,tabId(2),0,tabId(0),plan),"the first card must fit the destination");
      CHECK(planSequenceMove(g,tabId(2),2,tabId(0),plan)&&plan.size()==1,"a single top card is a plain move");
   }
   // --- 9. Who starts: magazine rank, then suit (spades > hearts > diamonds > clubs), then hand cards one by one.
   {
      bool consistent=true; int viaReveals=0;
      for(int i=0;i<3000;i++){
         Game g; g.newGame();
         const Card* a=g.top(resId(0)); const Card* b=g.top(resId(1));
         if(g.startReveals.empty()){
            bool rankDiff=a->rank!=b->rank;
            int w=rankDiff?(a->rank>b->rank?0:1):(Game::suitPrecedence(a->suit)>Game::suitPrecedence(b->suit)?0:1);
            if(!rankDiff&&a->suit==b->suit) consistent=false;                 // identical cards must go to the hand cards
            if(g.turn!=w||g.startCardId!=(w==0?a->id:b->id)) consistent=false;
            if(g.startedBySuit()!=!rankDiff) consistent=false;
         } else { viaReveals++; if(a->rank!=b->rank||a->suit!=b->suit) consistent=false; }
         size_t n=0; for(auto& p:g.pile) n+=p.size(); if(n!=104) consistent=false;
         if(g.pile[handId(0)].size()!=35||g.pile[handId(1)].size()!=35) consistent=false;   // shown cards went back under the hands
      }
      CHECK(consistent,"3000 deals: start decided by rank, then suit, identical magazine cards go to the hand cards");
      CHECK(viaReveals>0,"...and identical magazine cards did occur (hand cards were turned over)");
   }
   {  // identical magazine cards (6 hearts vs 6 hearts); hand cards: 5 hearts vs 5 hearts (identical again), then 3 spades vs 3 diamonds
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,2,0,false),mk(Hearts,6,0)};
      g.pile[resId(1)]={mk(Spades,2,1,false),mk(Hearts,6,1)};
      g.pile[handId(0)]={mk(Clubs,2,0,false),mk(Spades,3,0,false),mk(Hearts,5,0,false)};
      g.pile[handId(1)]={mk(Clubs,2,1,false),mk(Diamonds,3,1,false),mk(Hearts,5,1,false)};
      g.decideStart();
      CHECK(g.startReveals.size()==2,"identical magazine cards: two pairs of hand cards are turned over (the first pair is identical too)");
      CHECK(g.turn==0&&g.startCardId==mk(Spades,3,0).id&&g.startHow==Game::SH_HAND_SUIT,"the second pair decides by suit: 3 spades beats 3 diamonds, and it is the card that is shown as the winner");
      bool back=g.pile[handId(0)].size()==3&&!g.pile[handId(0)][0].up&&!g.pile[handId(0)][1].up&&g.pile[handId(0)][2].id==mk(Clubs,2,0).id;
      CHECK(back,"the turned-over cards were put back under the hand, face down");
   }
   // --- 10. Foundations are reserved by suit: spades 0-1, hearts 2-3, diamonds 4-5, clubs 6-7.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Spades,1,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[turnedId(0)]={mk(Hearts,1,0)};
      CHECK(g.canMove(resId(0),fndId(0),0)&&g.canMove(resId(0),fndId(1),0),"an ace of spades fits both spade foundations");
      CHECK(!g.canMove(resId(0),fndId(2),0)&&!g.canMove(resId(0),fndId(6),0),"...and no foundation of another suit");
      CHECK(g.canMove(turnedId(0),fndId(2),0)&&!g.canMove(turnedId(0),fndId(0),0),"an ace of hearts only fits the hearts foundations (2-3)");
      bool order=fndSuit(0)==Spades&&fndSuit(1)==Spades&&fndSuit(2)==Hearts&&fndSuit(3)==Hearts&&fndSuit(4)==Diamonds&&fndSuit(5)==Diamonds&&fndSuit(6)==Clubs&&fndSuit(7)==Clubs;
      CHECK(order,"slot order follows the seniority of suits (spades, hearts, diamonds, clubs)");
   }
   // --- 11. A click moves the card to the best of ALL legal moves; the waste pile only when there is none.
   {
      // turned king, only legal move: an empty column (low value for the computer, but a legal move)
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[turnedId(0)]={mk(Spades,13,0)};
      for(int j=0;j<7;j++) g.pile[tabId(j)]={mk(Clubs,2,1)};                 // column 7 is empty
      Move m;
      CHECK(bestClickMove(g,turnedId(0),m)&&m.dst==tabId(7),"turned king: the empty column is chosen, not the waste pile");
      // turned card whose only legal move is onto the OPPONENT's waste pile (a move the computer finds worthless)
      Game h=empty();
      h.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      h.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      h.pile[wasteId(1)]={mk(Diamonds,6,1)};
      h.pile[turnedId(0)]={mk(Diamonds,7,0)};
      for(int j=0;j<8;j++) h.pile[tabId(j)]={mk(Clubs,2,1)};
      CHECK(bestClickMove(h,turnedId(0),m)&&m.dst==wasteId(1),"turned 7 of diamonds: laid on the opponent's 6 of diamonds, not discarded");
      // a column card whose only legal move is onto the opponent's waste pile
      Game k=h; k.pile[turnedId(0)].clear(); k.pile[tabId(0)]={mk(Clubs,9,1),mk(Diamonds,7,0)};
      CHECK(bestClickMove(k,tabId(0),m)&&m.dst==wasteId(1),"a column card also goes onto the opponent's waste when that is its only move");
      // nothing fits anywhere: no move
      Game n=h; n.pile[turnedId(0)]={mk(Spades,9,0)};
      CHECK(!bestClickMove(n,turnedId(0),m),"no legal move at all: no move (the turned card is then discarded by the caller)");
   }
   // --- 12. Strict obligation at the moment of a move: only foundation moves are allowed while one is pending.
   {
      Game g=empty();
      g.pile[resId(0)]={mk(Spades,3,0,false),mk(Hearts,7,0)};
      g.pile[resId(1)]={mk(Hearts,5,1,false),mk(Hearts,6,1)};
      g.pile[turnedId(0)]={mk(Spades,1,0)};                                  // a drawn ace of spades: must go to a foundation
      for(int j=0;j<8;j++) g.pile[tabId(j)]={mk(Clubs,2+j%5,1)};
      CHECK(breaksObligation(g,0,tabId(3)),"the drawn ace laid on the table breaks the obligation");
      CHECK(breaksObligation(g,0,wasteId(1)),"...and so does laying it on the opponent's pile");
      CHECK(!breaksObligation(g,0,fndId(0)),"a foundation move is always fine");
      Game h=g; h.pile[turnedId(0)]={mk(Spades,5,0)};                         // a 5 of spades: no foundation move pending
      CHECK(!breaksObligation(h,0,tabId(3)),"with no foundation move pending every move is fine");
   }
   // --- 13. Deals are reproducible EVERYWHERE: the same seed gives the same cards (own shuffle, no std::shuffle).
   {
      auto fingerprint=[](uint32_t seed){ Game g; g.newGame(seed,false); return g.hash(); };
      CHECK(fingerprint(12345u)==fingerprint(12345u),"the same seed gives the same game");
      CHECK(fingerprint(12345u)!=fingerprint(12346u),"another seed gives another game");
      // GOLDEN VALUES: these fingerprints were recorded once. If the shuffle ever changes (a different compiler or
      // library, or an edit of portableShuffle) the deals of the same seed would differ between computers/versions.
      CHECK(fingerprint(1u)==GOLDEN_SEED_1 && fingerprint(2u)==GOLDEN_SEED_2 && fingerprint(2026u)==GOLDEN_SEED_2026,
            "golden deals for seeds 1, 2 and 2026 are unchanged");
      printf("      (fingerprints: %llu %llu %llu)\n",(unsigned long long)fingerprint(1u),(unsigned long long)fingerprint(2u),(unsigned long long)fingerprint(2026u));
   }
   // --- 14. Mirroring (the other player's chair) is its own inverse and keeps the rules intact.
   {
      Game g; g.newGame(777u,false); g.draw(0);
      Game m=g.mirrored();
      CHECK(m.mirrored().hash()==g.hash(),"mirrored() twice gives the original game");
      CHECK(m.turn==1-g.turn&&m.pile[turnedId(1)].size()==g.pile[turnedId(0)].size(),"players are swapped in the mirror");
      bool sameCards=true; for(int id=0;id<NP;id++) if(g.pile[id].size()!=m.pile[Game::mirrorPile(id)].size()) sameCards=false;
      CHECK(sameCards,"every pile has its counterpart in the mirror");
      // a move on one side, the mirrored move on the other, gives states that are again mirrors of each other
      Game a=g, b=m; bool ok=true; int moves=0;
      for(int step=0;step<400&&!a.over;step++){
         int p=a.turn; AIContext ctx; Step st=aiStep(a,p,ctx,2);             // a plays (any side)
         if(st.kind==ST_MOVE){ if(!b.canMove(Game::mirrorPile(st.m.src),Game::mirrorPile(st.m.dst),1-p)){ ok=false; break; } b.doMove(Game::mirrorPile(st.m.src),Game::mirrorPile(st.m.dst),1-p); }
         else if(st.kind==ST_DRAW) b.draw(1-p);
         else if(st.kind==ST_DISCARD) b.discard(1-p);
         else b.endTurn();
         moves++;
         if(b.mirrored().hash()!=a.hash()){ ok=false; break; }
      }
      CHECK(ok&&moves>50,"400 computer actions applied to one side and (mirrored) to the other keep both states identical");
   }
   printf(fails?"\n%d FAILED\n":"\nall passed\n",fails);
   return fails?1:0;
}
