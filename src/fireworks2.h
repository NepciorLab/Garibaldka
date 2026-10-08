#pragma once
// Fireworks in the style of "Fun With Fireworks" (Softwave): hundreds of thin streaks with a white-hot head and a
// coloured tail, air resistance and gravity, drifting glitter, and a strong bloom. Everything is drawn on the CPU
// into one additive image (premultiplied BGRA with alpha 0, so that it adds light to what is behind), which the game
// lays over the table. Pure C++ with no Windows dependency (tools/fwtest.cpp renders it to PNG for tuning).
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <random>
#include <cstdio>
#include <cstdlib>

class Fireworks2{
public:
   void start(int w,int h){ m_active=true; m_w=w; m_h=h; m_rockets.clear(); m_parts.clear(); m_flashes.clear(); m_events.clear(); m_time=0; m_acc=0; m_next=0.05f; }
   void stop(){ m_active=false; m_rockets.clear(); m_parts.clear(); m_flashes.clear(); m_events.clear(); }
   bool active() const { return m_active; }
   size_t particles() const { return m_parts.size(); }
   // What the ears have to know: a rocket was launched (kind 0: it flies `flight` seconds, then bursts; x = 0..1 across the
   // window) and a burst happened (kind 1; size = how many sparks). The game plays the whistles and the bangs at those moments.
   struct Event{ int kind; float flight; float x; float size; };
   std::vector<Event> takeEvents(){ std::vector<Event> e; e.swap(m_events); return e; }

   // advances the simulation by `dt` seconds (fixed internal steps of 1/60 s)
   void update(float dt,int w,int h){
      m_w=w; m_h=h; m_acc+=std::min(dt,0.1f);
      while(m_acc>=STEP){ m_acc-=STEP; step(); }
   }

