// Makes a test save: a game whose player 0 has the ace of spades on top of the magazine (a foundation move is obligatory).
//   zig c++ -std=c++17 tools/mksave.cpp -o build/mksave.exe && build/mksave.exe out.sav
#include "../src/game.h"
#include <cstdio>
int main(int argc,char** argv){
   Game g; g.newGame(12345u,false); g.turn=0; g.turnMoves=0; g.totalTurns=0; g.idle=0;
   Card c; bool found=false;
   for(int id=0;id<NP&&!found;id++) for(size_t i=0;i<g.pile[id].size();i++){
      const Card& k=g.pile[id][i]; if(k.suit==Spades&&k.rank==1&&k.deck==0){ c=k; g.pile[id].erase(g.pile[id].begin()+i); found=true; break; }
   }
   c.up=true; g.pile[resId(0)].push_back(c);
   std::string t=g.serialize(); FILE* f=fopen(argv[1],"wb"); fwrite(t.data(),1,t.size(),f); fclose(f);
   Move m; printf("mandatory=%d\n",(int)g.mandatory(0,m));
}
