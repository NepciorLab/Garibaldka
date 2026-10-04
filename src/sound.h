#pragma once
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// Sound slot keys - 6 configurable actions (Garibaldka)
static const char* SOUND_KEYS[] = {
   "nowa",    // 0 Nowa gra
   "click",   // 1 Ruch karty
   "sukces",  // 2 Wygrana
   "koniec",  // 3 Przegrana / remis
   "nono",    // 4 Ruch niemozliwy / naruszenie obowiazku
   "podp",    // 5 Podpowiedz
   "cofnij",  // 6 Cofniecie ruchu
};
static const int SOUND_COUNT = 7;
static const wchar_t* SOUND_LABELS[] = {
   L"Nowa gra",
   L"Ruch karty",
   L"Wygrana",
   L"Przegrana lub remis",
   L"Ruch niemożliwy",
   L"Podpowiedź",
   L"Cofnięcie ruchu",
};
static const wchar_t* SOUND_DEFAULTS[] = {
   L"nowa.wav",
   L"click.wav",
   L"sukces.wav",
   L"koniec.wav",
   L"nono.wav",
   L"podpowiedz.wav",
   L"cofnij.wav",
};

// WAV file header parsing
struct WavFormat {
   WORD  channels;
   DWORD sampleRate;
   WORD  bitsPerSample;
   std::vector<BYTE> pcmData;
   bool valid = false;
};

static WavFormat parseWav(const std::vector<BYTE>& buf) {
   WavFormat f;
   if(buf.size() < 44) return f;
   // RIFF header
   if(memcmp(buf.data(), "RIFF", 4) != 0) return f;
   if(memcmp(buf.data()+8, "WAVE", 4) != 0) return f;
   // Find fmt and data chunks
   size_t i = 12;
   WORD channels = 0, bps = 0;
   DWORD sr = 0;
   size_t dataOff = 0, dataSize = 0;
   while(i + 8 <= buf.size()) {
      char id[4]; memcpy(id, buf.data()+i, 4);
      DWORD sz = *(DWORD*)(buf.data()+i+4);
      i += 8;
      if(memcmp(id,"fmt ",4)==0 && sz>=16) {
         WORD fmt = *(WORD*)(buf.data()+i);
         if(fmt != 1) return f; // only PCM
         channels = *(WORD*)(buf.data()+i+2);
         sr       = *(DWORD*)(buf.data()+i+4);
         bps      = *(WORD*)(buf.data()+i+14);
      } else if(memcmp(id,"data",4)==0) {
         dataOff  = i;
         dataSize = sz;
      }
      i += (sz + 1) & ~1u; // word-align
   }
   if(!sr || !channels || !bps || !dataSize) return f;
   if(dataOff + dataSize > buf.size()) dataSize = buf.size() - dataOff;
   f.channels    = channels;
   f.sampleRate  = sr;
   f.bitsPerSample = bps;
   f.pcmData.assign(buf.data()+dataOff, buf.data()+dataOff+dataSize);
   f.valid = true;
   return f;
}

