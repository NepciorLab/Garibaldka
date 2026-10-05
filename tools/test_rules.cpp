// Regression tests for the computer player's judgement (no graphics).
//   python -m ziglang c++ -std=c++17 -O2 tools/test_rules.cpp -o build/test_rules.exe && build/test_rules.exe
#include "../src/game.h"
#include <cstdio>
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
   printf(fails?"\n%d FAILED\n":"\nall passed\n",fails);
   return fails?1:0;
}
