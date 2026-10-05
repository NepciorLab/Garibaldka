#pragma once
// Minimal TCP connection between two players (Winsock). One side listens (host), the other connects (guest).
// Messages are text lines (UTF-8, '\n'-terminated). A background thread does all the blocking work and hands
// every event to the main thread as a window message, so the game itself never blocks on the network.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

namespace net {

static const UINT WM_NET = WM_APP+10;                  // wParam = event, lParam = heap-allocated std::string*
enum { EV_CONNECTED=1, EV_LINE=2, EV_CLOSED=3, EV_FAILED=4 };
static const int DEFAULT_PORT = 47321;

inline void post(HWND hwnd,int ev,const std::string& text){
   PostMessageW(hwnd,WM_NET,(WPARAM)ev,(LPARAM)new std::string(text));
}

// What the game needs from a connection (a direct TCP link in a local network, or a WebSocket to the online server).
class Link {
public:
   virtual ~Link(){}
   virtual bool sendLine(const std::string& line)=0;
   virtual void close()=0;
   virtual bool connected() const=0;
};

class Conn : public Link {
public:
   ~Conn(){ close(); }

   // Waits for ONE incoming connection on `port`. false + `err` if the port cannot be opened.
   bool listenOn(HWND hwnd,int port,std::string& err){
      close(); startup();
      SOCKET ls=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
      if(ls==INVALID_SOCKET){ err="socket"; return false; }
      BOOL yes=TRUE; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof yes);
      sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons((u_short)port);
      if(bind(ls,(sockaddr*)&a,sizeof a)!=0||listen(ls,1)!=0){
         err="port "+std::to_string(port)+" is in use (WSA "+std::to_string(WSAGetLastError())+")"; closesocket(ls); return false;
      }
      m_hwnd=hwnd; m_listen=ls; m_stop=0;
      m_thread=CreateThread(nullptr,0,hostProc,this,0,nullptr);
      return m_thread!=nullptr;
   }
   // Connects to host:port in the background (EV_CONNECTED or EV_FAILED follows).
   bool connectTo(HWND hwnd,const std::string& host,int port){
      close(); startup();
      m_hwnd=hwnd; m_host=host; m_port=port; m_stop=0;
      m_thread=CreateThread(nullptr,0,guestProc,this,0,nullptr);
      return m_thread!=nullptr;
   }
   bool sendLine(const std::string& line) override {
      EnterCriticalSection(&m_cs);
      bool ok=false;
      if(m_sock!=INVALID_SOCKET){
         std::string d=line+"\n"; const char* p=d.data(); int left=(int)d.size(); ok=true;
         while(left>0){ int n=send(m_sock,p,left,0); if(n<=0){ ok=false; break; } p+=n; left-=n; }
      }
      LeaveCriticalSection(&m_cs);
      return ok;
   }
   bool connected() const override { return m_sock!=INVALID_SOCKET; }
   void close() override {
      InterlockedExchange(&m_stop,1);
      EnterCriticalSection(&m_cs);
      if(m_sock!=INVALID_SOCKET) shutdown(m_sock,SD_BOTH);
      if(m_listen!=INVALID_SOCKET){ closesocket(m_listen); m_listen=INVALID_SOCKET; }
      LeaveCriticalSection(&m_cs);
      if(m_thread){ WaitForSingleObject(m_thread,1500); CloseHandle(m_thread); m_thread=nullptr; }
      EnterCriticalSection(&m_cs);
      if(m_sock!=INVALID_SOCKET){ closesocket(m_sock); m_sock=INVALID_SOCKET; }
      LeaveCriticalSection(&m_cs);
   }
   Conn(){ InitializeCriticalSection(&m_cs); }
   Conn(const Conn&)=delete;

private:
   HWND m_hwnd=nullptr; SOCKET m_listen=INVALID_SOCKET, m_sock=INVALID_SOCKET; HANDLE m_thread=nullptr;
   volatile LONG m_stop=0; CRITICAL_SECTION m_cs; std::string m_host; int m_port=0;

   static void startup(){ static bool done=false; if(!done){ WSADATA w; WSAStartup(MAKEWORD(2,2),&w); done=true; } }

