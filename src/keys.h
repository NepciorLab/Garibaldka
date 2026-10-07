#pragma once
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <string>
#include "i18n.h"

// Configurable keyboard shortcuts (settings window, group "Sterowanie"). Every action has two slots.
// Fixed keys that are not configurable: Ctrl+Z (undo), Enter (chat), Esc (close a window), F6/F9/F10/F11 (debug aids).
enum KeyAction { KA_NEW=0, KA_UNDO, KA_HINT, KA_DRAW, KA_DISCARD, KA_NET, KA_HOT, KA_STATS, KA_SETTINGS, KA_RULES, KA_MUTE, KA_COUNT };

static const wchar_t* KA_LABELS[KA_COUNT]={
   L"Nowa gra", L"Cofnij", L"Podpowiedź", L"Dobierz kartę z talii", L"Odrzuć kartę / pas", L"Graj przez sieć",
   L"Hot seat", L"Statystyki", L"Ustawienia", L"Zasady", L"Dźwięk włączony / wyłączony",
};
static const wchar_t* KA_INI_KEYS[KA_COUNT]={
   L"NewGame", L"Undo", L"Hint", L"Draw", L"Discard", L"Network", L"HotSeat", L"Stats", L"Settings", L"Rules", L"Mute",
};
static const DWORD KA_DEFAULTS[KA_COUNT][2]={
   {VK_F2,0}, {VK_BACK,'U'}, {'H',0}, {VK_SPACE,0}, {'D',0}, {VK_F5,0}, {VK_F7,0}, {VK_F4,0}, {VK_F3,0}, {VK_F1,0}, {'M',0},
};

struct KeyBinding{ DWORD key[2]={0,0}; };
static KeyBinding g_keys[KA_COUNT];

static void keysDefaults(){ for(int i=0;i<KA_COUNT;i++){ g_keys[i].key[0]=KA_DEFAULTS[i][0]; g_keys[i].key[1]=KA_DEFAULTS[i][1]; } }

// The action bound to this key, or -1.
static int keyAction(DWORD vk){
   for(int i=0;i<KA_COUNT;i++) if(vk && (g_keys[i].key[0]==vk||g_keys[i].key[1]==vk)) return i;
   return -1;
}
// A key belongs to one action only: binding it elsewhere takes it away from the previous owner.
static void keySet(int action,int slot,DWORD vk){
   if(vk) for(int i=0;i<KA_COUNT;i++) for(int s=0;s<2;s++) if(g_keys[i].key[s]==vk) g_keys[i].key[s]=0;
   g_keys[action].key[slot]=vk;
}

static std::wstring vkName(DWORD vk){
   if(!vk) return L"—";
   switch(vk){
      case VK_LEFT: return L"←";   case VK_RIGHT: return L"→";   case VK_UP: return L"↑";   case VK_DOWN: return L"↓";
      case VK_ESCAPE: return L"Esc";   case VK_RETURN: return L"Enter";   case VK_SPACE: return T(L"Spacja");
      case VK_TAB: return L"Tab";   case VK_BACK: return L"Backspace";   case VK_DELETE: return L"Delete";
      case VK_INSERT: return L"Insert";   case VK_HOME: return L"Home";   case VK_END: return L"End";
      case VK_PRIOR: return L"PgUp";   case VK_NEXT: return L"PgDn";
   }
   if(vk>=VK_F1 && vk<=VK_F12){ wchar_t b[8]; wsprintfW(b,L"F%u",(unsigned)(vk-VK_F1+1)); return b; }
   if((vk>='0'&&vk<='9')||(vk>='A'&&vk<='Z')){ wchar_t b[2]={(wchar_t)vk,0}; return b; }
   wchar_t buf[40]={};
   UINT sc=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);
   if(GetKeyNameTextW((LONG)(sc<<16),buf,40)>0 && buf[0]) return buf;
   wsprintfW(buf,L"0x%02X",(unsigned)vk); return buf;
}
// keys that cannot be bound (modifiers on their own, and the ones with a fixed meaning)
static bool keyBindable(DWORD vk){
   switch(vk){
      case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
      case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_ESCAPE: case VK_RETURN:
      case VK_F6: case VK_F9: case VK_F10: case VK_F11: return false;
   }
   return vk!=0;
}

static void keysSave(const std::wstring& ini){
   wchar_t b[32];
   for(int i=0;i<KA_COUNT;i++){ wsprintfW(b,L"%u,%u",(unsigned)g_keys[i].key[0],(unsigned)g_keys[i].key[1]); WritePrivateProfileStringW(L"Keys",KA_INI_KEYS[i],b,ini.c_str()); }
}
static void keysLoad(const std::wstring& ini){
   keysDefaults();
   for(int i=0;i<KA_COUNT;i++){
      wchar_t b[48]={}; GetPrivateProfileStringW(L"Keys",KA_INI_KEYS[i],L"",b,48,ini.c_str());
      unsigned a=0,c=0; if(b[0] && swscanf(b,L"%u,%u",&a,&c)==2){ g_keys[i].key[0]=a; g_keys[i].key[1]=c; }
   }
}
