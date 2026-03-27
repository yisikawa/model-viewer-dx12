struct VS_OUT {
	float4 svpos : SV_POSITION;
	float4 normal : NORMAL;
	float2 uv : TEXCOORD;
	float3 view : VIEW;
	uint instanceID : SV_InstanceID;
	float4 tpos : TPOS;
};

Texture2D<float4> tex : register(t0);
Texture2D<float4> materialTex : register(t1);
Texture2D<float4> depthTex : register(t2);
SamplerState smp : register(s0);
SamplerState materialSampler : register(s1);
SamplerComparisonState shadowSmp : register(s2);

cbuffer cbuff0 : register(b0) {
	matrix world;
	matrix bones[256];
};
cbuffer cbuff1 : register(b1) {
	matrix view;
	matrix proj;
	matrix lightViewProj;
	matrix shadow;
	float3 eye;
	float pad_scene0;
	float3 lightDirection;
	float pad_scene1;
};

cbuffer Material : register(b1) {
	float4 ambient;
	float4 diffuse;
	float4 specular;
	float alpha;
};

float4 ShadowVS(in float4 pos: POSITION, in float4 normal : NORMAL, in float2 uv : TEXCOORD, in min16uint2 boneid : BONEID, in float2 weight : WEIGHT) : SV_POSITION{
    pos = mul(world, pos);
    return mul(lightViewProj, pos);
}

VS_OUT MainVS(in float4 pos : POSITION, in float3 normal : NORMAL, in float2 uv : TEXCOORD, in min16uint2 boneid : BONEID, in float2 weight : WEIGHT, in uint instanceID : SV_InstanceID)
{
    VS_OUT output;
    pos = mul(world, pos);
    if (instanceID == 1)
    {
        pos = mul(shadow, pos);
    }
    output.svpos = mul(proj, mul(view, pos));
    output.normal = mul(world, float4(normal, 0.0));
    output.uv = uv;
    output.view = normalize(pos.xyz - eye);
    output.instanceID = instanceID;
    output.tpos = mul(lightViewProj, pos);
    return output;
}

float4 MainPS(in VS_OUT input) : SV_TARGET
{
    if (input.instanceID == 1)
    {
        return float4(0, 0, 0, 1.0);
    }
    float3 L = normalize(lightDirection);
    float3 N = normalize(input.normal.xyz);
    float3 R = reflect(-L, N);
    float Specular = pow(saturate(dot(R, -input.view)), 20);
    float NdotL = dot(N, L);
    float4 texColor = materialTex.Sample(materialSampler, input.uv);

    float3 posFromLightVP = input.tpos.xyz / input.tpos.w;
    float2 shadowUV = (posFromLightVP.xy + float2(1, -1)) * float2(0.5, -0.5);
    float bias = max(0.001 * (1.0 - dot(N, L)), 0.0001);
    float depthFromLight = depthTex.SampleCmp(shadowSmp, shadowUV, posFromLightVP.z - bias);
    float shadowWeight = lerp(0.5f, 1.0f, depthFromLight);
    
    float4 finalColor = float4(NdotL, NdotL, NdotL, 1.0) * texColor;
    finalColor.rgb += Specular;
    finalColor.rgb *= shadowWeight;
    finalColor.a = texColor.a;
    
    // Alpha test for whiskers and fine details
    if(finalColor.a < 0.1) discard;

    return finalColor;
}