#pragma once
// Languages. The source language of the program is Polish: every text in the code is a Polish "msgid" wrapped in T().
// A translation is a gettext catalog (.po file, one per language): msgid = the Polish text, msgstr = the translation.
// English is built into the exe (res/lang/en.po, a resource); more languages, or fixes of the built-in one, can be put into a
// "lang" folder next to the exe (xx.po; the header entry gives "Language: xx" and "Language-Name: ...").
// Texts with a value in them use {0}, {1} in the msgid (see fmt()); the order of the words is then the translator's.
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>

namespace i18n{
struct Lang{ std::wstring code, name; std::unordered_map<std::wstring,std::wstring> map; };
inline std::vector<Lang>& all(){ static std::vector<Lang> v; return v; }
inline int& cur(){ static int c=0; return c; }                                           // index in all(); 0 = Polish, the source language
inline std::unordered_map<const wchar_t*,const wchar_t*>& cache(){ static std::unordered_map<const wchar_t*,const wchar_t*> c; return c; }
inline FILE*& missLog(){ static FILE* f=nullptr; return f; }

inline std::wstring fromUtf8(const std::string& s){
   if(s.empty()) return L"";
   int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
   std::wstring w(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&w[0],n); return w;
}
// "..." with \n \t \" \\ escapes -> text
inline std::wstring unquote(const std::string& line,size_t from){
   size_t a=line.find('"',from), b=line.rfind('"'); if(a==std::string::npos||b==std::string::npos||b<=a) return L"";
   std::string out; for(size_t i=a+1;i<b;i++){
      if(line[i]=='\\'&&i+1<b){ char c=line[++i]; out+= c=='n'?'\n': c=='t'?'\t': c; } else out+=line[i];
   }
   return fromUtf8(out);
}
// A gettext .po file: msgid "..." / msgstr "..." (also split over several "..." lines); comments (#) are ignored.
inline void parsePo(const std::string& text,Lang& lang){
   std::wstring id, str; int state=0; bool have=false;                                    // state: 1 = in msgid, 2 = in msgstr
   auto flush=[&](){
      if(!have) return;
      if(id.empty()){                                                                       // the header
         size_t p=str.find(L"Language-Name:"); if(p!=std::wstring::npos){ size_t e=str.find(L'\n',p); lang.name=str.substr(p+14,e==std::wstring::npos?std::wstring::npos:e-p-14); while(!lang.name.empty()&&lang.name[0]==L' ') lang.name.erase(0,1); }
         p=str.find(L"Language:"); if(p!=std::wstring::npos&&str.compare(p>0?p-1:p,1,L"-")!=0){ size_t e=str.find(L'\n',p); std::wstring c=str.substr(p+9,e==std::wstring::npos?std::wstring::npos:e-p-9); while(!c.empty()&&c[0]==L' ') c.erase(0,1); if(!c.empty()) lang.code=c; }
      } else if(!str.empty()) lang.map[id]=str;
      id.clear(); str.clear(); have=false;
   };
   size_t pos=0;
   while(pos<=text.size()){
      size_t e=text.find('\n',pos); if(e==std::string::npos) e=text.size();
      std::string line=text.substr(pos,e-pos); pos=e+1;
      while(!line.empty()&&(line.back()=='\r')) line.pop_back();
      size_t k=line.find_first_not_of(" \t"); if(k==std::string::npos) continue; line=line.substr(k);
      if(line[0]=='#') continue;
      if(line.compare(0,5,"msgid")==0 && (line.size()==5||line[5]==' '||line[5]=='\t')){ flush(); state=1; have=true; id=unquote(line,5); }
      else if(line.compare(0,6,"msgstr")==0){ state=2; str=unquote(line,6); }
      else if(line[0]=='"'){ if(state==1) id+=unquote(line,0); else if(state==2) str+=unquote(line,0); }
   }
   flush();
}
inline bool readFile(const std::wstring& path,std::string& out){
   FILE* f=_wfopen(path.c_str(),L"rb"); if(!f) return false;
   char buf[8192]; size_t n; out.clear(); while((n=fread(buf,1,sizeof buf,f))>0) out.append(buf,n);
   fclose(f); return true;
}
inline Lang* find(const std::wstring& code){ for(auto& l:all()) if(l.code==code) return &l; return nullptr; }
inline std::wstring exeDir(){ wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH); std::wstring s=b; size_t k=s.find_last_of(L"\\/"); return s.substr(0,k+1); }

