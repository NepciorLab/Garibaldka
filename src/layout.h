#pragma once
// Minimal layout description shared with renderer_d2d.h (taken from Pasjans Dziadkowy).
// Only the card geometry is needed by the renderer; slot positions are computed in main.cpp.
struct Layout {
   int cardW   = 90;
   int cardH   = 126;
   int cornerR = 7;
   static constexpr float PNG_ASPECT = 420.f / 300.f;   // card PNGs are 300x420
};