   void readLoop(){
      std::string buf; char tmp[2048];
      for(;;){
         int n=recv(m_sock,tmp,sizeof tmp,0);
         if(n<=0) break;
         buf.append(tmp,n);
         size_t nl;
         while((nl=buf.find('\n'))!=std::string::npos){
            std::string line=buf.substr(0,nl); buf.erase(0,nl+1);
            if(!line.empty()&&line.back()=='\r') line.pop_back();
            if(!line.empty()) post(m_hwnd,EV_LINE,line);
         }
      }
      if(!m_stop) post(m_hwnd,EV_CLOSED,"");
   }
   static DWORD WINAPI hostProc(LPVOID p){
      Conn* c=(Conn*)p;
      SOCKET ls=c->m_listen;
      for(;;){                                         // wait for a guest, looking at the stop flag now and then
         if(c->m_stop) return 0;
         fd_set fs; FD_ZERO(&fs); FD_SET(ls,&fs); timeval tv{0,200000};
         int r=select(0,&fs,nullptr,nullptr,&tv);
         if(r<0) return 0;
         if(r>0){
            sockaddr_in pa{}; int len=sizeof pa;
            SOCKET s=accept(ls,(sockaddr*)&pa,&len);
            if(s==INVALID_SOCKET) return 0;
            BOOL nd=TRUE; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&nd,sizeof nd);
            EnterCriticalSection(&c->m_cs);
            c->m_sock=s; closesocket(c->m_listen); c->m_listen=INVALID_SOCKET;
            LeaveCriticalSection(&c->m_cs);
            char ip[64]={}; inet_ntop(AF_INET,&pa.sin_addr,ip,sizeof ip);
            post(c->m_hwnd,EV_CONNECTED,ip);
            c->readLoop();
            return 0;
         }
      }
   }
   static DWORD WINAPI guestProc(LPVOID p){
      Conn* c=(Conn*)p;
      addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; addrinfo* res=nullptr;
      if(getaddrinfo(c->m_host.c_str(),std::to_string(c->m_port).c_str(),&hints,&res)!=0||!res){ post(c->m_hwnd,EV_FAILED,"nie znaleziono adresu"); return 0; }
      SOCKET s=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
      u_long nb=1; ioctlsocket(s,FIONBIO,&nb);
      connect(s,res->ai_addr,(int)res->ai_addrlen);
      bool ok=false;
      for(int i=0;i<30&&!c->m_stop;i++){                 // up to ~6 s
         fd_set w,e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s,&w); FD_SET(s,&e); timeval tv{0,200000};
         int r=select(0,nullptr,&w,&e,&tv);
         if(r>0){ ok=FD_ISSET(s,&w)&&!FD_ISSET(s,&e); break; }
      }
      freeaddrinfo(res);
      if(!ok){ closesocket(s); if(!c->m_stop) post(c->m_hwnd,EV_FAILED,"brak odpowiedzi hosta"); return 0; }
      nb=0; ioctlsocket(s,FIONBIO,&nb);
      BOOL nd=TRUE; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&nd,sizeof nd);
      EnterCriticalSection(&c->m_cs); c->m_sock=s; LeaveCriticalSection(&c->m_cs);
      post(c->m_hwnd,EV_CONNECTED,c->m_host);
      c->readLoop();
      return 0;
   }
};


// ---------------------------------------------------------------------------
// WebSocket client (WinHTTP, Windows 8+; TLS for https:// / wss:// is done by Windows). Every text frame is one line.
// ---------------------------------------------------------------------------
class WsConn : public Link {
public:
   WsConn(){ InitializeCriticalSection(&m_cs); }
   WsConn(const WsConn&)=delete;
   ~WsConn(){ close(); }

   // url: https://host[:port][/path] | http://... | wss:// | ws:// | bare host (-> https). Default path: /ws
   bool connectTo(HWND hwnd,const std::string& url){
      close();
      std::string u=url; bool secure=true;
      auto starts=[&](const char* p){ return u.rfind(p,0)==0; };
      if(starts("https://")){ u=u.substr(8); } else if(starts("wss://")){ u=u.substr(6); }
      else if(starts("http://")){ u=u.substr(7); secure=false; } else if(starts("ws://")){ u=u.substr(5); secure=false; }
      std::string path="/ws"; size_t sl=u.find('/');
      if(sl!=std::string::npos){ if(u.size()>sl+1) path=u.substr(sl); u=u.substr(0,sl); }
      int port=secure?443:80; size_t colon=u.rfind(':');
      if(colon!=std::string::npos){ port=atoi(u.c_str()+colon+1); u=u.substr(0,colon); }
      if(u.empty()) return false;
      m_hwnd=hwnd; m_secure=secure; m_port=port; m_path=widen(path); m_host=widen(u); m_stop=0;
      m_thread=CreateThread(nullptr,0,proc,this,0,nullptr);
      return m_thread!=nullptr;
   }
   bool sendLine(const std::string& line) override {
      EnterCriticalSection(&m_cs);
      bool ok=false;
      if(m_ws){ ok=WinHttpWebSocketSend(m_ws,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,(PVOID)line.data(),(DWORD)line.size())==NO_ERROR; }
      LeaveCriticalSection(&m_cs);
      return ok;
   }
   bool connected() const override { return m_ws!=nullptr; }
   void close() override {
      InterlockedExchange(&m_stop,1);
      EnterCriticalSection(&m_cs); HINTERNET w=m_ws; LeaveCriticalSection(&m_cs);
      if(w) WinHttpWebSocketClose(w,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);      // polite close frame
      if(m_thread){
         if(WaitForSingleObject(m_thread,800)==WAIT_TIMEOUT){ if(w) WinHttpCloseHandle(w); WaitForSingleObject(m_thread,1500); }
         CloseHandle(m_thread); m_thread=nullptr;
      }
      EnterCriticalSection(&m_cs);
      if(m_ws){ WinHttpCloseHandle(m_ws); m_ws=nullptr; }
      LeaveCriticalSection(&m_cs);
      if(m_conn){ WinHttpCloseHandle(m_conn); m_conn=nullptr; }
      if(m_sess){ WinHttpCloseHandle(m_sess); m_sess=nullptr; }
   }
private:
   HWND m_hwnd=nullptr; HINTERNET m_sess=nullptr, m_conn=nullptr, m_ws=nullptr; HANDLE m_thread=nullptr;
   volatile LONG m_stop=0; CRITICAL_SECTION m_cs; bool m_secure=true; int m_port=443; std::wstring m_host, m_path;

