
struct Vertex
{
    float3 pos;         float pad0;
    float3 normal;      float pad1;
    float2 uv;          float2 pad2;
    uint4 boneid;
    float4 weight;
};

StructuredBuffer<Vertex> InputVertices : register(t0);
RWStructuredBuffer<Vertex> OutputVertices : register(u0);
cbuffer cbuff0 : register(b0)
{
    matrix WorldMatrix;
    matrix BoneMatrices[256];
};


[numthreads(64, 1, 1)] // thread group size is 64 (Wavefront: 64, Warp: 32). if vertex count is 10_000, then 157 thread groups are launched (10_000 / 64)
void main( uint3 DTid : SV_DispatchThreadID )
{
    Vertex v = InputVertices[DTid.x];

    float4 pos = float4(v.pos, 1);

    matrix boneMatrix = (matrix)0;
    float boneAllWeight = 0;
    for (int j = 0; j < 4; j++)
    {
        uint bone = v.boneid[j];
        float w = v.weight[j];

        boneMatrix += BoneMatrices[bone] * w;
        boneAllWeight += w;
    }
    if (boneAllWeight > 0) {
        boneMatrix /= boneAllWeight;
    } else {
        boneMatrix = (matrix)1; // fallback to identity if no weight
    }

    // 頂点と法線のスキンニング
    v.pos = mul(boneMatrix, pos).xyz;
    v.normal = normalize(mul((float3x3)boneMatrix, v.normal));

    OutputVertices[DTid.x] = v;
}