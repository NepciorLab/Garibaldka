#pragma once
#include <windows.h>
#include <vector>
#include <cmath>
#include <random>
#include "game.h"

// ---------------------------------------------------------------------------
// Deal pop-in animation
// ---------------------------------------------------------------------------
struct PopIn {
   Card  card;
   int   col, cardIdx;
   float progress;
   float speed;
   bool  started()   const { return progress > 0.f; }
   bool  done()      const { return progress >= 1.f; }
   float scale()     const {
      float t = progress < 0.f ? 0.f : (progress > 1.f ? 1.f : progress);
      return 1.f - (1.f-t)*(1.f-t);
   }
};

class DealAnimator {
public:
   void clear() { m_pops.clear(); }
   bool busy()  const { return !m_pops.empty(); }
   void queue(const Card& c, int col, int cardIdx, float stagger) {
      PopIn p; p.card=c; p.col=col; p.cardIdx=cardIdx;
      p.progress=-stagger; p.speed=0.20f;
      m_pops.push_back(p);
   }
   std::vector<PopIn> tick() {
      std::vector<PopIn> done;
      for(auto& p : m_pops) p.progress += 1.f;
      std::vector<PopIn> remaining;
      for(auto& p : m_pops) {
         if(p.done()) done.push_back(p);
         else         remaining.push_back(p);
      }
      m_pops=remaining;
      return done;
   }
   const PopIn* current() const {
      for(auto& p : m_pops)
         if(p.started() && !p.done()) return &p;
      return nullptr;
   }
   bool anyStarted() const {
      for(auto& p : m_pops) if(p.started()) return true;
      return false;
   }
private:
   std::vector<PopIn> m_pops;
};

// (The fireworks live in fireworks2.h.)