   // Draws into out (premultiplied BGRA, alpha 0 = additive) of bw x bh pixels, which cover the whole window.
   void render(uint32_t* out,int bw,int bh){
      const size_t n=(size_t)bw*bh;
      if(m_acc3.size()!=n*3) m_acc3.assign(n*3,0.f); else std::fill(m_acc3.begin(),m_acc3.end(),0.f);
      m_bw=bw; m_bh=bh; const float sx=(float)bw/(float)std::max(1,m_w), sy=(float)bh/(float)std::max(1,m_h);
      const float k=scale();
      // flashes (the burst of light at the explosion)
      for(const Flash& f:m_flashes){
         float u=f.t/f.maxT, a=(1.f-u)*(1.f-u)*(1.f-u)*0.26f, R=(20.f+55.f*u)*k;
         int x0=(int)((f.x-R)*sx), x1=(int)((f.x+R)*sx), y0=(int)((f.y-R)*sy), y1=(int)((f.y+R)*sy);
         for(int y=std::max(0,y0);y<=std::min(bh-1,y1);y++) for(int x=std::max(0,x0);x<=std::min(bw-1,x1);x++){
            float dx=(x/sx-f.x)/R, dy=(y/sy-f.y)/R, d2=dx*dx+dy*dy; if(d2>=1.f) continue;
            float v=a*(1.f-d2)*(1.f-d2)*0.9f; float* p=&m_acc3[((size_t)y*bw+x)*3];
            p[0]+=v*f.r; p[1]+=v*f.g; p[2]+=v*f.b;
         }
      }
      // rockets: a white head and a thin warm trail
      for(const Rocket& r:m_rockets){
         for(int i=0;i+1<r.hn;i++){
            float a=1.f-(float)i/r.hn; a=a*a*0.9f;
            solidLine(r.hx[i]*sx,r.hy[i]*sy,r.hx[i+1]*sx,r.hy[i+1]*sy,1.0f*a,0.78f*a,0.45f*a);
         }
         splat(r.x*sx,r.y*sy,2.2f,1.f,0.95f,0.8f);
      }
      // particles
      for(const Part& p:m_parts){
         float u=p.life/p.maxLife;
         float fade= u<0.55f?1.f:std::pow(std::max(0.f,(1.f-u)/0.45f),1.4f);
         float tw=1.f; if(p.glitter) tw=0.35f+0.65f*(0.5f+0.5f*std::sin(m_time*(18.f+p.tw*20.f)+p.tw*40.f));
         else tw=0.88f+0.12f*std::sin(m_time*40.f+p.tw*30.f);
         float inten=fade*tw*p.bright;
         if(inten<0.01f) continue;
         // head colour: white-hot at first, then the particle's own colour
         float hot=std::max(0.f,1.f-u*3.2f);
         float hr=p.r+(1.f-p.r)*hot, hg=p.g+(1.f-p.g)*hot, hb=p.b+(1.f-p.b)*hot;
         if(p.hn<2){ splat(p.x*sx,p.y*sy,p.glitter?1.3f:1.8f,hr*inten,hg*inten,hb*inten); continue; }
         for(int i=0;i+1<p.hn;i++){
            float a=1.f-(float)i/(float)p.hn; a=a*a;                       // the tail fades quickly
            float cr=hr*a+p.r*(1.f-a)*0.55f, cg=hg*a+p.g*(1.f-a)*0.55f, cb=hb*a+p.b*(1.f-a)*0.55f;
            float w=inten*(0.15f+0.85*a);
            line(p.hx[i]*sx,p.hy[i]*sy,p.hx[i+1]*sx,p.hy[i+1]*sy,cr*w,cg*w,cb*w);
         }
         splat(p.x*sx,p.y*sy,1.6f,hr*inten,hg*inten,hb*inten);              // the bright head
      }
      bloom(bw,bh);
      // tone mapping: soft shoulder, so overlapping streaks go white instead of clipping; dithered, so the soft glow has no bands
      if(m_lut.empty()){ m_lut.resize(2049); for(int i=0;i<=2048;i++) m_lut[i]=(1.f-std::exp(-(i/256.f)*1.25f))*255.f; }
      auto tm=[&](float v,float d)->uint32_t{ int i=(int)(v*256.f); float x=m_lut[i<0?0:i>2048?2048:i]+d; return (uint32_t)(x<0.f?0.f:x>255.f?255.f:x); };
      uint32_t seed=(uint32_t)(m_time*977.f)*2654435761u;
      for(size_t i=0;i<n;i++){
         const float* a=&m_acc3[i*3];
         seed=seed*1664525u+1013904223u; float d=((seed>>24)&255)/255.f;       // 0..1 noise: floor(x+d) rounds stochastically
         uint32_t R=tm(a[0],d), G=tm(a[1],d), B=tm(a[2],d);
         out[i]=(R<<16)|(G<<8)|B;                                           // alpha 0: added to the table
      }
   }

private:
   static constexpr float STEP=1.f/60.f;
   struct Part{ float x,y,vx,vy,life,maxLife,r,g,b,drag,grav,bright,tw; bool glitter; float hx[14],hy[14]; int hn,hmax; };
   struct Rocket{ float x,y,vx,vy,ay,ty; float hx[20],hy[20]; int hn; float t,T; };
   struct Flash{ float x,y,t,maxT,r,g,b; };
   std::vector<Part> m_parts; std::vector<Rocket> m_rockets; std::vector<Flash> m_flashes; std::vector<Event> m_events;
   bool m_active=false; int m_w=800,m_h=600,m_bw=0,m_bh=0; float m_time=0,m_acc=0,m_next=0;
   std::vector<float> m_acc3, m_b1, m_b2; std::vector<float> m_lut; int m_pal=0, m_burstNo=0; std::vector<float> m_hues;
   std::mt19937 m_rng{std::random_device{}()};
   float rnd(){ return (float)(m_rng()%100000)/100000.f; }
   float scale() const { return std::min((float)m_w,(float)m_h*1.25f)/1000.f; }       // design: ~1000 px wide

