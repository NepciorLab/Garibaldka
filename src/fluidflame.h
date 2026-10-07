#pragma once
// A small 2D fluid simulation (Jos Stam's "stable fluids" with buoyancy and vorticity confinement) that draws
// the flame. The fuel is advected in the world, not glued to the burner: when the burner moves, the burning gas stays
// behind and is stretched into a curved trail that then rises - the look of the simulated fire of
// "Animating Fire with Sound" (Chadwick & James, Cornell), which was the reference for the colours and the behaviour.
// Pure CPU code with no Windows dependency (tools/flametest.cpp renders it to PNG for tuning).
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

struct FluidFlame{
   int nx=0, ny=0;                 // grid size in cells
   float h=3.f;                    // cell size in design pixels (the game scales design pixels to the screen)
   std::vector<float> u,v,f,s,q,dv,a1,a2,a3,a4,w;
   float time=0.f;
   int j0=0, j1=-1;                // rows that are simulated this step (the rest of the tall domain is empty)
   // tuning (design pixels, seconds)
   float buoy=1500.f, drag=1.3f, vort=10.f, burn=3.1f, smokeRate=0.55f, smokeDecay=0.8f, srcSpeed=300.f, spread=0.16f;

   void init(float width,float height,float cell=3.f){
      h=cell; nx=std::max(8,(int)(width/h)); ny=std::max(8,(int)(height/h));
      size_t n=(size_t)nx*ny;
      for(auto* a:{&u,&v,&f,&s,&q,&dv,&a1,&a2,&a3,&a4,&w}) a->assign(n,0.f);
      time=0.f;
   }
   bool ready() const { return nx>0; }
   void clear(){ for(auto* a:{&u,&v,&f,&s,&q}) std::fill(a->begin(),a->end(),0.f); j0=0; j1=-1; hasLast=false; }
   float energy() const { float e=0; for(float x:f) e+=x; for(float x:s) e+=x; return e; }

