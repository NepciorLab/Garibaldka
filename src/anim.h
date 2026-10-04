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

// ---------------------------------------------------------------------------
// Fireworks — realistic multi-burst system with trails
// ---------------------------------------------------------------------------

struct Particle {
   float x,y;       // current position
   float px,py;     // previous position (for trail line)
   float vx,vy;
   float ay;        // gravity
   float drag;      // velocity multiplier per tick (0.96..0.99)
   int   life, maxLife;
   BYTE  r,g,b;     // start colour
   BYTE  er,eg,eb;  // end colour
   float size;
   bool  spark;     // secondary micro-spark — no trail
   bool  strobe;    // blinks every other tick
   bool  sub;       // sub-explosion particle (smaller, shorter life)
};

// Burst types — each produces a distinct visual pattern
enum BurstType {
   BURST_SPHERE,    // classic random sphere with trails
   BURST_WILLOW,    // drooping willow-tree cascade
   BURST_CROSSETTE, // main stars that re-explode mid-flight
   BURST_STROBE,    // strobing ring
   BURST_DOUBLE,    // two concentric rings, offset timing
   BURST_COMET,     // tight fan of long-tailed comets
};

struct Firework {
   float x,y, px,py;   // position + previous (for rocket trail)
   float vx,vy;
   bool  exploded=false;
   int   flashLife=0;
   BYTE  cr,cg,cb;
   BurstType burstType;
   // Crossette: sub-explosions scheduled
   struct SubBoom { float x,y,vx,vy; int delay; BYTE r,g,b; };
   std::vector<SubBoom> subBooms;
   std::vector<Particle> parts;
   bool dead() const { return exploded && parts.empty() && flashLife<=0 && subBooms.empty(); }
};

class FireworkSystem {
public:
   void start(int w,int h){
      m_active=true; m_w=w; m_h=h;
      m_fws.clear(); m_tick=0;
      for(int i=0;i<4;i++) spawn();
   }
   void stop(){ m_active=false; m_fws.clear(); }
   bool active() const { return m_active; }

   void tick(int w,int h){
      m_w=w; m_h=h; m_tick++;

      for(auto& fw : m_fws){
         if(!fw.exploded){
            fw.px=fw.x; fw.py=fw.y;
            fw.x+=fw.vx; fw.y+=fw.vy;
            fw.vy+=0.32f; fw.vx*=0.99f;
            bool apex   = fw.vy >= -0.4f;
            bool offscr = fw.x<5||fw.x>w-5||fw.y<10;
            if(apex||offscr) explode(fw);
         } else {
            if(fw.flashLife>0) fw.flashLife--;

            // Process crossette sub-booms
            for(auto& sb : fw.subBooms){
               sb.delay--;
               if(sb.delay==0) subExplode(fw,sb);
            }
            fw.subBooms.erase(
               std::remove_if(fw.subBooms.begin(),fw.subBooms.end(),
                  [](const Firework::SubBoom& s){return s.delay<=0;}),
               fw.subBooms.end());

            // Tick particles
            std::vector<Particle> next;
            next.reserve(fw.parts.size());
            for(auto& p : fw.parts){
               p.px=p.x; p.py=p.y;
               p.x+=p.vx; p.y+=p.vy;
               p.vy+=p.ay; p.vx*=p.drag; p.vy*=p.drag;
               p.life++;
               if(p.life < p.maxLife) next.push_back(p);
            }
            fw.parts=std::move(next);
         }
      }

      // Remove dead fireworks
      m_fws.erase(
         std::remove_if(m_fws.begin(),m_fws.end(),
            [](const Firework& f){return f.dead();}),
         m_fws.end());

      // Spawn new rockets to maintain density
      if(m_active){
         int live=0;
         for(auto& f:m_fws) if(!f.exploded) live++;
         if(live<3) spawn();
      }
   }

   const std::vector<Firework>& fireworks() const { return m_fws; }

private:
   std::vector<Firework> m_fws;
   bool  m_active=false;
   int   m_w=800, m_h=600, m_tick=0;
   std::mt19937 m_rng{std::random_device{}()};

   float rnd()  { return (float)(m_rng()%10000)/10000.f; }
   int   randi(){ return (int)(m_rng()%1000); }
   float rndSym(){ return rnd()*2.f-1.f; } // -1..1

