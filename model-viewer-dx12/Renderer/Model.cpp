#include "Model.h"
#include "../ModelImporter.h"
#include "DX12DescriptorHeap.h"
#include <iostream>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <algorithm>

using namespace DirectX;

Model::~Model() {
}

bool Model::Initialize(ID3D12Device* device, ModelImporter* importer, const std::string& modelDir, TDX12DescriptorHeap* descriptorHeap) {
    if (!importer || !device || !descriptorHeap) return false;

    if (!SetupMeshResources(device, importer, descriptorHeap)) return false;
    if (!SetupTextures(device, importer, modelDir, descriptorHeap)) return false;

    return true;
}

bool Model::SetupMeshResources(ID3D12Device* device, ModelImporter* importer, TDX12DescriptorHeap* descriptorHeap) {
    D3D12_HEAP_PROPERTIES heappropUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_HEAP_PROPERTIES heappropDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    int meshIdx = 0;
    for (auto const& [name, vertices] : importer->mesh_vertices) {
        if (importer->mesh_indices.find(name) == importer->mesh_indices.end()) continue;
        const std::vector<unsigned short>& indices = importer->mesh_indices.at(name);

        ModelViewer::MeshDrawInfo meshInfo = {};
        meshInfo.vertexCount = (UINT)vertices.size();
        meshInfo.indexCount = (UINT)indices.size();

        // (1) Output Vertex Buffer (UAV for Skinning)
        D3D12_RESOURCE_DESC resdescOutput = CD3DX12_RESOURCE_DESC::Buffer(vertices.size() * sizeof(ModelViewer::Vertex));
        resdescOutput.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        
        Microsoft::WRL::ComPtr<ID3D12Resource> pOutputVertexBuffer;
        if (FAILED(device->CreateCommittedResource(
            &heappropDefault, D3D12_HEAP_FLAG_NONE, &resdescOutput,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pOutputVertexBuffer)))) return false;
        m_resources.push_back(pOutputVertexBuffer);
        meshInfo.pOutputVertexBuffer = pOutputVertexBuffer.Get();

        // UAV Handle作成 (t0:ComputeShader書き出し用)
        meshInfo.uavGpuHandle = descriptorHeap->AddUAV(device, pOutputVertexBuffer.Get(), (UINT)vertices.size(), sizeof(ModelViewer::Vertex));

        // (2) Index Buffer
        D3D12_RESOURCE_DESC resdescIndex = CD3DX12_RESOURCE_DESC::Buffer(indices.size() * sizeof(unsigned short));
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
        if (FAILED(device->CreateCommittedResource(
            &heappropUpload, D3D12_HEAP_FLAG_NONE, &resdescIndex,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer)))) return false;
        m_resources.push_back(indexBuffer);

        unsigned short* mappedIdx = nullptr;
        indexBuffer->Map(0, nullptr, (void**)&mappedIdx);
        std::copy(indices.begin(), indices.end(), mappedIdx);
        indexBuffer->Unmap(0, nullptr);

        // Views
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = pOutputVertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = (UINT)vertices.size() * sizeof(ModelViewer::Vertex);
        vbv.StrideInBytes = sizeof(ModelViewer::Vertex);
        m_vbViews[name] = vbv;

        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = indexBuffer->GetGPUVirtualAddress();
        ibv.Format = DXGI_FORMAT_R16_UINT;
        ibv.SizeInBytes = (UINT)indices.size() * sizeof(unsigned short);
        m_ibViews[name] = ibv;

        // (3) Input Vertex Buffer (SRV for Skinning CS)
        D3D12_RESOURCE_DESC resdescInput = CD3DX12_RESOURCE_DESC::Buffer(vertices.size() * sizeof(ModelViewer::Vertex));
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferInput;
        if (FAILED(device->CreateCommittedResource(
            &heappropUpload, D3D12_HEAP_FLAG_NONE, &resdescInput,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBufferInput)))) return false;
        m_resources.push_back(vertexBufferInput);
        
        ModelViewer::Vertex* vertMap = nullptr;
        vertexBufferInput->Map(0, nullptr, (void**)&vertMap);
        std::copy(vertices.begin(), vertices.end(), vertMap);
        vertexBufferInput->Unmap(0, nullptr);

        // SRV Handle作成 (t0:ComputeShader読み込み用)
        meshInfo.srvGpuHandle = descriptorHeap->AddSRV(device, vertexBufferInput.Get(), (UINT)vertices.size(), sizeof(ModelViewer::Vertex));

        m_meshDrawInfos.push_back(meshInfo);
        meshIdx++;
    }

    return true;
}

bool Model::SetupTextures(ID3D12Device* device, ModelImporter* importer, const std::string& modelDir, TDX12DescriptorHeap* descriptorHeap) {
    if (!importer || !descriptorHeap) return false;

    for (const auto& texName : importer->texture_names) {
        auto tex = std::make_unique<TDX12ShaderResource>();
        tex->Initialize(device, modelDir, texName);
        if (tex->IsValid()) {
            tex->srvGpuHandle = descriptorHeap->AddSRV(device, tex->m_shaderResource, tex->GetResourceFormat());
            m_textures.push_back(std::move(tex));
        } else {
            std::cout << "[Warning] Failed to load texture: " << texName << std::endl;
        }
    }

    for (size_t i = 0; i < m_meshDrawInfos.size(); ++i) {
        int texIdx = importer->mesh_texture_indices[i];
        if (texIdx >= 0 && texIdx < (int)m_textures.size()) {
            m_meshDrawInfos[i].materialTexGpuHandle = m_textures[texIdx]->srvGpuHandle;
        }
    }

    return true;
}

void Model::SetBoneCBV(D3D12_GPU_VIRTUAL_ADDRESS address) {
    for (auto& mesh : m_meshDrawInfos) {
        mesh.cbvGpuHandle = address;
    }
}

void Model::Draw(ID3D12GraphicsCommandList* cmdList) {
    // 描画ロジック (後のフェーズで実装)
}