// Convert a linear volume fraction (0.0 = silence .. 1.0 = full volume) into
// DirectSound's logarithmic volume units (hundredths of a decibel, 0 = full,
// DSBVOLUME_MIN = silence). Using log10 here (instead of a naive linear
// interpolation between DSBVOLUME_MIN and 0) matches how the ear perceives
// loudness; a plain linear interpolation squeezes almost all audible volume
// into the top ~30-40% of the slider and leaves the rest sounding silent.
static LONG volFracToDs(float frac) {
   if(frac >= 1.f) return 0;
   if(frac <= 0.f) return DSBVOLUME_MIN;
   LONG v = (LONG)(2000.0 * log10((double)frac));
   if(v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
   if(v > 0) v = 0;
   return v;
}

// One DirectSound secondary buffer per sound slot.
// Playing duplicates the buffer interface so multiple overlapping plays work.
struct SoundSlot {
   IDirectSoundBuffer* primary = nullptr; // template buffer
   WavFormat           fmt;
   std::wstring        customPath;
   bool                muted = false;
};

class SoundSystem {
public:
   static SoundSystem& instance() {
      static SoundSystem s; return s;
   }

   void init(HWND hwnd) {
      m_hwnd = hwnd;
      wchar_t exePath[MAX_PATH];
      GetModuleFileNameW(nullptr, exePath, MAX_PATH);
      wchar_t* last = wcsrchr(exePath, L'\\');
      m_exeDir = last ? std::wstring(exePath, last+1) : L".\\";

      // Create DirectSound device
      if(FAILED(DirectSoundCreate8(nullptr, &m_ds, nullptr))) return;
      m_ds->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);

      for(int i = 0; i < SOUND_COUNT; i++)
         loadDefault(i);
   }

   void setCustomPath(int idx, const std::wstring& path) {
      if(idx < 0 || idx >= SOUND_COUNT) return;
      m_slots[idx].customPath = path;
      releaseSlot(idx);
      if(!path.empty())
         loadFile(idx, path);
      else
         loadDefault(idx); // restore default when cleared
   }

   const std::wstring& customPath(int idx) const {
      static std::wstring empty;
      if(idx < 0 || idx >= SOUND_COUNT) return empty;
      return m_slots[idx].customPath;
   }

   void setMuted(int idx, bool m) {
      if(idx < 0 || idx >= SOUND_COUNT) return;
      m_slots[idx].muted = m;
   }
   bool isMuted(int idx) const {
      if(idx < 0 || idx >= SOUND_COUNT) return false;
      return m_slots[idx].muted;
   }

   void play(const char* key, float volume = 1.0f) {      if(volume < 0.01f) return;
      for(int i = 0; i < SOUND_COUNT; i++)
         if(strcmp(SOUND_KEYS[i], key) == 0) { playIdx(i, volume); return; }
   }

   void playIdx(int idx, float volume = 1.0f) {
      if(idx < 0 || idx >= SOUND_COUNT || volume < 0.01f) return;
      SoundSlot& s = m_slots[idx];
      if(s.muted) return; // user chose "no sound" for this event

      // If we have a DirectSound buffer (WAV), use it for overlap
      if(s.primary) {
         IDirectSoundBuffer* dup = nullptr;
         if(FAILED(m_ds ? m_ds->DuplicateSoundBuffer(s.primary, &dup) : E_FAIL)) {
            dup = s.primary; dup->AddRef();
         }
         LONG dsVol = volFracToDs(volume);
         dup->SetVolume(dsVol);
         dup->SetCurrentPosition(0);
         dup->Play(0, 0, 0);
         m_dups.push_back(dup);
         purgeDups();
         return;
      }

      // No WAV buffer — try MCI (handles MP3 and other formats).
      // open/play/close for this MCI device type must happen on the SAME
      // thread (splitting them across threads made playback silently fail
      // for some drivers) — so the whole sequence runs in one background
      // thread, fire-and-forget. The alias is reserved and registered HERE,
      // synchronously, before the thread is even started, so it's already
      // visible to fadeOutAll() the instant playIdx() returns — otherwise a
      // fast repeated click could call fadeOutAll() before this thread had
      // even reached "open", so its alias wasn't tracked yet and it never
      // got stopped.
      // IMPORTANT: playback uses "play alias" (no "wait") plus a status-poll
      // loop instead of the blocking "play alias wait". The blocking form
      // ties up this thread inside mciSendStringW for the whole duration,
      // and for this driver that appears to also block it from servicing a
      // "stop" command sent from ANOTHER thread (looks like an STA/apartment
      // stall) — which is exactly why "stop" was silently doing nothing and
      // several previews could pile up playing simultaneously, unstoppable
      // until they finished on their own. Non-blocking play avoids that.
      if(!s.customPath.empty()) {
         static std::atomic<int> mciId{0};
         wchar_t alias[32];
         wsprintfW(alias, L"snd%d", ++mciId);
         addMciAlias(alias);
         struct MciData { std::wstring path; std::wstring alias; float vol; };
         auto* data = new MciData{s.customPath, alias, volume};
         HANDLE h = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto* d = (MciData*)p;
            wchar_t cmd[MAX_PATH+64];
            wsprintfW(cmd, L"open \"%s\" type mpegvideo alias %s",
                      d->path.c_str(), d->alias.c_str());
            if(mciSendStringW(cmd, nullptr, 0, nullptr) == 0) {
               if(SoundSystem::instance().isMciAliasActive(d->alias)) {
                  wchar_t volCmd[64];
                  wsprintfW(volCmd, L"setaudio %s volume to %d", d->alias.c_str(), (int)(d->vol*1000.f));
                  mciSendStringW(volCmd, nullptr, 0, nullptr);
                  wsprintfW(cmd, L"play %s", d->alias.c_str());
                  mciSendStringW(cmd, nullptr, 0, nullptr);
                  // Poll instead of blocking on "wait" — stops promptly as
                  // soon as fadeOutAll() removes us from the active list.
                  wchar_t statusCmd[64], statusBuf[32];
                  wsprintfW(statusCmd, L"status %s mode", d->alias.c_str());
                  for(;;) {
                     Sleep(80);
                     if(!SoundSystem::instance().isMciAliasActive(d->alias)) break;
                     statusBuf[0]=0;
                     if(mciSendStringW(statusCmd, statusBuf, 32, nullptr) != 0) break;
                     if(wcscmp(statusBuf, L"playing") != 0) break; // finished naturally
                  }
                  wsprintfW(cmd, L"stop %s", d->alias.c_str());
                  mciSendStringW(cmd, nullptr, 0, nullptr);
               }
               // else: cancelled (fadeOutAll ran) while we were still opening — skip playback entirely
               wsprintfW(cmd, L"close %s", d->alias.c_str());
               mciSendStringW(cmd, nullptr, 0, nullptr);
            }
            SoundSystem::instance().removeMciAlias(d->alias);
            delete d;
            return 0;
         }, data, 0, nullptr);
         if(h) CloseHandle(h); else { removeMciAlias(alias); delete data; }
      }
   }

   void shutdown() {
      for(auto b : m_dups) b->Release();
      m_dups.clear();
      for(int i = 0; i < SOUND_COUNT; i++) releaseSlot(i);
      if(m_ds) { m_ds->Release(); m_ds = nullptr; }
   }

   void addMciAlias(const std::wstring& alias) {
      EnterCriticalSection(&m_mciLock);
      m_mciAliases.push_back(alias);
      LeaveCriticalSection(&m_mciLock);
   }

   void removeMciAlias(const std::wstring& alias) {
      EnterCriticalSection(&m_mciLock);
      m_mciAliases.erase(std::remove(m_mciAliases.begin(), m_mciAliases.end(), alias), m_mciAliases.end());
      LeaveCriticalSection(&m_mciLock);
   }

   // Checks whether `alias` is still in the "should be playing" list. Used
   // right after MCI's "open" command completes, to catch the case where a
   // fadeOutAll() came in (and cancelled/removed this alias) WHILE the open
   // was still in progress — "stop" issued for a device that isn't open yet
   // is simply ignored by MCI, so without this check that sound would start
   // playing anyway a moment later, fully unstoppable.
   bool isMciAliasActive(const std::wstring& alias) {
      EnterCriticalSection(&m_mciLock);
      bool found = std::find(m_mciAliases.begin(), m_mciAliases.end(), alias) != m_mciAliases.end();
      LeaveCriticalSection(&m_mciLock);
      return found;
   }

   // Fade out all currently playing buffers over the given duration, then stop them.
   void fadeOutAll(int durationMs = 1000) {
      auto* toFade = new std::vector<IDirectSoundBuffer*>();
      purgeDups();
      for(auto b : m_dups) { b->AddRef(); toFade->push_back(b); }
      m_dups.clear();

      // Stop any MCI-based (non-WAV, e.g. MP3) sounds immediately. MCI's
      // volume-ramp command support is unreliable across devices/drivers, so
      // rather than gamble on a smooth fade for these we just cut them —
      // that's still far better than letting them keep playing untouched.
      // Removing them from m_mciAliases here (not just reading a copy) is
      // what lets isMciAliasActive() catch sounds that are still in the
      // middle of opening — "stop" on a not-yet-open device is a no-op, so
      // without clearing the list those would start playing anyway.
      EnterCriticalSection(&m_mciLock);
      auto aliasesCopy = m_mciAliases;
      m_mciAliases.clear();
      LeaveCriticalSection(&m_mciLock);
      for(auto& alias : aliasesCopy) {
         wchar_t cmd[64];
         wsprintfW(cmd, L"stop %s", alias.c_str());
         mciSendStringW(cmd, nullptr, 0, nullptr);
      }

      if(toFade->empty()) { delete toFade; return; }
      struct FadeData {
         std::vector<IDirectSoundBuffer*>* bufs;
         int                                steps;
         int                                stepMs;
      };
      const int stepMs = 25;
      int steps = std::max(1, durationMs / stepMs);
      auto* fd = new FadeData{toFade, steps, stepMs};
      CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
         auto* fd = (FadeData*)p;
         auto* bufs = fd->bufs;
         // Start from each buffer's CURRENT volume (not full volume!) so the
         // sound begins fading immediately from wherever it actually is,
         // instead of jumping up to full volume first and only then fading.
         std::vector<LONG> startVol(bufs->size());
         for(size_t i = 0; i < bufs->size(); i++) {
            LONG v = 0;
            (*bufs)[i]->GetVolume(&v);
            startVol[i] = v;
         }
         for(int step = 0; step < fd->steps; step++) {
            Sleep(fd->stepMs);
            float frac = 1.f - (step + 1) / (float)fd->steps;
            for(size_t i = 0; i < bufs->size(); i++) {
               LONG vol = (LONG)(DSBVOLUME_MIN + (startVol[i] - DSBVOLUME_MIN) * frac);
               (*bufs)[i]->SetVolume(vol);
            }
         }
         for(auto b : *bufs) { b->Stop(); b->Release(); }
         delete bufs;
         delete fd;
         return 0;
      }, fd, 0, nullptr);
   }

   ~SoundSystem() { shutdown(); DeleteCriticalSection(&m_mciLock); }

