#pragma once

#include <string>
#include <vector>
#include <memory>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <chrono>

#include "Types.h"
#include "Renderer/DX12GraphicsDevice.h"

// Forward declarations
class TWindowManager;
class TDX12DescriptorHeap;
class TDX12RootSignature;
class TDX12ConstantBuffer;
class ModelImporter;
class Model;

namespace ModelViewer {

	class Application {
	private:
		// Singleton: private constructor
		Application();
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

	public:
		static Application& GetInstance() {
			static Application instance;
			return instance;
		}

		~Application();

		bool Init();
		void Run();
		void Terminate();

		bool LoadModel(const std::string& path);

		// Shared descriptor heap wrapper
		static std::unique_ptr<TDX12DescriptorHeap> g_resourceDescriptorHeapWrapper;

	private:
		void CheckError(const char* msg, HRESULT result = S_OK);
		bool CreatePipelineState();
		void CreateCanvasPipelineState();
		void CreateShadowMapPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipelineDesc);
		void CreateCBV();
		void CreateDepthStencilView();
		void CreatePostProcessResourceAndView();
		void OpenFileDialog();
		void ReleaseModelResources();
		
		void SetupImGui();
		void DrawImGui();
		void CleanupImGui();

		void SetupComputePass();

		std::unique_ptr<TWindowManager> windowManager;
		std::unique_ptr<DX12GraphicsDevice> _graphicsDevice;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvHeap;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> _canvasRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _canvasPipelineState;
		Microsoft::WRL::ComPtr<ID3D12Resource> _canvasVertexResource;
		D3D12_VERTEX_BUFFER_VIEW _canvasVBV;

		Microsoft::WRL::ComPtr<ID3D12Resource> _postProcessResource;
		D3D12_GPU_DESCRIPTOR_HANDLE _postProcessSRVHandle;

		Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> _lightDepthBuffer;
		D3D12_GPU_DESCRIPTOR_HANDLE _depthSRVHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE _lightDepthSRVHandle;

		std::unique_ptr<TDX12RootSignature> m_rootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframePipelineState;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _shadowPipelineState;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> _computeRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _computePipelineState;

		std::unique_ptr<TDX12ConstantBuffer> _transformCB;
		std::unique_ptr<TDX12ConstantBuffer> _sceneCB;
		D3D12_GPU_DESCRIPTOR_HANDLE _transformCBVHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE _sceneCBVHandle;

		TransformMatrices* _mapTransformMatrix = nullptr;
		SceneMatrices* _mapSceneMatrix = nullptr;

		DirectX::XMMATRIX _vMatrix;
		DirectX::XMMATRIX _pMatrix;

		std::unique_ptr<ModelImporter> _modelImporter;
		std::unique_ptr<Model> _model;

		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

		bool _shouldReloadModel = false;
		std::string _pendingModelPath;
		float m_modelScale = 1.0f;

		ModelViewer::AnimState m_animState;
		bool m_useGpuSkinning = true;

		float m_cameraYaw = 0.0f;
		float m_cameraPitch = 0.08f; // 約 13.0f, -30.0f の初期値に合わせる
		float m_cameraDistance = 30.1f;
		DirectX::XMFLOAT3 m_cameraTarget = { 0.0f, 10.5f, 0.0f };
	};

}