   // ---- additive drawing
   void plot(float x,float y,float r,float g,float b){
      int ix=(int)std::floor(x), iy=(int)std::floor(y); float fx=x-ix, fy=y-iy;
      for(int j=0;j<2;j++) for(int i=0;i<2;i++){
         int xx=ix+i, yy=iy+j; if(xx<0||yy<0||xx>=m_bw||yy>=m_bh) continue;
         float w=(i?fx:1.f-fx)*(j?fy:1.f-fy); float* p=&m_acc3[((size_t)yy*m_bw+xx)*3];
         p[0]+=r*w; p[1]+=g*w; p[2]+=b*w;
      }
   }
   void line(float x0,float y0,float x1,float y1,float r,float g,float b){
      float dx=x1-x0, dy=y1-y0; float len=std::sqrt(dx*dx+dy*dy); int n=std::max(1,(int)std::ceil(len/2.6f));    // a bead every ~2.6 px
      for(int i=0;i<n;i++){ float t=(float)i/n; splat(x0+dx*t,y0+dy*t,1.35f,r*2.1f,g*2.1f,b*2.1f); }
   }
   void solidLine(float x0,float y0,float x1,float y1,float r,float g,float b){                             // the rocket's thin continuous trail
      float dx=x1-x0, dy=y1-y0; int n=std::max(1,(int)std::ceil(std::max(std::fabs(dx),std::fabs(dy))));
      for(int i=0;i<=n;i++){ float t=(float)i/n; plot(x0+dx*t,y0+dy*t,r,g,b); }
   }
   void splat(float x,float y,float rad,float r,float g,float b){                       // a small soft dot
      int R=(int)std::ceil(rad); for(int j=-R;j<=R;j++) for(int i=-R;i<=R;i++){
         float d=std::sqrt((float)(i*i+j*j))/rad; if(d>=1.f) continue; float w=(1.f-d)*(1.f-d);
         int xx=(int)std::floor(x)+i, yy=(int)std::floor(y)+j; if(xx<0||yy<0||xx>=m_bw||yy>=m_bh) continue;
         float* p=&m_acc3[((size_t)yy*m_bw+xx)*3]; p[0]+=r*w; p[1]+=g*w; p[2]+=b*w;
      }
   }
   // glow: bright parts, downsampled 4x, blurred wide, added back
   void bloom(int bw,int bh){
      const int sw=std::max(2,bw/4), sh=std::max(2,bh/4);
      if(m_b1.size()!=(size_t)sw*sh*3){ m_b1.assign((size_t)sw*sh*3,0.f); m_b2.assign((size_t)sw*sh*3,0.f); }
      for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){
         float s0=0,s1=0,s2=0; int c=0;
         for(int j=0;j<4;j++){ int yy=y*4+j; if(yy>=bh) break; const float* row=&m_acc3[((size_t)yy*bw)*3];
            for(int i=0;i<4;i++){ int xx=x*4+i; if(xx>=bw) break; s0+=row[xx*3]; s1+=row[xx*3+1]; s2+=row[xx*3+2]; c++; } }
         float inv=c?1.f/c:0.f; float* q=&m_b1[((size_t)y*sw+x)*3];
         q[0]=std::max(0.f,s0*inv-0.08f); q[1]=std::max(0.f,s1*inv-0.08f); q[2]=std::max(0.f,s2*inv-0.08f);
      }
      const int R=4; const float norm=1.f/(2*R+1);
      for(int pass=0;pass<2;pass++){                                                    // two box blurs in each direction (a wide, smooth falloff)
         for(int y=0;y<sh;y++) for(int k=0;k<3;k++){ float run=0; float* d=&m_b2[((size_t)y*sw)*3+k]; const float* sr=&m_b1[((size_t)y*sw)*3+k];
            for(int x=-R;x<=R;x++) run+=sr[std::max(0,std::min(sw-1,x))*3];
            for(int x=0;x<sw;x++){ d[x*3]=run*norm; run+=sr[std::min(sw-1,x+R+1)*3]-sr[std::max(0,x-R)*3]; } }
         for(int x=0;x<sw;x++) for(int k=0;k<3;k++){ float run=0; for(int y=-R;y<=R;y++) run+=m_b2[((size_t)std::max(0,std::min(sh-1,y))*sw+x)*3+k];
            for(int y=0;y<sh;y++){ m_b1[((size_t)y*sw+x)*3+k]=run*norm; run+=m_b2[((size_t)std::min(sh-1,y+R+1)*sw+x)*3+k]-m_b2[((size_t)std::max(0,y-R)*sw+x)*3+k]; } }
      }
      std::vector<int> xi(bw); std::vector<float> xf(bw);
      for(int x=0;x<bw;x++){ float gx=(x+0.5f)/4.f-0.5f; int ix=(int)std::floor(gx); xf[x]=gx-ix; xi[x]=std::max(0,std::min(sw-2,ix)); if(ix<0) xf[x]=0.f; else if(ix>=sw-1) xf[x]=1.f; }
      for(int y=0;y<bh;y++){
         float gy=(y+0.5f)/4.f-0.5f; int iy=(int)std::floor(gy); float fy=gy-iy; if(iy<0){ iy=0; fy=0.f; } else if(iy>=sh-1){ iy=sh-2; fy=1.f; }
         const float* r0=&m_b1[((size_t)iy*sw)*3]; const float* r1=&m_b1[((size_t)(iy+1)*sw)*3]; float* o=&m_acc3[((size_t)y*bw)*3];
         for(int x=0;x<bw;x++){
            const int a0=xi[x]*3; const float fx=xf[x];
            for(int k=0;k<3;k++){ float v=(r0[a0+k]*(1-fx)+r0[a0+3+k]*fx)*(1-fy)+(r1[a0+k]*(1-fx)+r1[a0+3+k]*fx)*fy; o[x*3+k]+=v*2.0f; }
         }
      }
   }

   // ---- simulation
   void hsv(float h,float s,float v,float& r,float& g,float& b){
      h=h-std::floor(h); float c=v*s, x=c*(1.f-std::fabs(std::fmod(h*6.f,2.f)-1.f)), m=v-c; int i=(int)(h*6.f)%6;
      float R[6]={c,x,0,0,x,c}, G[6]={x,c,c,x,0,0}, B[6]={0,0,x,c,c,x}; r=R[i]+m; g=G[i]+m; b=B[i]+m;
   }
   void spawnRocket(){
      Rocket r; r.x=m_w*(0.12f+0.76f*rnd()); r.y=(float)m_h; r.ty=m_h*(0.10f+0.38f*rnd());
      r.T=0.85f+0.5f*rnd(); r.t=0.f; float dist=r.y-r.ty; r.vy=-2.f*dist/r.T; r.ay=-r.vy/r.T;
      r.vx=(rnd()-0.5f)*m_w*0.10f/r.T; r.hn=0; m_rockets.push_back(r);
      m_events.push_back({0,r.T,r.x/(float)std::max(1,m_w),0.f});
   }
   void pushHist(Part& p){
      if(p.hn<p.hmax) p.hn++;
      for(int i=p.hn-1;i>0;i--){ p.hx[i]=p.hx[i-1]; p.hy[i]=p.hy[i-1]; }
      p.hx[0]=p.x; p.hy[0]=p.y;
   }
   void burst(float x,float y){
      const float k=scale(); int type=(int)(rnd()*4.f)%4; if(m_burstNo++==0) type=0;
      if(m_hues.empty()){ m_hues={0.90f,0.50f,0.13f,0.76f,0.33f,0.02f,0.60f,0.95f,0.42f}; std::shuffle(m_hues.begin(),m_hues.end(),m_rng); }
      float hue=m_hues.back(); m_hues.pop_back(); float var=0.03f+0.12f*rnd();
      float fr,fg,fb; hsv(hue,0.55f+0.4f*rnd(),1.f,fr,fg,fb); 
      m_flashes.push_back({x,y,0.f,0.45f,fr*0.6f+0.4f,fg*0.6f+0.4f,fb*0.6f+0.4f});
      int N=(type==1)?420:(type==3?520:600+(int)(rnd()*300));
      for(int i=0;i<N;i++){
         Part p; p.x=x; p.y=y; p.glitter=false; p.tw=rnd(); p.hn=0; p.hmax=11;
         float a=rnd()*6.2831853f, v;
         p.maxLife=1.6f+rnd()*1.7f; p.drag=0.955f+0.012f*rnd(); p.grav=(60.f+40.f*rnd())*k; p.bright=0.55f+0.45f*rnd();
         float h=hue+(rnd()-0.5f)*var*2.f; float sat=0.55f+0.45f*rnd(); if(rnd()<0.10f) sat*=0.25f;                // a few nearly white
         hsv(h,sat,1.f,p.r,p.g,p.b);
         if(type==0){                                                                                              // classic sphere
            float q=std::pow(rnd(),0.62f); v=(25.f+640.f*q)*k;                                                       // q: 0 = slow (the core), 1 = fast (the rim)
            p.vx=std::cos(a)*v; p.vy=std::sin(a)*v;
            hsv(hue-(1.f-q)*0.30f+(rnd()-0.5f)*0.05f,0.35f+0.65f*q,1.f,p.r,p.g,p.b);                                   // slow ones turn yellow-white
         } else if(type==1){                                                                                       // willow: gold, slow, droops, long life
            v=(20.f+430.f*std::pow(rnd(),0.7f))*k; p.vx=std::cos(a)*v; p.vy=std::sin(a)*v-40.f*k;
            hsv(0.10f+0.04f*rnd(),0.55f+0.3f*rnd(),1.f,p.r,p.g,p.b); p.maxLife=2.6f+rnd()*1.6f; p.drag=0.975f+0.008f*rnd(); p.grav=(170.f+60.f*rnd())*k; p.hmax=13;
         } else if(type==2){                                                                                       // ring (a tilted circle) plus a core
            bool ring=i<N*0.7f; v=(ring?560.f:(100.f+260.f*rnd()))*k*(ring?(0.96f+0.08f*rnd()):1.f);
            p.vx=std::cos(a)*v; p.vy=std::sin(a)*v*0.55f; if(!ring) p.vy=std::sin(a)*v;
            if(ring){ hsv(hue,0.8f,1.f,p.r,p.g,p.b); } else { hsv(hue+0.5f,0.5f,1.f,p.r,p.g,p.b); }
         } else {                                                                                                  // two shells, inner one in the opposite colour
            bool inner=i<N*0.45f; v=(inner?(120.f+210.f*rnd()):(280.f+380.f*rnd()))*k; p.vx=std::cos(a)*v; p.vy=std::sin(a)*v;
            if(inner) hsv(hue+0.5f,0.7f,1.f,p.r,p.g,p.b);
         }
         m_parts.push_back(p);
      }
      // glitter: dim sparks that drift down and twinkle for a long time
      m_events.push_back({1,0.f,x/(float)std::max(1,m_w),(float)N});
      int G=120+(int)(rnd()*120);
      for(int i=0;i<G;i++){
         Part p; p.x=x; p.y=y; p.glitter=true; p.tw=rnd(); p.hn=0; p.hmax=1;
         float a=rnd()*6.2831853f, v=(60.f+300.f*rnd())*k; p.vx=std::cos(a)*v; p.vy=std::sin(a)*v; p.maxLife=2.6f+rnd()*2.4f;
         p.drag=0.93f+0.02f*rnd(); p.grav=(25.f+25.f*rnd())*k; p.bright=0.5f+0.5f*rnd(); hsv(hue+(rnd()-0.5f)*0.2f,0.25f+0.4f*rnd(),1.f,p.r,p.g,p.b);
         m_parts.push_back(p);
      }
   }
   void step(){
      m_time+=STEP;
      if(m_active){ m_next-=STEP; if(m_next<=0.f && m_rockets.size()<3){ spawnRocket(); m_next=0.45f+0.9f*rnd(); } }
      for(Rocket& r:m_rockets){
         r.t+=STEP; r.vy+=r.ay*STEP; r.x+=r.vx*STEP; r.y+=r.vy*STEP;
         if(r.hn<19) r.hn++; for(int i=r.hn-1;i>0;i--){ r.hx[i]=r.hx[i-1]; r.hy[i]=r.hy[i-1]; } r.hx[0]=r.x; r.hy[0]=r.y;
      }
      for(size_t i=0;i<m_rockets.size();){
         Rocket& r=m_rockets[i];
         if(r.t>=r.T||r.y<=r.ty){ burst(r.x,r.y); m_rockets.erase(m_rockets.begin()+i); } else i++;
      }
      for(Part& p:m_parts){
         if(!p.glitter) pushHist(p);
         p.vx*=p.drag; p.vy=(p.vy+p.grav*STEP)*p.drag; p.x+=p.vx*STEP; p.y+=p.vy*STEP; p.life+=STEP;
      }
      m_parts.erase(std::remove_if(m_parts.begin(),m_parts.end(),[](const Part& p){ return p.life>=p.maxLife; }),m_parts.end());
      for(Flash& f:m_flashes) f.t+=STEP;
      m_flashes.erase(std::remove_if(m_flashes.begin(),m_flashes.end(),[](const Flash& f){ return f.t>=f.maxT; }),m_flashes.end());
   }
};
