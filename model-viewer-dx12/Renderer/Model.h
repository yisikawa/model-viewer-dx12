#pragma once
#include "../Common.h"
#include "../Types.h"
#include "DX12ShaderResource.h"
#include "DX12ConstantBuffer.h"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <wrl.h>

class Model {
public:
    struct Mesh {
        std::string name;
        D3D12_VERTEX_BUFFER_VIEW vbv;
        D3D12_INDEX_BUFFER_VIEW ibv;
        ModelViewer::MeshDrawInfo drawInfo;
    };

    Model() = default;
    ~Model();

    // モデルリソースの初期化
    bool Initialize(ID3D12Device* device, class ModelImporter* importer, const std::string& modelDir, class TDX12DescriptorHeap* descriptorHeap);

    // スキニング計算の実行 (Compute Shader)
    void ExecuteSkinning(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso);

    // ボーン行列などの定数バッファアドレスをセット
    void SetBoneCBV(D3D12_GPU_VIRTUAL_ADDRESS address);

    // 描画
    void Draw(ID3D12GraphicsCommandList* cmdList);

    // ゲッター
    const std::vector<ModelViewer::MeshDrawInfo>& GetMeshDrawInfos() const { return m_meshDrawInfos; }
    const std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& GetResources() const { return m_resources; }
    const D3D12_VERTEX_BUFFER_VIEW* GetVBV(const std::string& name) const {
        auto it = m_vbViews.find(name);
        return (it != m_vbViews.end()) ? &it->second : nullptr;
    }
    const D3D12_INDEX_BUFFER_VIEW* GetIBV(const std::string& name) const {
        auto it = m_ibViews.find(name);
        return (it != m_ibViews.end()) ? &it->second : nullptr;
    }

private:
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_resources;
    std::vector<std::unique_ptr<TDX12ShaderResource>> m_textures;
    std::vector<ModelViewer::MeshDrawInfo> m_meshDrawInfos;
    std::vector<std::string> m_meshNames; // 各描画情報のメッシュ名
    std::map<std::string, TDX12ShaderResource*> m_textureMap; // 名称(パス) -> リソースのマップ
    std::map<std::string, D3D12_VERTEX_BUFFER_VIEW> m_vbViews;
    std::map<std::string, D3D12_INDEX_BUFFER_VIEW> m_ibViews;

    // ヘルパー
    bool SetupMeshResources(ID3D12Device* device, class ModelImporter* importer, class TDX12DescriptorHeap* descriptorHeap);
    bool SetupTextures(ID3D12Device* device, class ModelImporter* importer, const std::string& modelDir, class TDX12DescriptorHeap* descriptorHeap);
};
