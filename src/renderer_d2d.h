#pragma once
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include "layout.h"
#include "card_images_d2d.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helper: rounded rectangle geometry
// ─────────────────────────────────────────────────────────────────────────────
inline D2D1_ROUNDED_RECT RRectD2D(float x,float y,float w,float h,float r){
   return D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),r,r);
}

// ─────────────────────────────────────────────────────────────────────────────
// RendererD2D
// ─────────────────────────────────────────────────────────────────────────────
class RendererD2D {
public:
   // Call after creating / recreating the render target
   void setRT(ID2D1RenderTarget* rt, IDWriteFactory* dw){
      m_rt=rt; m_dw=dw;
      releaseResources();
      if(rt) createResources();
   }
   void setLayout(const Layout* lay){ m_lay=lay; }

   // ── card ─────────────────────────────────────────────────────────────────
   void drawCard(float x,float y,const Card& card,bool selected,bool hinted) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      float r=(float)m_lay->cornerR;
      drawShadow(x,y);
      ID2D1Bitmap* bmp=GetCardD2D(card.imgKey(),m_rt);
      if(bmp){
         m_rt->DrawBitmap(bmp,D2D1::RectF(x,y,x+cw,y+ch));
      } else {
         // Fallback: white card
         auto rr=RRectD2D(x,y,cw,ch,r);
         m_rt->FillRoundedRectangle(rr,m_brWhite);
      }
      if(selected||hinted) drawHighlightOverlay(x,y,selected);
   }

   // ── card back ─────────────────────────────────────────────────────────────
   // A card mid-turn about its vertical axis, scaled uniformly about its centre
   // (x,y = the card's top-left at normal size). flip: 0 = back fully showing,
   // 0.5 = edge-on, 1 = face fully showing; the face is drawn unmirrored once
   // past edge-on, exactly as a real half-turn would show the other side.
   void drawCardFlip(float x,float y,const Card& card,int deck,float scale,float flip) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      float wf=std::max(0.03f,fabsf(cosf(flip*3.14159265f)));
      D2D1::Matrix3x2F old; m_rt->GetTransform(&old);
      m_rt->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(scale*wf,scale),
         D2D1::Point2F(x+cw/2.f,y+ch/2.f))*old);
      if(flip>=0.5f) drawCard(x,y,card,false,false);
      else           drawBackOne(x,y,deck==1?"B2":"B1");
      m_rt->SetTransform(old);
   }

   // deck 0 = red back, deck 1 = blue back (see reserveDeckTagsForSeed in game.h)
   void drawBack(float x,float y,int deck) const {
      if(!m_rt) return;
      drawBackOne(x,y,deck==1?"B2":"B1");
   }

   // ── empty slot ───────────────────────────────────────────────────────────
   void drawEmpty(float x,float y,const wchar_t* label) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      auto rr=RRectD2D(x,y,cw,ch,(float)m_lay->cornerR);
      m_rt->FillRoundedRectangle(rr,m_brSlotFill);
      m_rt->DrawRoundedRectangle(rr,m_brSlotBorder,1.5f);
      if(label&&*label) drawCenteredText(label,x,y,cw,ch,m_brLabelText,11.f);
   }

   // Pulsing empty slot: brightens fill and border with alpha (0=normal, 1=fully lit)
   void drawEmptyPulsing(float x,float y,float alpha) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      float r=(float)m_lay->cornerR;
      auto rr=RRectD2D(x,y,cw,ch,r);
      // Base slot
      m_rt->FillRoundedRectangle(rr,m_brSlotFill);
      m_rt->DrawRoundedRectangle(rr,m_brSlotBorder,1.5f);
      if(alpha<=0.f) return;
      // Bright fill overlay – pulses from transparent to vivid white-gold
      ID2D1SolidColorBrush* brFill=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.97f,0.7f,alpha*0.82f),&brFill);
      if(brFill){
         m_rt->FillRoundedRectangle(rr,brFill);
         brFill->Release();
      }
      // Bright border – thickens with alpha
      ID2D1SolidColorBrush* brBor=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,0.92f,0.2f,0.6f+alpha*0.4f),&brBor);
      if(brBor){
         m_rt->DrawRoundedRectangle(rr,brBor,1.5f+alpha*3.5f);
         brBor->Release();
      }
   }

   // Fading empty slot: drawn at reduced alpha as the slot disappears when a card lands
   void drawEmptyFading(float x,float y,float alpha) const {
      if(!m_rt||alpha<=0.f) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      auto rr=RRectD2D(x,y,cw,ch,(float)m_lay->cornerR);
      ID2D1SolidColorBrush* brFill=nullptr;
      ID2D1SolidColorBrush* brBor=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.12f*alpha),&brFill);
      m_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,0.31f*alpha),&brBor);
      if(brFill){m_rt->FillRoundedRectangle(rr,brFill);brFill->Release();}
      if(brBor) {m_rt->DrawRoundedRectangle(rr,brBor,1.5f);brBor->Release();}
   }

   // ── empty foundation slot ─────────────────────────────────────────────────
   void drawEmptyFound(float x,float y,int slot) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      auto rr=RRectD2D(x,y,cw,ch,(float)m_lay->cornerR);
      m_rt->FillRoundedRectangle(rr,m_brSlotFill);
      m_rt->DrawRoundedRectangle(rr,m_brSlotBorder,1.5f);

      const wchar_t* suits[]={L"♠",L"♠",L"♥",L"♥",L"♦",L"♦",L"♣",L"♣"};
      bool isRed=(slot==2||slot==3||slot==4||slot==5);
      ID2D1SolidColorBrush* br=isRed?m_brFoundRed:m_brFoundBlack;

      float fsA=std::max(10.f,cw*0.26f);
      float fsS=std::max(9.f, cw*0.32f);

      // Measure A
      IDWriteTextLayout* layA=nullptr; IDWriteTextLayout* layS=nullptr;
      IDWriteTextFormat* fmtA=nullptr; IDWriteTextFormat* fmtS=nullptr;
      if(m_dw){
         m_dw->CreateTextFormat(L"Georgia",nullptr,
            DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,fsA*(96.f/72.f),L"",&fmtA);
         m_dw->CreateTextFormat(L"Segoe UI",nullptr,
            DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,fsS*(96.f/72.f),L"",&fmtS);
         if(fmtA) m_dw->CreateTextLayout(L"A",1,fmtA,cw,ch,&layA);
         if(fmtS) m_dw->CreateTextLayout(suits[slot],(UINT32)wcslen(suits[slot]),fmtS,cw,ch,&layS);
      }
      float hA=layA?metrics(layA).height:fsA*1.2f;
      float hS=layS?metrics(layS).height:fsS*1.2f;
      float totalH=hA+4.f+hS;
      float sy=y+(ch-totalH)/2.f;

      if(layA){
         float wA=metrics(layA).width;
         m_rt->DrawTextLayout(D2D1::Point2F(x+(cw-wA)/2.f,sy),layA,br);
         layA->Release();
      }
      if(layS){
         float wS=metrics(layS).width;
         m_rt->DrawTextLayout(D2D1::Point2F(x+(cw-wS)/2.f,sy+hA+4.f),layS,br);
         layS->Release();
      }
      if(fmtA) fmtA->Release();
      if(fmtS) fmtS->Release();
   }

   // Dim overlay drawn over a card that currently cannot be picked up/moved
   void drawDimOverlay(float x,float y) const {
      if(!m_rt) return;
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      auto rr=RRectD2D(x,y,cw,ch,(float)m_lay->cornerR);
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.19f),&br);
      if(br){m_rt->FillRoundedRectangle(rr,br);br->Release();}
   }

   // Thin outline around a movable sequence of cards (bounding box from the
   // top of the first card in the sequence to the bottom of the last one).
   // Color is meant to be a slightly darker shade of the table felt.
   void drawSeqOutline(float x,float y,float w,float h,BYTE r,BYTE g,BYTE b) const {
      if(!m_rt) return;
      auto rr=RRectD2D(x-2,y-2,w+4,h+4,(float)m_lay->cornerR+2);
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(r/255.f,g/255.f,b/255.f,0.85f),&br);
      if(br){m_rt->DrawRoundedRectangle(rr,br,2.f);br->Release();}
   }

   // ── highlight ring ────────────────────────────────────────────────────────
   void drawHighlight(float x,float y) const {
      if(!m_rt) return;
      float r=(float)m_lay->cornerR+3;
      auto rr=RRectD2D(x-3,y-3,(float)m_lay->cardW+6,(float)m_lay->cardH+6,r);
      m_rt->DrawRoundedRectangle(rr,m_brHighlight,2.5f);
   }

   // ── reserve count text ────────────────────────────────────────────────────
   void drawReserveCount(float x,float y,float cw,float ch,
                         const std::wstring& txt) const {
      if(!m_rt||!m_dw) return;
      float fs=std::max(10.f,13.f*cw/71.f);
      IDWriteTextFormat* fmt=nullptr;
      m_dw->CreateTextFormat(L"Segoe UI",nullptr,
         DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
         DWRITE_FONT_STRETCH_NORMAL,fs*(96.f/72.f),L"",&fmt);
      if(!fmt) return;
      IDWriteTextLayout* lay=nullptr;
      m_dw->CreateTextLayout(txt.c_str(),(UINT32)txt.size(),fmt,200,50,&lay);
      if(lay){
         float tw=metrics(lay).width;
         m_rt->DrawTextLayout(
            D2D1::Point2F(x-tw-8.f, y+(ch-metrics(lay).height)/2.f),
            lay,m_brReserveCount);
         lay->Release();
      }
      fmt->Release();
   }

   // ── status bar text ───────────────────────────────────────────────────────
   void drawStatusText(const std::wstring& txt, float winW,
                       bool won, bool noMoves, int moveCount, int seconds,
                       bool thinking, float thinkAlpha,
                       float mlx, float mly, long long gameNumber,
                       D2D1_RECT_F* moveLabelRect=nullptr) const {
      if(!m_rt||!m_dw) return;
      float fs=13.f;
      IDWriteTextFormat* fmt=nullptr;
      m_dw->CreateTextFormat(L"Segoe UI",nullptr,
         DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,
         DWRITE_FONT_STRETCH_NORMAL,fs*(96.f/72.f),L"",&fmt);
      if(!fmt) return;

      float curY=mly;

      // "Układ #N" — shown above the move count, when a game number is known
      if(gameNumber>=0){
         std::wstring deal=L"Uk\u0142ad #"+std::to_wstring(gameNumber);
         IDWriteTextLayout* layD=nullptr;
         m_dw->CreateTextLayout(deal.c_str(),(UINT32)deal.size(),fmt,400,40,&layD);
         if(layD){
            auto d=metrics(layD);
            m_rt->DrawTextLayout(D2D1::Point2F(mlx,curY),layD,m_brStatusText);
            curY+=d.height+2.f;
            layD->Release();
         }
      }

      // "Ruchy: N"
      std::wstring moves=L"Ruchy: "+std::to_wstring(moveCount);
      IDWriteTextLayout* layM=nullptr;
      m_dw->CreateTextLayout(moves.c_str(),(UINT32)moves.size(),fmt,400,40,&layM);
      float labelH=0.f;
      if(layM){
         auto m=metrics(layM);
         m_rt->DrawTextLayout(D2D1::Point2F(mlx,curY),layM,m_brStatusText);
         if(moveLabelRect) *moveLabelRect=D2D1::RectF(mlx,mly,mlx+m.width,curY+m.height);
         labelH=m.height;
         layM->Release();
      }

      // "MM:SS" (or "H:MM:SS" once the game runs an hour or more) under move count
      float timeH=0.f;
      {
         int hh=seconds/3600, mm=(seconds%3600)/60, ss=seconds%60;
         wchar_t tbuf[24];
         if(hh>0) wsprintfW(tbuf,L"%d:%02d:%02d",hh,mm,ss);
         else     wsprintfW(tbuf,L"%02d:%02d",mm,ss);
         IDWriteTextLayout* layT=nullptr;
         m_dw->CreateTextLayout(tbuf,(UINT32)wcslen(tbuf),fmt,200,30,&layT);
         if(layT){
            auto t=metrics(layT);
            timeH=t.height;
            m_rt->DrawTextLayout(D2D1::Point2F(mlx,curY+labelH+2.f),layT,m_brStatusText);
            layT->Release();
         }
      }

      // "Myślę" — shown under the clock while Hint/Auto-move is waiting on
      // the engine, pulsing from fully transparent (blending into the felt)
      // up to the normal status-text color and back, roughly every 500ms.
      if(thinking){
         m_brThinking->SetColor(D2D1::ColorF(1,1,1,thinkAlpha));
         std::wstring th=L"Myślę";
         IDWriteTextLayout* layTh=nullptr;
         m_dw->CreateTextLayout(th.c_str(),(UINT32)th.size(),fmt,200,30,&layTh);
         if(layTh){
            m_rt->DrawTextLayout(D2D1::Point2F(mlx,curY+labelH+2.f+timeH+2.f),layTh,m_brThinking);
            layTh->Release();
         }
      }

      // Status message
      if(!txt.empty()){
         ID2D1SolidColorBrush* br=won?m_brStatusWin:noMoves?m_brStatusNomoves:m_brStatusText;
         IDWriteTextLayout* layS=nullptr;
         m_dw->CreateTextLayout(txt.c_str(),(UINT32)txt.size(),fmt,400,40,&layS);
         if(layS){
            float tw=metrics(layS).width;
            m_rt->DrawTextLayout(D2D1::Point2F(winW-tw-12.f,28.f),layS,br);
            layS->Release();
         }
      }
      fmt->Release();
   }

   void drawEllipse(float x,float y,float r,
                    BYTE red,BYTE grn,BYTE blu,BYTE alpha) const {
      if(!m_rt||r<0.3f) return;
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(red/255.f,grn/255.f,blu/255.f,alpha/255.f),&br);
      if(br){m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),r,r),br);br->Release();}
   }

   void drawLine(float x0,float y0,float x1,float y1,float w,
                 BYTE red,BYTE grn,BYTE blu,BYTE alpha) const {
      if(!m_rt) return;
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(red/255.f,grn/255.f,blu/255.f,alpha/255.f),&br);
      if(br){m_rt->DrawLine(D2D1::Point2F(x0,y0),D2D1::Point2F(x1,y1),br,w);br->Release();}
   }

   // White overlay with rounded corners (for reserve pulse hint)
   void drawWhiteOverlay(float x,float y,float cw,float ch,float alpha) const {
      if(!m_rt||alpha<=0.f) return;
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,alpha),&br);
      if(br){m_rt->FillRoundedRectangle(RRectD2D(x,y,cw,ch,(float)m_lay->cornerR),br);br->Release();}
   }

   // Pulsing white card for empty-slot hint
   void drawPulseHighlight(float x,float y,float cw,float ch,float alpha) const {
      if(!m_rt||alpha<=0.f) return;
      float r=(float)m_lay->cornerR;
      {  int off=std::max(1,m_lay->cardW/40);
         ID2D1SolidColorBrush* brs=nullptr;
         m_rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.20f*alpha),&brs);
         if(brs){m_rt->FillRoundedRectangle(RRectD2D(x+off,y+off,cw,ch,r),brs);brs->Release();}
      }
      ID2D1SolidColorBrush* br=nullptr;
      m_rt->CreateSolidColorBrush(D2D1::ColorF(1.f,1.f,1.f,alpha),&br);
      if(br){m_rt->FillRoundedRectangle(RRectD2D(x,y,cw,ch,r),br);br->Release();}
   }

   void releaseResources(){
      auto rel=[](auto*& p){if(p){p->Release();p=nullptr;}};
      rel(m_brWhite); rel(m_brBlue); rel(m_brGray);
      rel(m_brSlotFill); rel(m_brSlotBorder); rel(m_brLabelText);
      rel(m_brHighlight); rel(m_brShadow);
      rel(m_brFoundRed); rel(m_brFoundBlack);
      rel(m_brReserveCount);
      rel(m_brStatusText); rel(m_brStatusWin); rel(m_brStatusNomoves); rel(m_brThinking);
   }

