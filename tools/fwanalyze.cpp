// Looks for the rocket whistles (a narrow tone that rises) and the bangs (a sudden loud broadband onset with bass) in a
// recording of fireworks, and prints their times.   zig c++ -std=c++17 -O2 tools/fwanalyze.cpp -o build/fwanalyze.exe
//   build/fwanalyze.exe in.wav
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <vector>
#include <complex>
#include <algorithm>
typedef std::complex<float> cf;
static void fft(std::vector<cf>& a){
   size_t n=a.size();
   for(size_t i=1,j=0;i<n;i++){ size_t bit=n>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit; if(i<j) std::swap(a[i],a[j]); }
   for(size_t len=2;len<=n;len<<=1){
      float ang=-6.2831853f/len; cf wl(std::cos(ang),std::sin(ang));
      for(size_t i=0;i<n;i+=len){ cf w(1); for(size_t j=0;j<len/2;j++){ cf u=a[i+j], v=a[i+j+len/2]*w; a[i+j]=u+v; a[i+j+len/2]=u-v; w*=wl; } }
   }
}
int main(int argc,char** argv){
   FILE* f=fopen(argv[1],"rb"); if(!f) return 1;
   uint8_t hdr[44]; fread(hdr,1,44,f); int ch=hdr[22]; int sr=*(int*)(hdr+24);
   std::vector<int16_t> raw; { int16_t buf[4096]; size_t n; while((n=fread(buf,2,4096,f))>0) raw.insert(raw.end(),buf,buf+n); }
   size_t N=raw.size()/ch; std::vector<float> m(N); for(size_t i=0;i<N;i++){ float s=0; for(int c=0;c<ch;c++) s+=raw[i*ch+c]; m[i]=s/ch/32768.f; }
   const int W=2048, H=512; const float binHz=(float)sr/W;
   struct Fr{ float t,peakHz,tonal,total,low,high; };
   std::vector<Fr> fr;
   std::vector<float> win(W); for(int i=0;i<W;i++) win[i]=0.5f-0.5f*std::cos(6.2831853f*i/W);
   for(size_t k=0;k+W<N;k+=H){
      std::vector<cf> a(W); for(int i=0;i<W;i++) a[i]=cf(m[k+i]*win[i],0);
      fft(a);
      float peak=0, tot=0, low=0, high=0; int pb=0; float sumBand=0; int nb=0;
      for(int b=1;b<W/2;b++){
         float mag=std::abs(a[b]); float hz=b*binHz; tot+=mag*mag;
         if(hz<250) low+=mag*mag; if(hz>2000) high+=mag*mag;
         if(hz>=700&&hz<=7000){ sumBand+=mag; nb++; if(mag>peak){ peak=mag; pb=b; } }
      }
      float mean=nb?sumBand/nb:1e-9f;
      fr.push_back({(float)k/sr,pb*binHz,peak/(mean+1e-9f),tot,low,high});
   }
   if(argc>2){ float t0=atof(argv[2]), t1=atof(argv[3]); for(auto& q:fr) if(q.t>=t0&&q.t<=t1) printf("t=%.3f peak=%.0fHz tonal=%.1f total=%.1fdB low=%.2f\n",q.t,q.peakHz,q.tonal,10.f*std::log10(q.total+1e-9f),q.low/(q.total+1e-9f)); return 0; }
   // whistles: runs of frames with a strong single tone (tonality) whose pitch keeps rising
   printf("== whistle candidates (tonal, rising)\n");
   size_t i=0;
   while(i<fr.size()){
      if(fr[i].tonal<20.f||fr[i].peakHz<2000.f){ i++; continue; }
      size_t j=i; float lastHz=fr[i].peakHz; int rising=0, steps=0;
      while(j+1<fr.size() && fr[j+1].tonal>=14.f && std::fabs(fr[j+1].peakHz-lastHz)<1300.f){ steps++; if(fr[j+1].peakHz>lastHz+4.f) rising++; lastHz=fr[j+1].peakHz; j++; }
      float dur=fr[j].t-fr[i].t;
      if(dur>=0.25f) printf("  %.2f s .. %.2f s  (%.2f s)  pitch %.0f -> %.0f Hz  tonality %.1f\n",fr[i].t,fr[j].t,dur,fr[i].peakHz,fr[j].peakHz,fr[i].tonal);
      i=j+1;
   }
   // bangs: a sudden jump of the total energy with a lot of bass
   printf("== bang candidates (onset + bass)\n");
   float prevAvg=0; int lastBang=-100;
   for(size_t k=8;k<fr.size();k++){
      float avg=0; for(size_t q=k-8;q<k-1;q++) avg+=fr[q].total; avg/=7.f;
      float rise=10.f*std::log10((fr[k].total+1e-9f)/(avg+1e-9f));
      float lowShare=fr[k].low/(fr[k].total+1e-9f);
      if(rise>9.f && lowShare>0.08f && (int)k-lastBang>40){ printf("  %.2f s  rise %.1f dB  bass share %.2f  total %.1f\n",fr[k].t,rise,lowShare,10.f*std::log10(fr[k].total+1e-9f)); lastBang=(int)k; }
   }
}