   // Vivid random colour — at least one channel near max
   void randColour(BYTE& r,BYTE& g,BYTE& b){
      r=BYTE(80+randi()%175); g=BYTE(80+randi()%175); b=BYTE(80+randi()%175);
      int ch=randi()%3;
      if(ch==0) r=220+randi()%35;
      else if(ch==1) g=220+randi()%35;
      else b=220+randi()%35;
   }
   // Warm ember end-colour
   void emberColour(BYTE sr,BYTE sg,BYTE sb,BYTE& er,BYTE& eg,BYTE& eb){
      er=BYTE(sr/4); eg=BYTE(sg/6); eb=BYTE(sb/8);
   }

   Particle makeParticle(float x,float y,float vx,float vy,
                         BYTE r,BYTE g,BYTE b,
                         int minLife,int maxLife,
                         float size,float ay=0.14f,float drag=0.975f,
                         bool spark=false,bool strobe=false){
      Particle p;
      p.x=x; p.y=y; p.px=x; p.py=y;
      p.vx=vx; p.vy=vy; p.ay=ay; p.drag=drag;
      p.r=r; p.g=g; p.b=b;
      emberColour(r,g,b,p.er,p.eg,p.eb);
      p.life=0; p.maxLife=minLife+randi()%(maxLife-minLife+1);
      p.size=size; p.spark=spark; p.strobe=strobe; p.sub=false;
      return p;
   }

   void spawn(){
      Firework fw;
      fw.x  = 60.f + rnd()*(float)(m_w-120);
      fw.y  = (float)m_h;
      fw.px = fw.x; fw.py = fw.y;
      // Target: upper 55% of screen
      float targetY = m_h * (0.04f + rnd()*0.51f);
      float targetX = fw.x + rndSym()*350.f;
      float dist    = fw.y - targetY;
      if(dist<50) dist=50;
      fw.vy = -sqrtf(2.f*0.32f*dist);
      if(fw.vy < -24.f) fw.vy=-24.f;
      if(fw.vy > -6.f)  fw.vy=-6.f;
      float ticks = (fw.y-targetY)/(-fw.vy);
      fw.vx = (targetX-fw.x)/std::max(ticks,1.f);
      randColour(fw.cr,fw.cg,fw.cb);
      fw.burstType = static_cast<BurstType>(randi()%6);
      m_fws.push_back(fw);
   }

   void explode(Firework& fw){
      fw.exploded=true;
      fw.flashLife=7;
      fw.parts.reserve(160);

      BYTE r=fw.cr, g=fw.cg, b=fw.cb;

      switch(fw.burstType){

      case BURST_SPHERE:{
         // 80-120 particles, random sphere, long trails
         int n=80+randi()%40;
         for(int i=0;i<n;i++){
            float a=(float)(randi()%6283)/1000.f;
            float s=2.f+rnd()*5.5f;
            fw.parts.push_back(makeParticle(fw.x,fw.y,
               cosf(a)*s, sinf(a)*s-0.5f,
               r,g,b, 40,65, 1.8f+rnd()*1.2f, 0.13f,0.974f));
         }
         // Golden trailing sparks
         for(int i=0;i<14;i++){
            float a=(float)(randi()%6283)/1000.f;
            float s=1.f+rnd()*2.5f;
            auto p=makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
               255,200+randi()%55,50+randi()%80, 50,80,1.0f,0.08f,0.985f,true);
            fw.parts.push_back(p);
         }
         break;}

      case BURST_WILLOW:{
         // Upward burst that droops under strong gravity
         int n=70+randi()%35;
         for(int i=0;i<n;i++){
            // Fan mostly upward (-150°...-30°)
            float deg=-150.f+rnd()*120.f;
            float a=deg*3.14159f/180.f;
            float s=3.f+rnd()*5.f;
            auto p=makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
               r,g,b, 50,80, 1.4f, 0.28f, 0.970f); // strong gravity, more drag
            // Gold-green willow colours
            p.r=180+randi()%75; p.g=200+randi()%55; p.b=30+randi()%60;
            p.er=100; p.eg=80; p.eb=10;
            fw.parts.push_back(p);
         }
         break;}