private:
   SoundSystem() { InitializeCriticalSection(&m_mciLock); }
   SoundSystem(const SoundSystem&) = delete;

   HWND                m_hwnd = nullptr;
   IDirectSound8*      m_ds   = nullptr;
   std::wstring        m_exeDir;
   SoundSlot           m_slots[SOUND_COUNT];
   std::vector<IDirectSoundBuffer*> m_dups;
   std::vector<std::wstring> m_mciAliases; // currently-playing MCI (non-WAV) sounds
   CRITICAL_SECTION    m_mciLock = {};

   // Fade out all currently playing buffers over ~1 second, then stop them.
   // Called on new game start so the win sound doesn't continue playing.

   void purgeDups() {
      m_dups.erase(
         std::remove_if(m_dups.begin(), m_dups.end(), [](IDirectSoundBuffer* b){
            DWORD status = 0;
            b->GetStatus(&status);
            if(!(status & DSBSTATUS_PLAYING)) { b->Release(); return true; }
            return false;
         }), m_dups.end());
   }

   void releaseSlot(int idx) {
      if(m_slots[idx].primary) {
         m_slots[idx].primary->Release();
         m_slots[idx].primary = nullptr;
      }
   }

   void loadDefault(int idx) {
      // Load default only when no custom path is set
      if(!m_slots[idx].customPath.empty()) return;
      // A .wav in the "Sounds" subfolder next to the exe overrides the built-in
      // one; otherwise fall back to the copy embedded in the executable
      // (resource SND_<idx>, app.rc). The subfolder is created on first run
      // (harmless no-op afterwards) so there's always somewhere obvious to
      // drop a replacement file.
      CreateDirectoryW((m_exeDir + L"Sounds").c_str(), nullptr);
      std::wstring path = m_exeDir + L"Sounds\\" + SOUND_DEFAULTS[idx];
      DWORD attr = GetFileAttributesW(path.c_str());
      if(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
         loadFile(idx, path);
         if(m_slots[idx].primary) return;
      }
      loadEmbedded(idx);
   }

   // Loads the default sound `idx` from the executable's own resources.
   void loadEmbedded(int idx) {
      if(!m_ds) return;
      wchar_t name[16]; wsprintfW(name, L"SND_%d", idx);
      HRSRC hr = FindResourceW(nullptr, name, RT_RCDATA);
      if(!hr) return;
      HGLOBAL hg = LoadResource(nullptr, hr);
      DWORD sz = SizeofResource(nullptr, hr);
      const BYTE* p = hg ? (const BYTE*)LockResource(hg) : nullptr;
      if(!p || !sz) return;
      releaseSlot(idx);
      loadBuffer(idx, std::vector<BYTE>(p, p + sz));
   }

   void loadFile(int idx, const std::wstring& path) {
      if(!m_ds) return;
      releaseSlot(idx);

      // Read file
      HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
      if(hf == INVALID_HANDLE_VALUE) return;
      DWORD sz = GetFileSize(hf, nullptr);
      if(!sz || sz == INVALID_FILE_SIZE) { CloseHandle(hf); return; }
      std::vector<BYTE> buf(sz);
      DWORD rd = 0; ReadFile(hf, buf.data(), sz, &rd, nullptr);
      CloseHandle(hf);
      if(rd != sz) return;
      loadBuffer(idx, buf);
   }

   void loadBuffer(int idx, const std::vector<BYTE>& buf) {
      // Check if it's a WAV
      if(buf.size() >= 4 && memcmp(buf.data(), "RIFF", 4) == 0) {
         WavFormat fmt = parseWav(buf);
         if(!fmt.valid) return;
         createBuffer(idx, fmt);
      }
      // MP3 and other formats: not supported by DirectSound directly
      // Fall back to thread+PlaySound for non-WAV
      // (store path for MCI fallback)
   }

   void createBuffer(int idx, const WavFormat& fmt) {
      if(!m_ds || fmt.pcmData.empty()) return;

      WAVEFORMATEX wfx = {};
      wfx.wFormatTag      = WAVE_FORMAT_PCM;
      wfx.nChannels       = fmt.channels;
      wfx.nSamplesPerSec  = fmt.sampleRate;
      wfx.wBitsPerSample  = fmt.bitsPerSample;
      wfx.nBlockAlign     = fmt.channels * fmt.bitsPerSample / 8;
      wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

      DSBUFFERDESC desc = {};
      desc.dwSize        = sizeof(desc);
      desc.dwFlags       = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN |
                           DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
      desc.dwBufferBytes = (DWORD)fmt.pcmData.size();
      desc.lpwfxFormat   = &wfx;

      IDirectSoundBuffer* buf8 = nullptr;
      if(FAILED(m_ds->CreateSoundBuffer(&desc, &buf8, nullptr))) return;

      // Fill with PCM data
      void* p1 = nullptr; DWORD b1 = 0;
      void* p2 = nullptr; DWORD b2 = 0;
      if(SUCCEEDED(buf8->Lock(0, (DWORD)fmt.pcmData.size(),
                              &p1, &b1, &p2, &b2, 0))) {
         if(p1) memcpy(p1, fmt.pcmData.data(), std::min((size_t)b1, fmt.pcmData.size()));
         if(p2 && b2) memcpy(p2, fmt.pcmData.data()+b1, std::min((size_t)b2, fmt.pcmData.size()-b1));
         buf8->Unlock(p1, b1, p2, b2);
      }
      m_slots[idx].primary = buf8;
      m_slots[idx].fmt     = fmt;
   }
};

// Defined in main.cpp - true while all sounds are muted by the user.
bool isSoundMuted();
inline void playSound(const char* key, float vol = 1.0f) {
   if(isSoundMuted()) return;
   SoundSystem::instance().play(key, vol);
}
