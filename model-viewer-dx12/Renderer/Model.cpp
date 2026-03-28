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
    if (!device || !importer || !descriptorHeap) return false;

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
        D3D12_RESOURCE_DESC resdescIB = CD3DX12_RESOURCE_DESC::Buffer(indices.size() * sizeof(unsigned short));
        Microsoft::WRL::ComPtr<ID3D12Resource> pIndexBuffer;
        if (FAILED(device->CreateCommittedResource(
            &heappropUpload, D3D12_HEAP_FLAG_NONE, &resdescIB,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pIndexBuffer)))) return false;
        
        unsigned short* idMap = nullptr;
        pIndexBuffer->Map(0, nullptr, (void**)&idMap);
        std::copy(indices.begin(), indices.end(), idMap);
        pIndexBuffer->Unmap(0, nullptr);
        
        m_resources.push_back(pIndexBuffer);
        meshInfo.ibv.BufferLocation = pIndexBuffer->GetGPUVirtualAddress();
        meshInfo.ibv.Format = DXGI_FORMAT_R16_UINT;
        meshInfo.ibv.SizeInBytes = (UINT)(indices.size() * sizeof(unsigned short));
        meshInfo.indexCount = (UINT)indices.size();

        // (3) Input Vertex Buffer (SRV for Skinning)
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
        m_meshNames.push_back(name); // メッシュ名を記録
    }

    return true;
}

bool Model::SetupTextures(ID3D12Device* device, ModelImporter* importer, const std::string& modelDir, TDX12DescriptorHeap* descriptorHeap) {
    if (!device || !importer || !descriptorHeap) return false;

    m_textureMap.clear();
    for (const auto& texName : importer->texture_names) {
        auto tex = std::make_unique<TDX12ShaderResource>();
        tex->Initialize(device, modelDir, texName);
        if (tex->IsValid()) {
            tex->srvGpuHandle = descriptorHeap->AddSRV(device, tex->m_shaderResource, tex->GetResourceFormat());
            m_textureMap[texName] = tex.get();
            m_textures.push_back(std::move(tex));
        } else {
            std::cout << "[Warning] Failed to load texture: " << texName << std::endl;
        }
    }

    for (unsigned int i = 0; i < (unsigned int)m_meshDrawInfos.size(); ++i) {
        if (i >= (unsigned int)m_meshNames.size()) break;
        const std::string& meshName = m_meshNames[i];
        if (importer->mesh_texture_name.count(meshName)) {
            const std::string& texName = importer->mesh_texture_name.at(meshName);
            if (m_textureMap.count(texName)) {
                m_meshDrawInfos[i].materialTexGpuHandle = m_textureMap[texName]->srvGpuHandle;
            }
        }
    }

    return true;
}

void Model::ExecuteSkinning(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso) {
    if (!cmdList || !rootSig || !pso) return;

    cmdList->SetComputeRootSignature(rootSig);
    cmdList->SetPipelineState(pso);

    for (const auto& mesh : m_meshDrawInfos) {
        cmdList->SetComputeRootDescriptorTable(0, mesh.srvGpuHandle); 
        cmdList->SetComputeRootDescriptorTable(1, mesh.uavGpuHandle); 
        cmdList->SetComputeRootConstantBufferView(2, mesh.cbvGpuHandle); 

        UINT threadGroupCount = (mesh.vertexCount + 63) / 64; 
        cmdList->Dispatch(threadGroupCount, 1, 1);
    }
}

void Model::Draw(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList) return;

    for (const auto& mesh : m_meshDrawInfos) {
        cmdList->SetGraphicsRootDescriptorTable(1, mesh.materialTexGpuHandle);
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = mesh.pOutputVertexBuffer->GetGPUVirtualAddress();
        vbv.StrideInBytes = sizeof(ModelViewer::Vertex);
        vbv.SizeInBytes = mesh.vertexCount * sizeof(ModelViewer::Vertex);

        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&mesh.ibv);
        cmdList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
    }
}

void Model::SetBoneCBV(D3D12_GPU_VIRTUAL_ADDRESS address) {
    for (auto& mesh : m_meshDrawInfos) {
        mesh.cbvGpuHandle = address;
    }
}
