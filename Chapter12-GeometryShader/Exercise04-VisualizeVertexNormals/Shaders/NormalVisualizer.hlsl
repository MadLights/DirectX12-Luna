// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

// Constant data that varies per pass.
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

	// Allow application to change fog parameters once per frame.
	// For example, we may only use fog for certain times of day.
    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
};
    
VertexOut VS(const VertexIn vertex)
{
    VertexOut result;
    result.PosW = mul(float4(vertex.PosL, 1.0f), gWorld).xyz;
    result.NormalW = mul(vertex.NormalL, (float3x3)gWorld);
    
    return result;
}

struct GeoOut
{
    float4 PosH : SV_Position;
};

[maxvertexcount(2)]
void GS(const point VertexOut p[1], inout LineStream<GeoOut> lineStream)
{
    const GeoOut lineStart = { mul(float4(p[0].PosW, 1.0f), gViewProj) };
    lineStream.Append(lineStart);
    
    const float4 endPoint = float4(p[0].PosW + 4.0f * normalize(p[0].NormalW), 1.0f);
    const GeoOut lineEnd = { mul(endPoint, gViewProj) };
    lineStream.Append(lineEnd);
}

float4 PS(const GeoOut fragment) : SV_Target
{
    return float4(0.0f, 0.0f, 0.0, 1.0f);
}
