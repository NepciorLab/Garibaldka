// Prints a saved game: zig c++ -std=c++17 tools/showsave.cpp -o build/showsave.exe && build/showsave.exe file.sav
#include "../src/game.h"
#include <fstream>
#include <iostream>
#include <sstream>
static std::string nm(int id){ static const char* N[]={"res","hand","turned","waste"}; char b[24];
   if(ptype(id)==PT_TAB) sprintf(b,"col%d",id-8+1); else if(ptype(id)==PT_FND) sprintf(b,"fnd%d",id-16+1); else sprintf(b,"%s.%s",N[id/2],id%2?"opp":"me"); return b; }
int main(int argc,char** argv){
   std::ifstream f(argv[1]); std::stringstream ss; ss<<f.rdbuf();
   Game g; if(!g.deserialize(ss.str())){ puts("cannot load"); return 1; }
   printf("turn=%d over=%d idle=%d turnMoves=%d totalTurns=%d\n",g.turn,(int)g.over,g.idle,g.turnMoves,g.totalTurns);
   for(int id=0;id<NP;id++){ printf("%-10s(%2zu):",nm(id).c_str(),g.pile[id].size()); size_t from=g.pile[id].size()>8?g.pile[id].size()-8:0;
      for(size_t i=from;i<g.pile[id].size();i++) printf(" %s%s",g.pile[id][i].imgKey().c_str(),g.pile[id][i].up?"":"(dn)"); puts(""); }
   for(int p=0;p<2;p++){ printf("player %d legal:",p); for(auto m:legalMoves(g,p)) printf(" %s>%s",g.srcTop(m.src,p)->imgKey().c_str(),nm(m.dst).c_str()); puts(""); }
}
