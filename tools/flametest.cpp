// Renders the fluid flame (src/fluidflame.h) to PNG sheets, to tune its look without running the game.
//   zig c++ -std=c++17 -O2 tools/flametest.cpp -o build/flametest.exe && build/flametest.exe build/ref
// Writes still.png (the standing flame) and move.png (the burner moving up, as when the turn changes).
#include "../src/fluidflame.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static uint32_t crcT[256];
static void crcInit(){ for(uint32_t n=0;n<256;n++){ uint32_t c=n; for(int k=0;k<8;k++) c=c&1?0xEDB88320u^(c>>1):c>>1; crcT[n]=c; } }
static uint32_t crc(const uint8_t* p,size_t n,uint32_t c=0xFFFFFFFFu){ for(size_t i=0;i<n;i++) c=crcT[(c^p[i])&255]^(c>>8); return c; }
static void be32(std::vector<uint8_t>& o,uint32_t v){ o.push_back(v>>24); o.push_back(v>>16); o.push_back(v>>8); o.push_back(v); }
static void chunk(FILE* f,const char* type,const std::vector<uint8_t>& d){
   std::vector<uint8_t> b; be32(b,(uint32_t)d.size()); fwrite(b.data(),1,4,f);
   std::vector<uint8_t> t(type,type+4); t.insert(t.end(),d.begin(),d.end()); fwrite(t.data(),1,t.size(),f);
   std::vector<uint8_t> c; be32(c,~crc(t.data(),t.size())); fwrite(c.data(),1,4,f);
}
static void writePng(const std::string& path,int w,int h,const std::vector<uint8_t>& rgb){      // stored (uncompressed) deflate
   crcInit(); FILE* f=fopen(path.c_str(),"wb"); if(!f) return;
   const uint8_t sig[8]={137,80,78,71,13,10,26,10}; fwrite(sig,1,8,f);
   std::vector<uint8_t> ih; be32(ih,w); be32(ih,h); ih.push_back(8); ih.push_back(2); ih.push_back(0); ih.push_back(0); ih.push_back(0); chunk(f,"IHDR",ih);
   std::vector<uint8_t> raw; for(int y=0;y<h;y++){ raw.push_back(0); raw.insert(raw.end(),rgb.begin()+(size_t)y*w*3,rgb.begin()+(size_t)(y+1)*w*3); }
   std::vector<uint8_t> z={0x78,0x01}; size_t pos=0;
   while(pos<raw.size()){ size_t n=std::min<size_t>(65535,raw.size()-pos); bool last=pos+n>=raw.size();
      z.push_back(last?1:0); z.push_back(n&255); z.push_back(n>>8); z.push_back(~n&255); z.push_back((~n>>8)&255); z.insert(z.end(),raw.begin()+pos,raw.begin()+pos+n); pos+=n; }
   uint32_t a=1,b=0; for(uint8_t c:raw){ a=(a+c)%65521; b=(b+a)%65521; } be32(z,(b<<16)|a);
   chunk(f,"IDAT",z); chunk(f,"IEND",{}); fclose(f);
}
static float ease(float t){ t=std::max(0.f,std::min(1.f,t)); return t*t*(3-2*t); }

struct Sim{
   FluidFlame F; std::vector<uint32_t> img; float t=0;
   float srcY(float tt) const { const float y0=620.f,y1=140.f; return y0+(y1-y0)*ease((tt-2.5f)/0.65f); }
   void init(){ F.init(140.f,700.f,3.f); img.assign((size_t)F.nx*F.ny,0); t=0; }
   void advance(float to){ while(t<to){ float dt=1.f/60.f; F.step(dt,70.f,srcY(t),56.f,1.f); t+=dt; } F.render(img.data()); }
};
// bilinear sample of the premultiplied BGRA flame image, drawn over the green table
static void blit(std::vector<uint8_t>& out,int W,int X0,int Y0,const Sim& S,float cropY,float cropH,float scale){
   const int w=(int)(140.f*scale), h=(int)(cropH*scale);
   for(int y=0;y<h;y++) for(int x=0;x<w;x++){
      float gx=(x/scale)/S.F.h-0.5f, gy=((y/scale)+cropY)/S.F.h-0.5f;
      int i=(int)std::floor(gx), j=(int)std::floor(gy); float tx=gx-i, ty=gy-j;
      auto P=[&](int ii,int jj,float* c){ if(ii<0||ii>=S.F.nx||jj<0||jj>=S.F.ny){ c[0]=c[1]=c[2]=c[3]=0; return; }
         uint32_t p=S.img[(size_t)jj*S.F.nx+ii]; c[3]=(p>>24)/255.f; c[0]=((p>>16)&255)/255.f; c[1]=((p>>8)&255)/255.f; c[2]=(p&255)/255.f; };
      float c00[4],c10[4],c01[4],c11[4],c[4]; P(i,j,c00); P(i+1,j,c10); P(i,j+1,c01); P(i+1,j+1,c11);
      for(int k=0;k<4;k++) c[k]=(c00[k]*(1-tx)+c10[k]*tx)*(1-ty)+(c01[k]*(1-tx)+c11[k]*tx)*ty;
      float bg[3]={0.17f,0.42f,0.24f}; size_t o=((size_t)(Y0+y)*W+(X0+x))*3;
      for(int k=0;k<3;k++){ float r=std::min(1.f,c[k]+bg[k]*(1.f-c[3])); out[o+k]=(uint8_t)(r*255.f+0.5f); }
   }
}
int main(int argc,char** argv){
   std::string dir=argc>1?argv[1]:".";
   Sim S; S.init();
   {  // the standing flame: two frames, 3x
      const int sc=3, cropY=380, cropH=300; const int W=140*sc*3, H=cropH*sc;
      std::vector<uint8_t> out((size_t)W*H*3,0); for(size_t i=0;i<out.size();i+=3){ out[i]=43; out[i+1]=107; out[i+2]=61; }
      float ts[3]={1.4f,1.9f,2.4f}; for(int k=0;k<3;k++){ S.advance(ts[k]); blit(out,W,k*140*sc,0,S,(float)cropY,(float)cropH,(float)sc); }
      writePng(dir+"/still.png",W,H,out);
   }
   {  // the burner moves up (turn changes): 8 frames
      Sim M; M.init(); const float sc=1.f; const int cols=8, W=(int)(140*sc)*cols, H=700;
      std::vector<uint8_t> out((size_t)W*H*3,0); for(size_t i=0;i<out.size();i+=3){ out[i]=43; out[i+1]=107; out[i+2]=61; }
      float ts[8]={2.45f,2.65f,2.8f,2.95f,3.15f,3.4f,3.8f,4.6f};
      for(int k=0;k<cols;k++){ M.advance(ts[k]); blit(out,W,k*(int)(140*sc),0,M,0.f,700.f,sc); }
      writePng(dir+"/move.png",W,H,out);
   }
   printf("energy %.1f\n",S.F.energy());
}
