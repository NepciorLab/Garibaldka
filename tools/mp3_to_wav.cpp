// Converts an MP3 (or any audio file Windows can decode) to a 16-bit PCM WAV, using Windows Media Foundation.
//   python -m ziglang c++ -std=c++17 -O2 tools/mp3_to_wav.cpp -o build/mp3_to_wav.exe -lmfplat -lmfreadwrite -lmfuuid -lole32
//   build/mp3_to_wav.exe in.mp3 out.wav
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstdio>
#include <vector>
#include <cstdint>

int wmain(int argc,wchar_t** argv){
   if(argc<3){ wprintf(L"usage: mp3_to_wav in.mp3 out.wav\n"); return 1; }
   CoInitializeEx(nullptr,COINIT_MULTITHREADED);
   if(FAILED(MFStartup(MF_VERSION))){ wprintf(L"MFStartup failed\n"); return 2; }
   IMFSourceReader* rd=nullptr;
   if(FAILED(MFCreateSourceReaderFromURL(argv[1],nullptr,&rd))){ wprintf(L"cannot open %ls\n",argv[1]); return 3; }
   rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS,FALSE);
   rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
   IMFMediaType* want=nullptr; MFCreateMediaType(&want);
   want->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
   want->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
   want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
   if(FAILED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,want))){ wprintf(L"cannot set PCM\n"); return 4; }
   want->Release();
   IMFMediaType* got=nullptr; rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,&got);
   UINT32 ch=0,rate=0,bits=0; got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch); got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate); got->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits);
   got->Release();
   std::vector<uint8_t> pcm;
   for(;;){
      DWORD flags=0; IMFSample* smp=nullptr;
      if(FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&smp))) break;
      if(flags&MF_SOURCE_READERF_ENDOFSTREAM){ if(smp) smp->Release(); break; }
      if(!smp) continue;
      IMFMediaBuffer* buf=nullptr; smp->ConvertToContiguousBuffer(&buf);
      BYTE* p=nullptr; DWORD len=0; buf->Lock(&p,nullptr,&len); pcm.insert(pcm.end(),p,p+len); buf->Unlock();
      buf->Release(); smp->Release();
   }
   rd->Release(); MFShutdown();
   FILE* f=_wfopen(argv[2],L"wb"); if(!f){ wprintf(L"cannot write\n"); return 5; }
   uint32_t dataLen=(uint32_t)pcm.size(), riff=36+dataLen, fmtLen=16, byteRate=rate*ch*bits/8;
   uint16_t pcmTag=1, chn=(uint16_t)ch, align=(uint16_t)(ch*bits/8), bps=(uint16_t)bits;
   fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&fmtLen,4,1,f);
   fwrite(&pcmTag,2,1,f); fwrite(&chn,2,1,f); fwrite(&rate,4,1,f); fwrite(&byteRate,4,1,f); fwrite(&align,2,1,f); fwrite(&bps,2,1,f);
   fwrite("data",1,4,f); fwrite(&dataLen,4,1,f); fwrite(pcm.data(),1,pcm.size(),f); fclose(f);
   wprintf(L"%u channels, %u Hz, %u bit, %.3f s, %u bytes\n",ch,rate,bits,(double)dataLen/byteRate,dataLen+44);
   return 0;
}
