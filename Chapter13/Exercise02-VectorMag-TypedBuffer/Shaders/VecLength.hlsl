
Buffer<float3> gInput : register(t0);
RWBuffer<float> gOutput : register(u0);


[numthreads(64, 1, 1)]
void CS(const int3 dtid : SV_DispatchThreadID)
{
    gOutput[dtid.x] = length(gInput[dtid.x]);
}

