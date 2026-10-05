#pragma once
// Self-update over GitHub Releases (WinHTTP, no external dependencies beyond
// what Windows itself ships). Taken from Pasjans Dziadkowy (MIT), adapted to Garibaldka.
//
// Flow: checkLatest() does one small HTTPS GET to GitHub's "latest release"
// API and hand-extracts the two fields we need (tag_name, and the
// browser_download_url of the asset named ASSET_NAME) — no JSON library,
// since we control exactly what gets uploaded to our own releases and only
// ever need these two well-known keys. isNewer() compares that tag against
// APP_VERSION (main.cpp) as a plain MAJOR.MINOR.PATCH tuple. If the caller
// downloads the new exe (httpDownload) and asks to install it
// (launchSelfUpdate), a detached helper .bat waits for this process to exit,
// swaps the file, relaunches, and deletes itself — Windows won't reliably
// let a running exe overwrite its own image file, so a second short-lived
// process has to do the swap after the original is really gone.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdio>
#pragma comment(lib,"winhttp.lib")

namespace update {

static const wchar_t* GH_OWNER   = L"NepciorLab";
static const wchar_t* GH_REPO    = L"Garibaldka";
static const wchar_t* ASSET_NAME = L"Garibaldi.exe"; // exact filename uploaded to each Release

struct ReleaseInfo {
   bool ok=false;
   std::wstring tag;         // e.g. "v1.2.3"
   std::wstring downloadUrl; // asset's browser_download_url
   std::wstring htmlUrl;     // release page, for "zobacz szczegóły"
   std::wstring notes;       // release body (changelog)
};

inline std::wstring toW(const std::string& s){
   if(s.empty()) return L"";
   int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
   std::wstring w((size_t)n,0);
   MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n);
   return w;
}

// Extracts the string value of "key":"..." from raw JSON text. Handles the
// handful of escape sequences GitHub's API actually emits (\n \" \\ \/) —
// not a general JSON parser, just enough for the two flat string fields this
// file reads out of a response we don't otherwise need to understand.
inline std::string jsonStr(const std::string& json,const std::string& key){
   std::string pat="\""+key+"\":\"";
   size_t p=json.find(pat);
   if(p==std::string::npos) return "";
   p+=pat.size();
   size_t e=p;
   while(e<json.size() && json[e]!='"'){ if(json[e]=='\\') e++; e++; }
   std::string raw=json.substr(p,e-p);
   std::string out; out.reserve(raw.size());
   for(size_t i=0;i<raw.size();i++){
      if(raw[i]=='\\' && i+1<raw.size()){
         char c=raw[i+1];
         if(c=='n'){ out+='\n'; i++; }
         else if(c=='"'){ out+='"'; i++; }
         else if(c=='\\'){ out+='\\'; i++; }
         else if(c=='/'){ out+='/'; i++; }
         else out+=raw[i];
      } else out+=raw[i];
   }
   return out;
}

