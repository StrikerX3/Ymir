#include "test_include.hlsli"

#ifdef __INTELLISENSE__
#define USE_INCLUDE 1
#endif

ByteAddressBuffer bufIn : register(t0);
RWByteAddressBuffer bufOut : register(u0);

[numthreads(16, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint value = bufIn.Load(id.x * 4);
#if USE_INCLUDE
    bufOut.Store(id.x * 4, Transform(value) ^ kXorValue);
#else
    bufOut.Store(id.x * 4, ~value);
#endif
}
