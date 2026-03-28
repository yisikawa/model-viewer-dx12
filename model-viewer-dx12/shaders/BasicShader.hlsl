struct VS_OUT {
	float4 svpos : SV_POSITION;
	float4 normal : NORMAL;
	float2 uv : TEXCOORD;
	float3 view : VIEW;
	uint instanceID : SV_InstanceID;
	float4 tpos : TPOS;
	float3 worldPos : WORLDPOS;
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
	float specularStrength;
	float3 lightDirection;
	uint useFlatShading;
};

cbuffer Material : register(b1) {
	float4 ambient;
	float4 diffuse;
	float4 specular;
	float alpha;
};

float4 ShadowVS(in float4 pos: POSITION, in float4 normal : NORMAL, in float2 uv : TEXCOORD, in float2 pad_uv : TEXCOORD1, in uint4 boneid : BONEID, in float4 weight : WEIGHT) : SV_POSITION{
    pos = mul(world, pos);
    return mul(lightViewProj, pos);
}

VS_OUT MainVS(in float4 pos : POSITION, in float3 normal : NORMAL, in float2 uv : TEXCOORD, in float2 pad_uv : TEXCOORD1, in uint4 boneid : BONEID, in float4 weight : WEIGHT, in uint instanceID : SV_InstanceID)
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
    output.worldPos = pos.xyz;
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
    if (useFlatShading != 0) {
        N = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    }
    float3 R = reflect(-L, N);
    float Specular = pow(saturate(dot(R, -input.view)), 20);
    float NdotL = saturate(dot(N, L));
    float lighting = NdotL * 0.65 + 0.35; // 環境光 0.35 (35%) に変更
    float4 texColor = materialTex.Sample(materialSampler, input.uv);

    float3 posFromLightVP = input.tpos.xyz / input.tpos.w;
    float2 shadowUV = (posFromLightVP.xy + float2(1, -1)) * float2(0.5, -0.5);
    float bias = max(0.001 * (1.0 - dot(N, L)), 0.0001);
    float depthFromLight = depthTex.SampleCmp(shadowSmp, shadowUV, posFromLightVP.z - bias);
    float shadowWeight = lerp(0.5f, 1.0f, depthFromLight);
    
    // ライティングとテクスチャ、自己影の合成
    float4 finalColor = float4(lighting, lighting, lighting, 1.0) * texColor;
    finalColor.rgb += Specular * specularStrength;
    finalColor.rgb *= shadowWeight;
    finalColor.a = texColor.a;
    
    // Alpha test for whiskers and fine details
    if(finalColor.a < 0.1) discard;

    return finalColor;
}