   // ---- value noise 0..1 (cheap turbulence for the ragged tips)
   static float hash(int x,int y,int z){ uint32_t n=(uint32_t)(x*374761393+y*668265263+z*1274126177); n=(n^(n>>13))*1274126177u; n^=n>>16; return (n&0xFFFFFF)/16777215.f; }
   static float vnoise(float x,float y,float z){
      int xi=(int)std::floor(x), yi=(int)std::floor(y), zi=(int)std::floor(z); float fx=x-xi, fy=y-yi, fz=z-zi;
      fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy); fz=fz*fz*(3-2*fz);
      auto L=[&](float a,float b,float t){ return a+(b-a)*t; };
      float c00=L(hash(xi,yi,zi),hash(xi+1,yi,zi),fx), c10=L(hash(xi,yi+1,zi),hash(xi+1,yi+1,zi),fx);
      float c01=L(hash(xi,yi,zi+1),hash(xi+1,yi,zi+1),fx), c11=L(hash(xi,yi+1,zi+1),hash(xi+1,yi+1,zi+1),fx);
      return L(L(c00,c10,fy),L(c01,c11,fy),fz);
   }

   // ---- sampling (x,y in design px from the domain's top-left). outsideZero: scalars vanish outside, velocities are clamped
   float sample(const std::vector<float>& a,float x,float y,bool outsideZero) const {
      float gx=x/h-0.5f, gy=y/h-0.5f;
      int i=(int)std::floor(gx), j=(int)std::floor(gy); float tx=gx-i, ty=gy-j;
      auto at=[&](int ii,int jj)->float{
         if(outsideZero){ if(ii<0||ii>=nx||jj<0) return 0.f; if(jj>=ny) jj=ny-1; }
         else { ii=std::max(0,std::min(nx-1,ii)); jj=std::max(0,std::min(ny-1,jj)); }
         return a[(size_t)jj*nx+ii];
      };
      float a0=at(i,j)*(1-tx)+at(i+1,j)*tx, a1_=at(i,j+1)*(1-tx)+at(i+1,j+1)*tx;
      return a0*(1-ty)+a1_*ty;
   }
   // The back-traced positions are computed once for the velocity and once for the fuel/smoke (they share them).
   std::vector<float> tx,ty;
   void trace(float dt){
      if(tx.size()!=u.size()){ tx.assign(u.size(),0.f); ty.assign(u.size(),0.f); }
      for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++){
         float x=(i+0.5f)*h, y=(j+0.5f)*h;
         float ux=sample(u,x,y,false), vy=sample(v,x,y,false);
         float mx=x-0.5f*dt*ux, my=y-0.5f*dt*vy;                       // midpoint (RK2) back-trace
         size_t id=(size_t)j*nx+i;
         tx[id]=x-dt*sample(u,mx,my,false); ty[id]=y-dt*sample(v,mx,my,false);
      }
   }
   void advect(std::vector<float>& dst,const std::vector<float>& src,bool outsideZero) const {
      for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++){ size_t id=(size_t)j*nx+i; dst[id]=sample(src,tx[id],ty[id],outsideZero); }
   }
   void takeBand(std::vector<float>& to,const std::vector<float>& from){ for(int j=j0;j<=j1;j++) std::copy(from.begin()+(size_t)j*nx,from.begin()+(size_t)(j+1)*nx,to.begin()+(size_t)j*nx); }
   // pressure projection: open at the left, right and top edges (air flows in and out), a wall at the bottom
   void project(){
      const float inv2h=1.f/(2.f*h);
      auto U=[&](int i,int j){ i=std::max(0,std::min(nx-1,i)); j=std::max(0,std::min(ny-1,j)); return u[(size_t)j*nx+i]; };
      auto V=[&](int i,int j){ i=std::max(0,std::min(nx-1,i)); if(j>=ny) return 0.f; j=std::max(0,j); return v[(size_t)j*nx+i]; };
      for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++) dv[(size_t)j*nx+i]=(U(i+1,j)-U(i-1,j)+V(i,j+1)-V(i,j-1))*inv2h;
      const float hh=h*h;
      for(int it=0;it<18;it++){
         for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++){
            float qe= i+1<nx? q[(size_t)j*nx+i+1] : 0.f, qw= i>0? q[(size_t)j*nx+i-1] : 0.f;
            float qn= j>0? q[(size_t)(j-1)*nx+i] : 0.f;
            float qs= j+1<ny? q[(size_t)(j+1)*nx+i] : q[(size_t)j*nx+i];          // wall: mirror
            q[(size_t)j*nx+i]=(qe+qw+qn+qs-hh*dv[(size_t)j*nx+i])*0.25f;
         }
      }
      auto Q=[&](int i,int j)->float{ if(i<0||i>=nx||j<0) return 0.f; if(j>=ny) j=ny-1; return q[(size_t)j*nx+i]; };
      for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++){
         u[(size_t)j*nx+i]-=(Q(i+1,j)-Q(i-1,j))*inv2h;
         v[(size_t)j*nx+i]-=(Q(i,j+1)-Q(i,j-1))*inv2h;
      }
      for(int i=0;i<nx;i++) v[(size_t)(ny-1)*nx+i]=std::min(0.f,v[(size_t)(ny-1)*nx+i]);   // nothing flows into the floor
   }

   float lastSx=0.f, lastSy=0.f; bool hasLast=false;
   // The burner at (sx,sy): fuel and an upward push. A fast burner is stamped along its path (every cell or so),
   // otherwise it would leave separate rings behind.
   void stamp(float dt,float sx,float sy,float sw,float strength){
      float k=1.f-std::exp(-dt*28.f);
      for(int j=0;j<ny;j++){
         float y=(j+0.5f)*h; float dy=(y-sy)/(h*1.6f); if(dy<-1.f||dy>1.f) continue;
         float wy=1.f-dy*dy;
         for(int i=0;i<nx;i++){
            float x=(i+0.5f)*h; float dx=(x-sx)/(sw*0.5f); if(dx<-1.f||dx>1.f) continue;
            float wgt=(1.f-dx*dx)*wy*strength;                     // soft edges
            size_t id=(size_t)j*nx+i;
            f[id]+=(1.f-f[id])*std::min(1.f,k*wgt*1.6f);
            v[id]+=(-srcSpeed*(0.85f+0.3f*vnoise(time*5.f,i*0.4f,3.f))-v[id])*std::min(1.f,k*wgt);
            u[id]+=(vnoise(time*9.f,j*0.5f,7.f)-0.5f)*140.f*dt*wgt;
         }
      }
   }
   // One simulation step. The burner: centre x, y (design px from the domain's top-left), width, strength 0..1.
   void step(float dt,float sx,float sy,float sw,float strength){
      if(!ready()) return;
      time+=dt;
      if(strength>0.001f){
         if(!hasLast){ lastSx=sx; lastSy=sy; hasLast=true; }
         float dist=std::hypot(sx-lastSx,sy-lastSy);
         int n=std::max(1,std::min(80,(int)std::ceil(dist/(h*0.4f))));
         for(int k=1;k<=n;k++){ float t=(float)k/n; stamp(dt/n*1.f,lastSx+(sx-lastSx)*t,lastSy+(sy-lastSy)*t,sw,strength); }
         lastSx=sx; lastSy=sy;
      } else hasLast=false;
      // --- which rows are alive (fuel, smoke or moving air), plus a margin; the rest of the domain is empty
      { int lo=ny, hi=-1;
        for(int j=0;j<ny;j++){ bool act=false; const size_t b=(size_t)j*nx;
           for(int i=0;i<nx&&!act;i++) act=f[b+i]>0.002f||s[b+i]>0.004f||std::fabs(v[b+i])>3.f;
           if(act){ if(j<lo) lo=j; hi=j; } }
        if(hi<0){ j0=0; j1=-1; return; }
        j0=std::max(0,lo-6); j1=std::min(ny-1,hi+6); }
      // --- buoyancy, drag, vorticity confinement
      const float dragK=std::exp(-drag*dt);
      for(size_t id=(size_t)j0*nx;id<(size_t)(j1+1)*nx;id++){
         v[id]-=(buoy*f[id]+110.f*s[id])*dt;
         u[id]*=dragK; v[id]*=dragK;
      }
      for(int j=std::max(1,j0);j<=std::min(ny-2,j1);j++) for(int i=1;i<nx-1;i++){
         size_t id=(size_t)j*nx+i;
         w[id]=((v[id+1]-v[id-1])-(u[id+nx]-u[id-nx]))/(2.f*h);
      }
      for(int j=std::max(2,j0);j<=std::min(ny-3,j1);j++) for(int i=2;i<nx-2;i++){
         size_t id=(size_t)j*nx+i;
         float gx=(std::fabs(w[id+1])-std::fabs(w[id-1]))/(2.f*h), gy=(std::fabs(w[id+nx])-std::fabs(w[id-nx]))/(2.f*h);
         float len=std::sqrt(gx*gx+gy*gy)+1e-6f; gx/=len; gy/=len;
         float gate=std::min(1.f,(f[id]+s[id]*0.5f)*3.f);                // only where there is something burning
         u[id]+= vort*h*gy*w[id]*dt*gate;
         v[id]+=-vort*h*gx*w[id]*dt*gate;
      }
      // --- velocity: advect, project
      trace(dt); advect(a1,u,false); advect(a2,v,false); takeBand(u,a1); takeBand(v,a2);
      project();
      // --- fuel and smoke: advect, burn
      trace(dt); advect(a3,f,true); advect(a4,s,true); takeBand(f,a3); takeBand(s,a4);
      { for(int j=j0;j<=j1;j++) for(int i=1;i<nx-1;i++){ size_t id=(size_t)j*nx+i; a3[id]=f[id]+spread*0.5f*(f[id-1]+f[id+1]-2.f*f[id]); }
        for(int j=j0;j<=j1;j++){ a3[(size_t)j*nx]=f[(size_t)j*nx]; a3[(size_t)j*nx+nx-1]=f[(size_t)j*nx+nx-1]; }
        takeBand(f,a3); }
      const float sd=std::exp(-smokeDecay*dt);
      for(int j=j0;j<=j1;j++) for(int i=0;i<nx;i++){
         size_t id=(size_t)j*nx+i;
         float rag=0.55f+1.1f*vnoise(i*0.33f,j*0.21f-time*2.6f,1.f);       // ragged burning: the tips fray
         float b=f[id]*burn*rag*dt; if(b>f[id]) b=f[id];
         f[id]-=b; s[id]=s[id]*sd+b*smokeRate;
         if(f[id]<0.003f) f[id]=0.f;
         if(s[id]>1.f) s[id]=1.f;
         float y=(j+0.5f)*h; if(y<h*6.f){ float fade=y/(h*6.f); s[id]*=fade+(1.f-fade)*std::exp(-2.f*dt); }   // the smoke thins out at the top edge
      }
   }

   // Draws into a premultiplied BGRA buffer of nx*ny pixels.
   // Look (measured on the reference video): a bright peach edge (253,192,124), a translucent warm inside
   // (about 150,105,70 over black), the thin upper flame glowing yellow-peach, grey translucent smoke above.
   // The colours are emitted light: a premultiplied colour larger than the alpha adds to what is behind (a glow).
   void render(uint32_t* out) const {
      auto F=[&](int i,int j){ i=std::max(0,std::min(nx-1,i)); j=std::max(0,std::min(ny-1,j)); return f[(size_t)j*nx+i]; };
      for(int j=0;j<ny;j++) for(int i=0;i<nx;i++){
         size_t id=(size_t)j*nx+i; float fv=f[id], sv=s[id];
         float gx=(F(i+1,j)-F(i-1,j))*0.5f, gy=(F(i,j+1)-F(i,j-1))*0.5f; float g=std::sqrt(gx*gx+gy*gy);
         float cover=std::min(1.f,fv*3.2f); cover=cover*cover*(3.f-2.f*cover);                   // where there is flame at all
         float rim=std::min(1.f,std::max(0.f,(g-0.07f)*5.5f))*std::min(1.f,fv*6.f);                    // its bright edge (thin)
         if(gy<0.f) rim*=0.25f;                                                                   // not along the underside of the burner
         float tip=std::min(1.f,std::max(0.f,(0.5f-fv)*2.2f))*cover;                              // thin parts burn brighter
         float tex=0.86f+0.28f*vnoise(i*0.55f,j*0.55f-time*5.f,5.f);                              // fine flicker inside
         // emitted colour: inside warm orange-brown, edge peach, tips light peach
         float er=0.74f*tex, eg=0.30f*tex, eb=0.07f*tex;
         float ea=0.62f;
         float mix=std::min(1.f,rim+tip*0.7f);
         er+=(1.00f-er)*mix; eg+=(0.74f-eg)*mix; eb+=(0.40f-eb)*mix; ea+=(0.92f-ea)*mix;
         float r=er*cover, gr=eg*cover, b=eb*cover, a=ea*cover;
         // smoke: grey, translucent, in front
         float sa=std::min(0.30f,sv*0.50f); const float sg=0.50f;
         r=r*(1.f-sa)+sg*sa; gr=gr*(1.f-sa)+sg*sa; b=b*(1.f-sa)+sg*sa; a=a+sa*(1.f-a);
         r=std::min(1.f,r); gr=std::min(1.f,gr); b=std::min(1.f,b); a=std::min(1.f,a);
         uint32_t A=(uint32_t)(a*255.f+0.5f), R=(uint32_t)(r*255.f+0.5f), Gc=(uint32_t)(gr*255.f+0.5f), B=(uint32_t)(b*255.f+0.5f);
         out[id]=(A<<24)|(R<<16)|(Gc<<8)|B;
      }
   }
};
