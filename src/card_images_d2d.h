#pragma once
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

// Loads PNG card images from Win32 RCDATA resources and caches them as
// ID2D1Bitmap objects (live in VRAM on the render target).
// Call invalidate() when the render target is recreated.

class CardImagesD2D {
public:
   static CardImagesD2D& instance(){
      static CardImagesD2D inst;
      return inst;
   }

   void init(HINSTANCE hInst, IWICImagingFactory* wic){
      m_hInst=hInst; m_wic=wic;
   }

   // Returns a cached ID2D1Bitmap. Loads and caches on first call.
   // rt must be valid; if target changed call invalidate() first.
   ID2D1Bitmap* get(const std::string& key, ID2D1RenderTarget* rt){
      auto it=m_cache.find(key);
      if(it!=m_cache.end()) return it->second;
      ID2D1Bitmap* bmp=load(key,rt);
      m_cache[key]=bmp; // nullptr on failure — remember to skip
      return bmp;
   }

   // Call when render target is recreated (device lost / resize with new RT)
   void invalidate(){
      for(auto& kv:m_cache) if(kv.second) kv.second->Release();
      m_cache.clear();
   }

   ~CardImagesD2D(){ invalidate(); }

private:
   CardImagesD2D():m_hInst(nullptr),m_wic(nullptr){}
   CardImagesD2D(const CardImagesD2D&)=delete;

   ID2D1Bitmap* load(const std::string& key, ID2D1RenderTarget* rt){
      if(!m_hInst||!m_wic||!rt) return nullptr;
      std::string resName=(key.rfind("IMG_",0)==0)?key:"CARD_"+key; // IMG_* = UI icons
      HRSRC hRes=FindResourceA(m_hInst,resName.c_str(),(LPCSTR)RT_RCDATA);
      if(!hRes) return nullptr;
      DWORD size=SizeofResource(m_hInst,hRes);
      HGLOBAL hLoad=LoadResource(m_hInst,hRes);
      if(!hLoad||!size) return nullptr;
      void* pSrc=LockResource(hLoad);
      if(!pSrc) return nullptr;

      // Copy to HGLOBAL stream
      HGLOBAL hMem=GlobalAlloc(GMEM_MOVEABLE,size);
      if(!hMem) return nullptr;
      void* pDst=GlobalLock(hMem);
      if(!pDst){GlobalFree(hMem);return nullptr;}
      memcpy(pDst,pSrc,size);
      GlobalUnlock(hMem);

      IStream* stream=nullptr;
      if(FAILED(CreateStreamOnHGlobal(hMem,TRUE,&stream))) return nullptr;

      // Decode with WIC
      IWICBitmapDecoder*     dec=nullptr;
      IWICBitmapFrameDecode* frame=nullptr;
      IWICFormatConverter*   conv=nullptr;
      ID2D1Bitmap*           bmp=nullptr;

      if(FAILED(m_wic->CreateDecoderFromStream(stream,nullptr,
            WICDecodeMetadataCacheOnLoad,&dec))) goto done;
      if(FAILED(dec->GetFrame(0,&frame))) goto done;
      if(FAILED(m_wic->CreateFormatConverter(&conv))) goto done;
      if(FAILED(conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,nullptr,0.f,
            WICBitmapPaletteTypeMedianCut))) goto done;
      rt->CreateBitmapFromWicBitmap(conv,nullptr,&bmp);

   done:
      if(conv)  conv->Release();
      if(frame) frame->Release();
      if(dec)   dec->Release();
      if(stream)stream->Release();
      return bmp;
   }

   HINSTANCE            m_hInst;
   IWICImagingFactory*  m_wic;
   std::map<std::string,ID2D1Bitmap*> m_cache;
};

inline ID2D1Bitmap* GetCardD2D(const std::string& key, ID2D1RenderTarget* rt){
   return CardImagesD2D::instance().get(key,rt);
}
