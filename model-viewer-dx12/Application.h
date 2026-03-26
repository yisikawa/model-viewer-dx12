#pragma once

#include "Common.h"
#include "Types.h"
#include "ModelImporter.h"
#include "WindowManager.h"
#include "Renderer/DX12GraphicsDevice.h"
#include "Renderer/DX12DescriptorHeap.h"
#include "Renderer/DX12ConstantBuffer.h"
#include "Renderer/DX12RootSignature.h"
#include "Renderer/Model.h"
#include <memory>

using namespace DirectX;

class Application {
private:
	// private member
	// Windows
	ID3DBlob* errorBlob = nullptr;
	XMMATRIX _vMatrix = XMMatrixIdentity();
	XMMATRIX _pMatrix = XMMatrixIdentity();
	ModelViewer::TransformMatrices* _mapTransformMatrix = nullptr;
	ModelViewer::SceneMatrices* _mapSceneMatrix = nullptr;
	std::unique_ptr<class TDX12ConstantBuffer> _transformCB;
	std::unique_ptr<class TDX12ConstantBuffer> _sceneCB;

	// DX12
	std::unique_ptr<DX12GraphicsDevice> _graphicsDevice;


	// Pipeline State
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _shadowPipelineState = nullptr;

	std::unique_ptr<class TDX12RootSignature> m_rootSignature;

	// Pipeline settings of pass for Post Process
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _canvasRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _canvasPipelineState = nullptr;
	// Pipeline settings of pass for Compute (Skinning for now)
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _computeRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _computePipelineState = nullptr;

	static std::unique_ptr<TDX12DescriptorHeap> g_resourceDescriptorHeapWrapper; // Descriptor Heap Wrapper for CBV, SRV, UAV
	Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap = nullptr;

	// Model resource management (migrated to Model class)
	std::unique_ptr<Model> _model;

	// for Post Process Additional Path
	Microsoft::WRL::ComPtr<ID3D12Resource> _postProcessResource = nullptr;

	// for shadow map
	Microsoft::WRL::ComPtr<ID3D12Resource> _lightDepthBuffer = nullptr; // shadow map

	// Vertex (migrated to Model class)
	D3D12_VERTEX_BUFFER_VIEW _canvasVBV = {}; // for post process canvas

	std::unique_ptr<ModelImporter> _modelImporter;
	std::unique_ptr<TWindowManager> windowManager;

	void CreateDepthStencilView();
	bool CreatePipelineState();
	void CreateCanvasPipelineState();
	void CreateShadowMapPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipelineDesc);
	void CreateCBV();
	void CreatePostProcessResourceAndView();

	bool LoadModel(const std::string& path);
	void ReleaseModelResources();

	void SetVerticesInfo();
	void SetupComputePass();
	void SetupModelResources(); // モデルごとのバッファ作成を分離

	// ImGui
	void SetupImGui();
	void DrawImGui(bool &useGpuSkinning, ModelViewer::AnimState& animState);
	void CleanupImGui();

	// ファイル選択
	void OpenFileDialog();
	std::string _pendingModelPath = "";
	bool _shouldReloadModel = false;

	// Singleton: private constructor
	// not allow to copy but allow to move
	Application() {};
	Application(const Application&) = delete;
	void operator=(const Application&) = delete;

	// Util
	void CheckError(const char* msg, HRESULT result = S_OK); // UE4の参考にもっと良くする

public:
	static Application& GetInstance() {
		static Application instance; // Guaranteed to be destroyed.
									 // Instantiated on first use.
		return instance;
	};
	bool Init();
	void Run();
	void Terminate();
	~Application() {};
};

