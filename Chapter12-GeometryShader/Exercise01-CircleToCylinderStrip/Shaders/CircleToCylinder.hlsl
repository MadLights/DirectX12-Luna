
// Constant data that varies per material.
cbuffer cbPass : register(b0)
{
    float4x4 gViewProj;
};


struct VertexIn
{
    float3 PosW : POSITION;
    float3 Color : COLOR;
};


struct VertexOut
{
    float3 PosW : POSITION;
    float3 Color : COLOR;
};


struct GeoOut
{
    float4 PosH : SV_Position;
    float3 Color : COLOR;
};


VertexOut VS(const VertexIn input)
{
    const VertexOut result = { input.PosW, input.Color };
    return result;
}


[maxvertexcount(4)]
void GS(const line VertexIn input[2], inout LineStream<GeoOut> lineStream)
{
    GeoOut v[4];
    
    v[0].PosH = mul(float4(input[0].PosW, 1.0f), gViewProj);
    v[0].Color = input[0].Color;
    
    v[1].PosH = mul(float4(input[1].PosW, 1.0f), gViewProj);
    v[1].Color = input[1].Color;
    
    v[2].PosH = float4(input[1].PosW.x, input[1].PosW.y + 1.0f, input[1].PosW.z, 1.0f);
    v[2].PosH = mul(v[2].PosH, gViewProj);
    v[2].Color = input[1].Color;

    v[3].PosH = float4(input[0].PosW.x, input[0].PosW.y + 1.0f, input[0].PosW.z, 1.0f);
    v[3].PosH = mul(v[3].PosH, gViewProj);
    v[3].Color = input[0].Color;
    
    lineStream.Append(v[0]);
    lineStream.Append(v[1]);
    lineStream.Append(v[2]);
    lineStream.Append(v[3]);
}


float4 PS(const GeoOut input) : SV_Target
{
    return float4(input.Color, 1.0f);
}