   static std::wstring widen(const std::string& s){
      if(s.empty()) return L"";
      int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
      std::wstring w((size_t)n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n); return w;
   }
   void fail(const std::string& why){ if(!m_stop) post(m_hwnd,EV_FAILED,why); }
   static DWORD WINAPI proc(LPVOID p){
      WsConn* c=(WsConn*)p;
      c->m_sess=WinHttpOpen(L"Garibaldka/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
      if(!c->m_sess){ c->fail("WinHTTP"); return 0; }
      WinHttpSetTimeouts(c->m_sess,10000,10000,10000,0);                  // receiving may wait forever (the game sends keep-alives)
      c->m_conn=WinHttpConnect(c->m_sess,c->m_host.c_str(),(INTERNET_PORT)c->m_port,0);
      if(!c->m_conn){ c->fail("nie można połączyć z serwerem"); return 0; }
      HINTERNET req=WinHttpOpenRequest(c->m_conn,L"GET",c->m_path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,c->m_secure?WINHTTP_FLAG_SECURE:0);
      if(!req){ c->fail("żądanie"); return 0; }
      WinHttpSetOption(req,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0);
      bool ok=WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,nullptr,0,0,0)&&WinHttpReceiveResponse(req,nullptr);
      if(!ok){ DWORD e=GetLastError(); WinHttpCloseHandle(req); c->fail("brak połączenia z serwerem (błąd "+std::to_string(e)+")"); return 0; }
      HINTERNET ws=WinHttpWebSocketCompleteUpgrade(req,0);
      WinHttpCloseHandle(req);
      if(!ws){ c->fail("serwer nie przyjął połączenia WebSocket"); return 0; }
      EnterCriticalSection(&c->m_cs); c->m_ws=ws; LeaveCriticalSection(&c->m_cs);
      post(c->m_hwnd,EV_CONNECTED,"");
      std::string msg; char buf[4096];
      for(;;){
         DWORD read=0; WINHTTP_WEB_SOCKET_BUFFER_TYPE t;
         if(WinHttpWebSocketReceive(ws,buf,sizeof buf,&read,&t)!=NO_ERROR) break;
         if(t==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
         msg.append(buf,read);
         if(t==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE||t==WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE){
            if(!msg.empty()) post(c->m_hwnd,EV_LINE,msg);
            msg.clear();
         }
      }
      if(!c->m_stop) post(c->m_hwnd,EV_CLOSED,"");
      return 0;
   }
};

// Local IPv4 addresses (to tell the friend where to connect).
inline std::vector<std::string> localAddresses(){
   std::vector<std::string> out; WSADATA w; WSAStartup(MAKEWORD(2,2),&w);
   char name[256]={}; if(gethostname(name,sizeof name)!=0) return out;
   addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; addrinfo* res=nullptr;
   if(getaddrinfo(name,nullptr,&hints,&res)!=0) return out;
   for(addrinfo* a=res;a;a=a->ai_next){
      char ip[64]={}; inet_ntop(AF_INET,&((sockaddr_in*)a->ai_addr)->sin_addr,ip,sizeof ip);
      std::string s=ip; if(s.rfind("127.",0)!=0 && s.rfind("169.254.",0)!=0) out.push_back(s);
   }
   freeaddrinfo(res); return out;
}

inline std::string toUtf8(const std::wstring& w){
   if(w.empty()) return "";
   int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
   std::string s((size_t)n,0); WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),&s[0],n,nullptr,nullptr); return s;
}
inline std::wstring fromUtf8(const std::string& s){
   if(s.empty()) return L"";
   int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
   std::wstring w((size_t)n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n); return w;
}

} // namespace net