private:
   ID2D1RenderTarget* m_rt=nullptr;
   IDWriteFactory*    m_dw=nullptr;
   const Layout*      m_lay=nullptr;

   ID2D1SolidColorBrush* m_brWhite         =nullptr;
   ID2D1SolidColorBrush* m_brBlue          =nullptr;
   ID2D1SolidColorBrush* m_brGray          =nullptr;
   ID2D1SolidColorBrush* m_brSlotFill      =nullptr;
   ID2D1SolidColorBrush* m_brSlotBorder    =nullptr;
   ID2D1SolidColorBrush* m_brLabelText     =nullptr;
   ID2D1SolidColorBrush* m_brHighlight     =nullptr;
   ID2D1SolidColorBrush* m_brShadow        =nullptr;
   ID2D1SolidColorBrush* m_brFoundRed      =nullptr;
   ID2D1SolidColorBrush* m_brFoundBlack    =nullptr;
   ID2D1SolidColorBrush* m_brReserveCount  =nullptr;
   ID2D1SolidColorBrush* m_brStatusText    =nullptr;
   ID2D1SolidColorBrush* m_brStatusWin     =nullptr;
   ID2D1SolidColorBrush* m_brStatusNomoves =nullptr;
   ID2D1SolidColorBrush* m_brThinking      =nullptr; // "Myślę" pulse — color set per frame

   void createResources(){
      auto mk=[&](ID2D1SolidColorBrush*& br, D2D1_COLOR_F c){
         m_rt->CreateSolidColorBrush(c,&br);
      };
      mk(m_brWhite,        D2D1::ColorF(1,1,1,1));
      mk(m_brBlue,         D2D1::ColorF(0.08f,0.31f,0.71f,1));
      mk(m_brGray,         D2D1::ColorF(0.7f,0.7f,0.7f,0.8f));
      mk(m_brSlotFill,     D2D1::ColorF(0,0,0,0.12f));
      mk(m_brSlotBorder,   D2D1::ColorF(1,1,1,0.31f));
      mk(m_brLabelText,    D2D1::ColorF(1,1,1,0.43f));
      mk(m_brHighlight,    D2D1::ColorF(1,0.94f,0,0.82f));
      mk(m_brShadow,       D2D1::ColorF(0,0,0,0.20f));
      mk(m_brFoundRed,     D2D1::ColorF(0.78f,0,0,0.71f));
      mk(m_brFoundBlack,   D2D1::ColorF(0.08f,0.08f,0.08f,0.71f));
      mk(m_brReserveCount, D2D1::ColorF(0.78f,0.78f,0.78f,0.78f));
      mk(m_brStatusText,   D2D1::ColorF(1,1,1,0.47f));
      mk(m_brStatusWin,    D2D1::ColorF(1,0.86f,0.20f,1));
      mk(m_brStatusNomoves,D2D1::ColorF(1,0.43f,0.43f,1));
      mk(m_brThinking,     D2D1::ColorF(1,1,1,0.f));
   }

   void drawBackOne(float x,float y,const char* key) const {
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      drawShadow(x,y);
      ID2D1Bitmap* bmp=GetCardD2D(key,m_rt);
      if(bmp){
         m_rt->DrawBitmap(bmp,D2D1::RectF(x,y,x+cw,y+ch));
      } else {
         auto rr=RRectD2D(x,y,cw,ch,(float)m_lay->cornerR);
         m_rt->FillRoundedRectangle(rr,m_brBlue);
         m_rt->DrawRoundedRectangle(rr,m_brGray,0.8f);
      }
   }

   void drawShadow(float x,float y) const {
      int off=std::max(1,m_lay->cardW/40);
      auto rr=RRectD2D(x+off,y+off,
         (float)m_lay->cardW,(float)m_lay->cardH,(float)m_lay->cornerR);
      m_rt->FillRoundedRectangle(rr,m_brShadow);
   }

   void drawHighlightOverlay(float x,float y,bool selected) const {
      float cw=(float)m_lay->cardW, ch=(float)m_lay->cardH;
      float r=(float)m_lay->cornerR;
      auto rr=RRectD2D(x,y,cw,ch,r);
      if(selected){
         ID2D1SolidColorBrush* br=nullptr;
         m_rt->CreateSolidColorBrush(D2D1::ColorF(0.12f,0.56f,1,0.39f),&br);
         if(br){m_rt->FillRoundedRectangle(rr,br);
                m_rt->DrawRoundedRectangle(rr,br,2.5f);br->Release();}
      } else {
         ID2D1SolidColorBrush* br=nullptr;
         m_rt->CreateSolidColorBrush(D2D1::ColorF(1,0.78f,0,0.90f),&br);
         if(br){m_rt->DrawRoundedRectangle(rr,br,2.5f);br->Release();}
      }
   }

   void drawCenteredText(const wchar_t* txt,
                         float x,float y,float cw,float ch,
                         ID2D1SolidColorBrush* br, float fs) const {
      if(!m_dw) return;
      IDWriteTextFormat* fmt=nullptr;
      m_dw->CreateTextFormat(L"Segoe UI",nullptr,
         DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
         DWRITE_FONT_STRETCH_NORMAL,fs*(96.f/72.f),L"",&fmt);
      if(!fmt) return;
      IDWriteTextLayout* lay=nullptr;
      m_dw->CreateTextLayout(txt,(UINT32)wcslen(txt),fmt,cw,ch,&lay);
      if(lay){
         auto m=metrics(lay);
         m_rt->DrawTextLayout(
            D2D1::Point2F(x+(cw-m.width)/2.f,y+(ch-m.height)/2.f),
            lay,br);
         lay->Release();
      }
      fmt->Release();
   }

   struct TM{ float width,height; };
   static TM metrics(IDWriteTextLayout* lay){
      DWRITE_TEXT_METRICS m{}; lay->GetMetrics(&m);
      return {m.width,m.height};
   }
};