      case BURST_CROSSETTE:{
         // 6-9 bright comets that re-explode into mini-bursts
         int n=6+randi()%4;
         for(int i=0;i<n;i++){
            float a=2.f*3.14159f*i/(float)n + rndSym()*0.15f;
            float s=4.5f+rnd()*2.f;
            float vx=cosf(a)*s, vy=sinf(a)*s;
            auto p=makeParticle(fw.x,fw.y,vx,vy, r,g,b, 18,24, 2.5f, 0.10f,0.980f);
            fw.parts.push_back(p);
            // Schedule sub-explosion at particle apex (~life/2)
            Firework::SubBoom sb;
            sb.x=fw.x+vx*11; sb.y=fw.y+vy*11+0.5f*0.10f*11*11;
            sb.vx=vx; sb.vy=vy;
            sb.delay=12+randi()%6;
            // Complementary colour
            sb.r=BYTE(255-r); sb.g=BYTE(255-g); sb.b=BYTE(255-b);
            fw.subBooms.push_back(sb);
         }
         break;}

      case BURST_STROBE:{
         // Even ring that blinks (visible every other tick)
         int n=48+randi()%24;
         for(int i=0;i<n;i++){
            float a=2.f*3.14159f*i/(float)n;
            float s=4.f+rnd()*1.5f;
            auto p=makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
               r,g,b, 35,50, 2.2f, 0.12f,0.972f,false,true); // strobe=true
            fw.parts.push_back(p);
         }
         // Dense inner bright ring
         for(int i=0;i<16;i++){
            float a=2.f*3.14159f*i/16.f;
            float s=1.8f;
            fw.parts.push_back(makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
               255,255,200, 20,30, 1.5f, 0.10f,0.980f));
         }
         break;}

      case BURST_DOUBLE:{
         // Slow outer sphere + fast bright inner ring (offset colours)
         int n=100+randi()%40;
         BYTE r2=BYTE(255-r), g2=BYTE(255-g), b2=BYTE(255-b);
         for(int i=0;i<n;i++){
            if(i<n/2){
               // Outer slow sphere
               float a=(float)(randi()%6283)/1000.f;
               float s=2.f+rnd()*4.f;
               fw.parts.push_back(makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
                  r,g,b, 45,70, 1.6f, 0.13f,0.974f));
            } else {
               // Inner fast ring (complementary colour)
               float a=2.f*3.14159f*(i-n/2)/(float)(n/2);
               float s=6.f+rnd()*1.f;
               fw.parts.push_back(makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
                  r2,g2,b2, 25,40, 2.0f, 0.11f,0.976f));
            }
         }
         break;}

      case BURST_COMET:{
         // 5-8 tight-angled comets with very long glowing trails
         int n=5+randi()%4;
         float baseAngle=-3.14159f/2.f + rndSym()*0.4f; // mostly upward
         for(int i=0;i<n;i++){
            float spread=((float)i/(float)(n-1)-0.5f)*1.2f;
            float a=baseAngle+spread;
            float s=5.f+rnd()*4.f;
            auto p=makeParticle(fw.x,fw.y,cosf(a)*s,sinf(a)*s,
               r,g,b, 60,90, 2.8f, 0.09f,0.982f); // slow gravity, low drag
            fw.parts.push_back(p);
            // Long glowing tail particles along initial path
            for(int j=1;j<8;j++){
               float frac=(float)j/8.f;
               auto tail=makeParticle(
                  fw.x-cosf(a)*s*j*0.8f, fw.y-sinf(a)*s*j*0.8f,
                  cosf(a)*(s*0.6f), sinf(a)*(s*0.6f),
                  r,g,b, 15,25, 1.5f-frac*0.8f, 0.07f,0.985f,true);
               fw.parts.push_back(tail);
            }
         }
         break;}
      }
   }

   // Crossette sub-explosion: mini burst at scheduled position
   void subExplode(Firework& fw, const Firework::SubBoom& sb){
      int n=16+randi()%10;
      for(int i=0;i<n;i++){
         float a=(float)(randi()%6283)/1000.f;
         float s=1.5f+rnd()*2.5f;
         auto p=makeParticle(sb.x,sb.y,cosf(a)*s,sinf(a)*s,
            sb.r,sb.g,sb.b, 20,35, 1.3f, 0.12f,0.972f);
         p.sub=true;
         fw.parts.push_back(p);
      }
   }
};
