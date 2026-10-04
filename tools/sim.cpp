// Headless computer-vs-computer simulation: checks that games finish and rules hold.
//   zig c++ -std=c++17 -O2 tools/sim.cpp -o build/sim.exe && build/sim.exe [games] [level0] [level1]
#include "../src/game.h"
#include <cstdio>
int main(int argc,char** argv){
   int n=argc>1?atoi(argv[1]):200, l0=argc>2?atoi(argv[2]):1, l1=argc>3?atoi(argv[3]):1;
   int w[3]={0,0,0}; long turns=0; int maxT=0, full=0, capped=0;
   for(int i=0;i<n;i++){
      Game g; g.newGame(); AIContext ctx; int t=0; long guard=0;
      while(!g.over && guard++<200000){
         int p=g.turn; Move mm; bool must=g.mandatory(p,mm);
         Step s=aiStep(g,p,ctx,p==0?l0:l1);
         if(must && s.kind!=ST_MOVE){ printf("OBLIGATION IGNORED (player %d)\n",p); return 1; }
         if(s.kind==ST_DISCARD||s.kind==ST_PASS){ ctx=AIContext(); t++; }
         // invariants: 104 cards always on the table
         if(guard%500==0){ size_t c=0; for(auto&pl:g.pile) c+=pl.size(); if(c!=104){ printf("CARD COUNT BROKEN %zu\n",c); return 1; } }
      }
      size_t c=0; for(auto&pl:g.pile) c+=pl.size();
      if(c!=104){ printf("CARD COUNT BROKEN %zu\n",c); return 1; }
      if(g.winner>=0){ if(g.remaining(g.winner)==0) full++; else capped++; }
      w[g.over?(g.winner<0?2:g.winner):2]++; turns+=t; maxT=std::max(maxT,t);
   }
   printf("(%d won by emptying everything, %d decided by the turn limit)  ",full,capped); printf("games=%d  p0(level %d) wins=%d  p1(level %d) wins=%d  draws=%d  avg turns=%.1f  max=%d\n",n,l0,w[0],l1,w[1],w[2],(double)turns/n,maxT);
}
