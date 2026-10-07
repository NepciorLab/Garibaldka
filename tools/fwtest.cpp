// Renders the fireworks (src/fireworks2.h) to a PNG contact sheet, to tune their look without running the game.
//   zig c++ -std=c++17 -O2 tools/fwtest.cpp -o build/fwtest.exe && build/fwtest.exe build/ref/fw_out.png [scale]
#include "../src/fireworks2.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
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

int main(int argc,char** argv){
   std::string path=argc>1?argv[1]:"fw_out.png"; float rs=argc>2?(float)atof(argv[2]):0.6f;
   const int W=1180,H=860, bw=(int)(W*rs), bh=(int)(H*rs);
   Fireworks2 F; F.start(W,H); std::vector<uint32_t> img((size_t)bw*bh);
   const int cols=2, rows=2; std::vector<uint8_t> out((size_t)bw*cols*bh*rows*3);
   float shots[4]={1.6f,2.4f,3.3f,4.6f}; float t=0; int k=0; double renderMs=0; int frames=0;
   while(k<4){
      F.update(1.f/60.f,W,H); t+=1.f/60.f;
      if(t>=shots[k]){
         auto t0=std::chrono::steady_clock::now(); F.render(img.data(),bw,bh); auto t1=std::chrono::steady_clock::now();
         renderMs+=std::chrono::duration<double,std::milli>(t1-t0).count(); frames++;
         int ox=(k%cols)*bw, oy=(k/cols)*bh;
         for(int y=0;y<bh;y++) for(int x=0;x<bw;x++){
            uint32_t p=img[(size_t)y*bw+x]; float bg[3]={0.07f,0.15f,0.10f};
            size_t o=((size_t)(oy+y)*bw*cols+(ox+x))*3;
            float c[3]={((p>>16)&255)/255.f,((p>>8)&255)/255.f,(p&255)/255.f};
            for(int q=0;q<3;q++) out[o+q]=(uint8_t)(std::min(1.f,c[q]+bg[q])*255.f+0.5f);
         }
         printf("t=%.1f particles=%zu\n",t,F.particles()); k++;
      }
   }
   writePng(path,bw*cols,bh*rows,out);
   printf("render %.2f ms/frame\n",renderMs/frames);
}