// Plain HTTPS GET (api.github.com's JSON responses are small — read fully
// into memory). WinHttp validates the TLS certificate by default; no proxy
// handling beyond letting Windows use its own configured system proxy.
inline bool httpGet(const wchar_t* host,const wchar_t* path,std::string& outBody,const wchar_t* accept=nullptr){
   outBody.clear();
   HINTERNET hSession=WinHttpOpen(L"Garibaldka-Updater/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
   if(!hSession) return false;
   HINTERNET hConnect=WinHttpConnect(hSession,host,INTERNET_DEFAULT_HTTPS_PORT,0);
   if(!hConnect){ WinHttpCloseHandle(hSession); return false; }
   HINTERNET hRequest=WinHttpOpenRequest(hConnect,L"GET",path,nullptr,WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
   if(!hRequest){ WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
   std::wstring hdrs=L"User-Agent: Garibaldka-Updater\r\n";
   if(accept){ hdrs+=L"Accept: "; hdrs+=accept; hdrs+=L"\r\n"; }
   WinHttpAddRequestHeaders(hRequest,hdrs.c_str(),(DWORD)-1,WINHTTP_ADDREQ_FLAG_ADD);
   bool ok=WinHttpSendRequest(hRequest,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)!=0
        && WinHttpReceiveResponse(hRequest,nullptr)!=0;
   if(ok){
      for(;;){
         DWORD avail=0;
         if(!WinHttpQueryDataAvailable(hRequest,&avail) || avail==0) break;
         std::vector<char> buf(avail);
         DWORD read=0;
         if(!WinHttpReadData(hRequest,buf.data(),avail,&read) || read==0) break;
         outBody.append(buf.data(),read);
      }
   }
   WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
   return ok;
}

// Downloads an arbitrary https:// URL straight to a local file. Used for the
// release asset itself (a redirect from github.com to a signed
// objects.githubusercontent.com URL, which WinHttp follows automatically).
inline bool httpDownload(const std::wstring& url,const std::wstring& destPath){
   URL_COMPONENTS uc={}; uc.dwStructSize=sizeof(uc);
   wchar_t host[256]={}, path[2048]={};
   uc.lpszHostName=host; uc.dwHostNameLength=256;
   uc.lpszUrlPath=path;  uc.dwUrlPathLength=2048;
   uc.dwSchemeLength=(DWORD)-1;
   if(!WinHttpCrackUrl(url.c_str(),(DWORD)url.size(),0,&uc)) return false;

   HINTERNET hSession=WinHttpOpen(L"Garibaldka-Updater/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
   if(!hSession) return false;
   HINTERNET hConnect=WinHttpConnect(hSession,host,uc.nPort,0);
   if(!hConnect){ WinHttpCloseHandle(hSession); return false; }
   DWORD flags=(uc.nScheme==INTERNET_SCHEME_HTTPS)?WINHTTP_FLAG_SECURE:0;
   HINTERNET hRequest=WinHttpOpenRequest(hConnect,L"GET",path,nullptr,WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
   if(!hRequest){ WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
   bool ok=WinHttpSendRequest(hRequest,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)!=0
        && WinHttpReceiveResponse(hRequest,nullptr)!=0;
   if(ok){
      HANDLE hFile=CreateFileW(destPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(hFile==INVALID_HANDLE_VALUE) ok=false;
      else{
         for(;;){
            DWORD avail=0;
            if(!WinHttpQueryDataAvailable(hRequest,&avail) || avail==0) break;
            std::vector<char> buf(avail);
            DWORD read=0;
            if(!WinHttpReadData(hRequest,buf.data(),avail,&read) || read==0) break;
            DWORD written=0;
            WriteFile(hFile,buf.data(),read,&written,nullptr);
         }
         CloseHandle(hFile);
      }
   }
   WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
   return ok;
}

// Parses "vMAJOR.MINOR.PATCH" (leading v/V optional, anything after the
// third number — e.g. "-beta" — is ignored) into 3 ints for ordering.
inline bool parseVersion(const std::wstring& s,int out[3]){
   std::wstring t=s;
   if(!t.empty() && (t[0]==L'v'||t[0]==L'V')) t=t.substr(1);
   out[0]=out[1]=out[2]=0;
   int part=0,val=0; bool any=false;
   for(size_t i=0;i<=t.size();i++){
      if(i==t.size() || t[i]==L'.'){
         if(part<3) out[part]=val;
         part++; val=0;
         if(part>=3) break;
      } else if(t[i]>=L'0'&&t[i]<=L'9'){ val=val*10+(t[i]-L'0'); any=true; }
      else break; // first non-numeric, non-dot char ends parsing (e.g. "-beta")
   }
   return any;
}
inline bool isNewer(const std::wstring& remote,const std::wstring& local){
   int r[3],l[3];
   if(!parseVersion(remote,r) || !parseVersion(local,l)) return false;
   for(int i=0;i<3;i++) if(r[i]!=l[i]) return r[i]>l[i];
   return false;
}

// One HTTPS GET to GitHub's "latest release" API, no auth token needed for a
// public repo (well under the 60/hr unauthenticated rate limit for this).
inline ReleaseInfo checkLatest(){
   ReleaseInfo info;
   std::wstring path=L"/repos/"; path+=GH_OWNER; path+=L"/"; path+=GH_REPO; path+=L"/releases/latest";
   std::string body;
   if(!httpGet(L"api.github.com",path.c_str(),body,L"application/vnd.github+json")) return info;
   std::string tag=jsonStr(body,"tag_name");
   if(tag.empty()) return info;
   info.tag=toW(tag);
   info.htmlUrl=toW(jsonStr(body,"html_url"));
   info.notes=toW(jsonStr(body,"body"));
   std::string aname; for(const wchar_t* p=ASSET_NAME; *p; p++) aname+=(char)*p; // ASCII exe filename
   size_t ap=body.find("\"name\":\""+aname+"\"");
   if(ap==std::string::npos) return info; // release exists but no matching asset uploaded yet
   size_t up=body.find("\"browser_download_url\":\"",ap);
   if(up==std::string::npos) return info;
   std::string url=jsonStr(body.substr(up),"browser_download_url");
   if(url.empty()) return info;
   info.downloadUrl=toW(url);
   info.ok=true;
   return info;
}

// ANSI/system-codepage narrowing for the two paths embedded in the helper
// .bat below — see launchSelfUpdate()'s comment for why the file itself must
// be plain ANSI, not UTF-16.
inline std::string toAcp(const std::wstring& w){
   if(w.empty()) return "";
   int n=WideCharToMultiByte(CP_ACP,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
   std::string s((size_t)n,0);
   WideCharToMultiByte(CP_ACP,0,w.c_str(),(int)w.size(),&s[0],n,nullptr,nullptr);
   return s;
}

// Launches a detached helper .bat that waits for THIS process to exit,
// replaces targetExePath with newExePath, relaunches it, and deletes itself.
// Caller must already have downloaded newExePath, and must quit (e.g.
// PostQuitMessage) right after this returns true — the swap can't happen
// while the old exe is still running and holding its image file open.
inline bool launchSelfUpdate(const std::wstring& newExePath,const std::wstring& targetExePath){
   wchar_t tempDir[MAX_PATH]; GetTempPathW(MAX_PATH,tempDir);
   std::wstring batPath=std::wstring(tempDir)+L"garibaldi_update.bat";
   // Plain ANSI text (fopen, narrow fprintf), NOT _wfopen(..., L"w, ccs=UNICODE"):
   // that wrote a UTF-16LE file cmd.exe couldn't even recognize as a command
   // ("'garibaldi_update.bat' is not recognized..." — confirmed by reproducing
   // it directly) — the whole update silently did nothing past this point.
   // Text mode's own '\n'->CRLF translation also doubled the CR in the old
   // version's literal "\r\n" endings (FF FE ... 0D 00 0D 00 0A 00 in a hex
   // dump), which didn't help either. Paths here are plain filesystem paths,
   // no need for Unicode content — an ANSI file sidesteps both problems.
   FILE* f=_wfopen(batPath.c_str(),L"w");
   if(!f) return false;
   std::string target=toAcp(targetExePath), source=toAcp(newExePath);
   fprintf(f,
      "@echo off\n"
      "setlocal enabledelayedexpansion\n"
      "set TARGET=%s\n"
      "set SOURCE=%s\n"
      "set TRIES=0\n"
      ":wait\n"
      "ping -n 2 127.0.0.1 >nul\n"
      "move /y \"%%SOURCE%%\" \"%%TARGET%%\" >nul 2>&1\n"
      "if exist \"%%SOURCE%%\" (\n"
      "  set /a TRIES+=1\n"
      "  if !TRIES! LSS 30 goto wait\n"
      "  goto :eof\n"
      ")\n"
      "start \"\" \"%%TARGET%%\"\n"
      "del \"%%~f0\"\n",
      target.c_str(),source.c_str());
   fclose(f);
   STARTUPINFOW si={}; si.cb=sizeof(si);
   PROCESS_INFORMATION pi{};
   std::wstring cmd=L"cmd.exe /c \""+batPath+L"\"";
   std::vector<wchar_t> buf(cmd.begin(),cmd.end()); buf.push_back(0);
   BOOL ok=CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,
      CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
   if(ok){ CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
   return ok!=0;
}

} // namespace update