// Loads the built-in languages (resources LANG_xx) and the .po files of the "lang" folder next to the exe.
inline void init(HINSTANCE hInst){
   all().clear(); cache().clear();
   Lang pl; pl.code=L"pl"; pl.name=L"Polski"; all().push_back(pl);
   static const wchar_t* BUILTIN[]={L"LANG_EN"};
   for(const wchar_t* res:BUILTIN){
      HRSRC r=FindResourceW(hInst,res,(LPCWSTR)RT_RCDATA); if(!r) continue;
      HGLOBAL g=LoadResource(hInst,r); const char* p=(const char*)LockResource(g); DWORD n=SizeofResource(hInst,r); if(!p||!n) continue;
      Lang l; l.code=L"en"; l.name=L"English"; parsePo(std::string(p,n),l);
      all().push_back(l);
   }
   WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW((exeDir()+L"lang\\*.po").c_str(),&fd);        // more languages / fixes next to the exe
   if(h!=INVALID_HANDLE_VALUE){
      do{
         std::string text; if(!readFile(exeDir()+L"lang\\"+fd.cFileName,text)) continue;
         Lang l; std::wstring fn=fd.cFileName; l.code=fn.substr(0,fn.size()-3); parsePo(text,l);
         if(l.code==L"pl") continue;
         if(Lang* ex=find(l.code)){ for(auto& kv:l.map) ex->map[kv.first]=kv.second; if(!l.name.empty()) ex->name=l.name; }
         else { if(l.name.empty()) l.name=l.code; all().push_back(l); }
      } while(FindNextFileW(h,&fd));
      FindClose(h);
   }
}
inline std::wstring systemLanguageCode(){ return PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_POLISH ? L"pl" : L"en"; }
inline bool setLanguage(const std::wstring& code){
   for(size_t i=0;i<all().size();i++) if(all()[i].code==code){ cur()=(int)i; cache().clear(); return true; }
   return false;
}
inline const std::wstring& code(){ static std::wstring none=L"pl"; return all().empty()?none:all()[cur()].code; }

// The translation of a Polish text (a literal). Without a translation the text itself is returned.
inline const wchar_t* T(const wchar_t* pl){
   if(cur()==0||all().empty()) return pl;
   auto& c=cache(); auto it=c.find(pl); if(it!=c.end()) return it->second;
   const Lang& l=all()[cur()]; auto m=l.map.find(pl);
   const wchar_t* r=pl;
   if(m!=l.map.end()) r=m->second.c_str();
   else if(missLog()){ fwprintf(missLog(),L"%ls\n",pl); fflush(missLog()); }               // (/missing: the texts that have no translation)
   c[pl]=r; return r;
}
// the same for a text that is not a literal (a message that came from the server, ...)
inline std::wstring Tw(const std::wstring& pl){
   if(cur()==0||all().empty()) return pl;
   const Lang& l=all()[cur()]; auto m=l.map.find(pl); return m!=l.map.end()?m->second:pl;
}
// a text with places for values: {0}, {1} ... are replaced by the arguments (after the translation, so the order is free)
inline std::wstring fmt(const wchar_t* pl,const std::vector<std::wstring>& args){
   std::wstring s=T(pl);
   for(size_t i=0;i<args.size();i++){
      std::wstring key=L"{"+std::to_wstring(i)+L"}"; size_t p;
      while((p=s.find(key))!=std::wstring::npos) s.replace(p,key.size(),args[i]);
   }
   return s;
}
}  // namespace i18n
using i18n::T;
