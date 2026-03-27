// Application implementation
#include "Application.h"
#include "WindowManager.h"
#include "Renderer/DX12GraphicsDevice.h"
#include "ModelImporter.h"
#include "Renderer/Model.h"
#include "Renderer/Shader.h"
#include "Renderer/DX12RootSignature.h"
#include "Renderer/DX12DescriptorHeap.h"
#include "Renderer/DX12ConstantBuffer.h"
#include "Renderer/DX12ShaderResource.h"
#include <shobjidl.h>
#include <iostream>
#include <algorithm>
#include <iterator>

using namespace ModelViewer;
using namespace DirectX;

static constexpr int APP_NUM_FRAMES_IN_FLIGHT = 2;
std::unique_ptr<TDX12DescriptorHeap> Application::g_resourceDescriptorHeapWrapper = nullptr;

// Constructor/Destructor
Application::Application() : windowManager(nullptr), _graphicsDevice(nullptr) {}
Application::~Application() {
	Terminate();
}

// Debug output to console
void DebugOutput(const char* format, ...) {
#ifdef _DEBUG
	va_list valist;
	va_start(valist, format);
	vprintf(format, valist);
	va_end(valist);
#endif // DEBUG
}

void EnableDebugLayer() {
	ID3D12Debug* debugLayer = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
		debugLayer->EnableDebugLayer();
		debugLayer->Release();
	}
}

void Application::CheckError(const char* msg, HRESULT result) {
	if (FAILED(result)) {
		std::cerr << msg << " is BAD!!!!!" << std::endl;
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
			::OutputDebugStringA("FILE NOT FOUND!!!");
		}
		else if (errorBlob != nullptr) {
			std::string errstr;
			errstr.resize(errorBlob->GetBufferSize());
			std::copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), errstr.begin());
			errstr += "\n";
			OutputDebugStringA(errstr.c_str());
		}
		else if (result == HRESULT_FROM_WIN32(E_OUTOFMEMORY)) {
			std::cerr << "Memory leak" << std::endl;
		}
		exit(1);
	}
	else {
		//std::cerr << msg << " is OK!!!" << std::endl;
	}
}

void Application::CreateDepthStencilView() {
	D3D12_RESOURCE_DESC depthResDesc = {};
	depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthResDesc.Width = windowManager->GetWidth();
	depthResDesc.Height = windowManager->GetHeight();
	depthResDesc.DepthOrArraySize = 1; 
	depthResDesc.Format = DXGI_FORMAT_R32_TYPELESS; 
	depthResDesc.SampleDesc.Count = 1;
	depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	depthResDesc.MipLevels = 1;
	depthResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthResDesc.Alignment = 0;

	D3D12_HEAP_PROPERTIES depthHeapProp = {};
	depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;

	CheckError("CreateDepthResource", _graphicsDevice->GetDevice()->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(_depthBuffer.ReleaseAndGetAddressOf())));

	// Create Shadow Map
	depthResDesc.Width = windowManager->GetWidth();
	depthResDesc.Height = windowManager->GetHeight();
	CheckError("CreateDepthResource", _graphicsDevice->GetDevice()->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(_lightDepthBuffer.ReleaseAndGetAddressOf())));

	// Depth Descriptor Heap
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 2; // 0: normal depth, 1: light depth
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	CheckError("CreateDepthDescriptorHeap", _graphicsDevice->GetDevice()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(_dsvHeap.ReleaseAndGetAddressOf())));

	// CreateDepthStencilView
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
	_graphicsDevice->GetDevice()->CreateDepthStencilView(_depthBuffer.Get(), &dsvDesc, dsvHandle);
	dsvHandle.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	_graphicsDevice->GetDevice()->CreateDepthStencilView(_lightDepthBuffer.Get(), &dsvDesc, dsvHandle);

	// CreateDepthSRV
	_depthSRVHandle = g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT);
	_lightDepthSRVHandle = g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _lightDepthBuffer.Get(), DXGI_FORMAT_R32_FLOAT);
}

void Application::CreatePostProcessResourceAndView() {
	auto resDesc = _graphicsDevice->GetCurrentBackBuffer()->GetDesc();
	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	float val[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	D3D12_CLEAR_VALUE clearValue = CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM, val);

	CheckError("CreatePostProcessResource", _graphicsDevice->GetDevice()->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(_postProcessResource.ReleaseAndGetAddressOf())
	));

	// Create RTV/SRV View
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; 

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * 2; // RenderTarget * 2
	_graphicsDevice->GetDevice()->CreateRenderTargetView(_postProcessResource.Get(), &rtvDesc, cpuHandle);

	_postProcessSRVHandle = g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _postProcessResource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
}

bool Application::CreatePipelineState() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BONEID", 0, DXGI_FORMAT_R16G16_UINT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "WEIGHT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	gpipeline.InputLayout.pInputElementDescs = inputLayout;
	gpipeline.InputLayout.NumElements = _countof(inputLayout);

	TShader vs, ps;
	vs.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "MainVS", "vs_5_0");
	ps.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "MainPS", "ps_5_0");
	if (!vs.IsValid() || !ps.IsValid()) {
		std::cout << "Failed to load shader." << std::endl;
		return false;
	}

	gpipeline.pRootSignature = m_rootSignature->GetRootSignaturePointer();
	gpipeline.VS = vs.GetShaderBytecode();
	gpipeline.PS = ps.GetShaderBytecode();

	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	gpipeline.NumRenderTargets = 1;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	gpipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
	gpipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	gpipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	gpipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	gpipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	gpipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	gpipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	gpipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.SampleDesc.Count = 1;

	CheckError("CreateGraphicsPipelineState", _graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(_pipelineState.ReleaseAndGetAddressOf())));
	CreateShadowMapPipelineState(gpipeline);
	return true;
}

void Application::CreateCanvasPipelineState() {
	D3D12_INPUT_ELEMENT_DESC canvasLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	TShader vs, ps;
	vs.Load(L"../model-viewer-dx12/shaders/CanvasShader.hlsl", "MainVS", "vs_5_0");
	ps.Load(L"../model-viewer-dx12/shaders/CanvasShader.hlsl", "MainPS", "ps_5_0");
	if (!vs.IsValid() || !ps.IsValid()) return;

	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; 
	range.BaseShaderRegister = 0; 
	range.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameter.DescriptorTable.NumDescriptorRanges = 1;
	rootParameter.DescriptorTable.pDescriptorRanges = &range;
	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0); 

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 1;
	rsDesc.pParameters = &rootParameter;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &sampler;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
	D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, rsBlob.ReleaseAndGetAddressOf(), &errorBlob);
	_graphicsDevice->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(_canvasRootSignature.ReleaseAndGetAddressOf()));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _canvasRootSignature.Get();
	gpipeline.VS = vs.GetShaderBytecode();
	gpipeline.PS = ps.GetShaderBytecode();
	gpipeline.InputLayout.NumElements = _countof(canvasLayout);
	gpipeline.InputLayout.pInputElementDescs = canvasLayout;
	gpipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.NumRenderTargets = 1; 
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.SampleDesc.Count = 1;
	_graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(_canvasPipelineState.ReleaseAndGetAddressOf()));
}

void Application::CreateShadowMapPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipelineDesc) {
	TShader vs;
	vs.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "ShadowVS", "vs_5_0");
	gpipelineDesc.VS = vs.GetShaderBytecode();
	gpipelineDesc.PS.pShaderBytecode = nullptr;
	gpipelineDesc.PS.BytecodeLength = 0;
	gpipelineDesc.NumRenderTargets = 0;
	gpipelineDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	_graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipelineDesc, IID_PPV_ARGS(_shadowPipelineState.ReleaseAndGetAddressOf()));
}

void Application::CreateCBV() {
	DirectX::XMMATRIX mMatrix = DirectX::XMMatrixIdentity();
	DirectX::XMVECTOR targetPos = DirectX::XMLoadFloat3(&m_cameraTarget);
	DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(
		m_cameraDistance * cosf(m_cameraPitch) * sinf(m_cameraYaw),
		m_cameraDistance * sinf(m_cameraPitch),
		-m_cameraDistance * cosf(m_cameraPitch) * cosf(m_cameraYaw),
		0.0f
	) + targetPos;

	DirectX::XMVECTOR upVec = { 0.0f, 1.0f, 0.0f };
	_vMatrix = DirectX::XMMatrixLookAtLH(eyePos, targetPos, upVec);
	_pMatrix = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, static_cast<float>(windowManager->GetWidth()) / static_cast<float>(windowManager->GetHeight()), 1.0f, 200.0f);

	DirectX::XMVECTOR lightVec = { 1.0f, -1.0f, 1.0f };
	DirectX::XMVECTOR planeVec = { 0.0f, 1.0f, 0.0f, 0.0f };
	auto lightPos = targetPos - DirectX::XMVector3Normalize(lightVec) * DirectX::XMVector3Length(DirectX::XMVectorSubtract(targetPos, eyePos)).m128_f32[0];

	_transformCB = std::make_unique<TDX12ConstantBuffer>(sizeof(TransformMatrices), _graphicsDevice->GetDevice());
	_sceneCB = std::make_unique<TDX12ConstantBuffer>(sizeof(SceneMatrices), _graphicsDevice->GetDevice());
	
	_transformCB->Map((void**)&_mapTransformMatrix);
	_mapTransformMatrix->world = mMatrix;
	for (int i = 0; i < 256; ++i) _mapTransformMatrix->bones[i] = DirectX::XMMatrixIdentity();

	_sceneCB->Map((void**)&_mapSceneMatrix);
	_mapSceneMatrix->view = _vMatrix;
	_mapSceneMatrix->proj = _pMatrix;
	_mapSceneMatrix->lightViewProj = DirectX::XMMatrixLookAtLH(lightPos, targetPos, upVec) * DirectX::XMMatrixOrthographicLH(40, 40, 1.0f, 100.0f); 
	_mapSceneMatrix->eye = DirectX::XMFLOAT3(eyePos.m128_f32[0], eyePos.m128_f32[1], eyePos.m128_f32[2]);
	_mapSceneMatrix->pad_scene0 = 0.0f;
	DirectX::XMVECTOR lightDirVec = DirectX::XMVector3Normalize(DirectX::XMVectorNegate(lightVec));
	DirectX::XMStoreFloat3(&_mapSceneMatrix->lightDirection, lightDirVec);
	_mapSceneMatrix->pad_scene1 = 0.0f;
	_mapSceneMatrix->shadow = DirectX::XMMatrixShadow(planeVec, -lightVec);

	_transformCBVHandle = g_resourceDescriptorHeapWrapper->AddCBV(_graphicsDevice->GetDevice(), _transformCB->m_constantBuffer);
	_sceneCBVHandle = g_resourceDescriptorHeapWrapper->AddCBV(_graphicsDevice->GetDevice(), _sceneCB->m_constantBuffer);
	if (_model) _model->SetBoneCBV(_transformCB->m_constantBuffer->GetGPUVirtualAddress());
}

void Application::SetupImGui() {
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowManager->GetHandle());
	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = _graphicsDevice->GetDevice();
	init_info.CommandQueue = _graphicsDevice->GetCommandQueue();
	init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.SrvDescriptorHeap = g_resourceDescriptorHeapWrapper->Get();
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_resourceDescriptorHeapWrapper->AllocDynamic(out_cpu_handle, out_gpu_handle); };
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_resourceDescriptorHeapWrapper->FreeDynamic(cpu_handle, gpu_handle); };
	ImGui_ImplDX12_Init(&init_info);
}

void Application::DrawImGui() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open...")) OpenFileDialog();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
	ImGui::Begin("Animation Settings");
	ImGui::Checkbox("Use GPU Skinning", &m_useGpuSkinning);
	ImGui::Checkbox("Is Playing", &m_animState.isPlaying);
	ImGui::Checkbox("Show Bind Pose", &m_animState.showBindPose);
	ImGui::SliderFloat("Playing Time", &m_animState.playingTime, 0.f, m_animState.currentAnimDuration);
	if (m_animState.sceneAnimCount > 0) {
		if (ImGui::BeginCombo("Selected Animation", m_animState.animationNames[m_animState.currentAnimIdx].c_str())) {
			for (int i = 0; i < (int)m_animState.animationNames.size(); ++i) {
				if (ImGui::Selectable(m_animState.animationNames[i].c_str(), i == m_animState.currentAnimIdx)) m_animState.currentAnimIdx = i;
			}
			ImGui::EndCombo();
		}
	}

	ImGui::End();
	ImGui::Render();
}

void Application::CleanupImGui() {
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Application::ReleaseModelResources() {
	_graphicsDevice->WaitDrawDone();
	_model.reset();
	if (g_resourceDescriptorHeapWrapper) g_resourceDescriptorHeapWrapper->Reset(3);
}

void Application::OpenFileDialog() {
	IFileOpenDialog* pFileOpen;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
		COMDLG_FILTERSPEC rgSpec[] = { { L"Model Files", L"*.gltf;*.fbx;*.obj;*.glb" }, { L"All Files", L"*.*" } };
		pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
		if (SUCCEEDED(pFileOpen->Show(windowManager->GetHandle()))) {
			IShellItem* pItem;
			if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
				PWSTR pszFilePath;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
					int size = WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
					std::vector<char> path(size);
					WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, path.data(), size, NULL, NULL);
					_pendingModelPath = path.data();
					_shouldReloadModel = true;
					CoTaskMemFree(pszFilePath);
				}
				pItem->Release();
			}
		}
		pFileOpen->Release();
	}
}

bool Application::LoadModel(const std::string& path) {
	ReleaseModelResources(); 
	_modelImporter = std::make_unique<ModelImporter>();
	if (!_modelImporter->CreateModelImporter(path)) return false;
	std::string modelDir = path.substr(0, path.find_last_of("\\/")) + "/";
	_model = std::make_unique<Model>();
	if (!_model->Initialize(_graphicsDevice->GetDevice(), _modelImporter.get(), modelDir, g_resourceDescriptorHeapWrapper.get())) {
		_model.reset();
		return false;
	}
	m_animState = _modelImporter->GetDefaultAnimState();
	CreateCBV();
	SetupComputePass();
	return true;
}

bool Application::Init() {
	windowManager = std::make_unique<TWindowManager>(1280, 720);
#ifdef _DEBUG
	EnableDebugLayer();
#endif
	_graphicsDevice = std::make_unique<DX12GraphicsDevice>();
	if (!_graphicsDevice->Initialize(windowManager->GetHandle(), windowManager->GetWidth(), windowManager->GetHeight())) return false;
	if (g_resourceDescriptorHeapWrapper == nullptr) g_resourceDescriptorHeapWrapper = std::make_unique<TDX12DescriptorHeap>(_graphicsDevice->GetDevice());

	SetupImGui();
	CreateCBV();
	m_rootSignature = std::make_unique<TDX12RootSignature>();
	m_rootSignature->Initialize(_graphicsDevice->GetDevice());

	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	_graphicsDevice->GetDevice()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(_rtvHeap.ReleaseAndGetAddressOf()));

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	UINT rtvDescriptorSize = _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT n = 0; n < 2; n++) {
		_graphicsDevice->GetDevice()->CreateRenderTargetView(_graphicsDevice->GetRenderTarget(n), nullptr, rtvHandle);
		rtvHandle.ptr += rtvDescriptorSize;
	}

	CreatePostProcessResourceAndView();
	CreateDepthStencilView();

	struct CanvasVtx { float pos[3]; float uv[2]; };
	CanvasVtx vertices[] = { {-1.f, 1.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 1.f, 0.f}, {-1.f, -1.f, 0.f, 0.f, 1.f}, {1.f, -1.f, 0.f, 1.f, 1.f} };
	D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
	_graphicsDevice->GetDevice()->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_canvasVertexResource.ReleaseAndGetAddressOf()));
	void* pData = nullptr;
	_canvasVertexResource->Map(0, nullptr, &pData);
	memcpy(pData, vertices, sizeof(vertices));
	_canvasVertexResource->Unmap(0, nullptr);
	_canvasVBV = { _canvasVertexResource->GetGPUVirtualAddress(), (UINT)sizeof(vertices), (UINT)sizeof(CanvasVtx) };

	CreatePipelineState();
	CreateCanvasPipelineState();
	return true;
}

void Application::SetupComputePass() {
	TShader cs; cs.Load(L"../model-viewer-dx12/shaders/SkinningCS.hlsl", "main", "cs_5_0");
	D3D12_DESCRIPTOR_RANGE ranges[] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0} };
	D3D12_ROOT_PARAMETER params[3];
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable = { 1, &ranges[0] };
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable = { 1, &ranges[1] };
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[2].Descriptor = { 0, 0 };
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = { 3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
	ID3DBlob* rsBlob;
	D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, nullptr);
	_graphicsDevice->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(_computeRootSignature.ReleaseAndGetAddressOf()));
	rsBlob->Release();

	D3D12_COMPUTE_PIPELINE_STATE_DESC cpDesc = { _computeRootSignature.Get(), cs.GetShaderBytecode(), 0, {nullptr, 0}, D3D12_PIPELINE_STATE_FLAG_NONE };
	_graphicsDevice->GetDevice()->CreateComputePipelineState(&cpDesc, IID_PPV_ARGS(_computePipelineState.ReleaseAndGetAddressOf()));
}

void Application::Run() {
	ShowWindow(windowManager->GetHandle(), SW_SHOW);
	D3D12_VIEWPORT vp = { 0, 0, (float)windowManager->GetWidth(), (float)windowManager->GetHeight(), 0.f, 1.f };
	D3D12_RECT sr = { 0, 0, (LONG)windowManager->GetWidth(), (LONG)windowManager->GetHeight() };
	MSG msg = {};
	if (_modelImporter) m_animState = _modelImporter->GetDefaultAnimState();
	auto prevTime = std::chrono::high_resolution_clock::now();
	
	while (msg.message != WM_QUIT) {
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg); DispatchMessage(&msg);
			if (msg.message == WM_QUIT) break;
		}

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		if (_shouldReloadModel) { _shouldReloadModel = false; LoadModel(_pendingModelPath); }
		auto currTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<float> dt = currTime - prevTime; prevTime = currTime;
		if (_modelImporter && _mapTransformMatrix) {
			_modelImporter->UpdateBoneMatrices(dt.count(), m_animState);
			std::copy(_modelImporter->boneMatrices, _modelImporter->boneMatrices + 256, _mapTransformMatrix->bones);
		}

		// カメラ回転・平行移動処理
		if (ImGui::IsMouseDown(0) && !ImGui::GetIO().WantCaptureMouse) {
			auto delta = ImGui::GetIO().MouseDelta;
			if (ImGui::GetIO().KeyShift) {
				// 平行移動（パン）
				float sensitivity = 0.05f;
				// ビュー行列から右・上ベクトルを取得（LH)
				DirectX::XMVECTOR right = DirectX::XMVectorSet(_vMatrix.r[0].m128_f32[0], _vMatrix.r[1].m128_f32[0], _vMatrix.r[2].m128_f32[0], 0);
				DirectX::XMVECTOR up = DirectX::XMVectorSet(_vMatrix.r[0].m128_f32[1], _vMatrix.r[1].m128_f32[1], _vMatrix.r[2].m128_f32[1], 0);

				DirectX::XMVECTOR translation = (right * -delta.x + up * delta.y) * sensitivity;
				DirectX::XMVECTOR newTarget = DirectX::XMLoadFloat3(&m_cameraTarget) + translation;
				DirectX::XMStoreFloat3(&m_cameraTarget, newTarget);
			}
			else {
				// 回転処理
				m_cameraYaw -= delta.x * 0.005f;
				m_cameraPitch += delta.y * 0.005f;
				// ジンバルロック防止
				m_cameraPitch = std::clamp(m_cameraPitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
			}
		}

		// モデルスケール操作
		if (!ImGui::GetIO().WantCaptureMouse) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				m_modelScale += wheel * 0.2f;
				m_modelScale = std::clamp(m_modelScale, 0.1f, 40.0f);
			}
		}

		DirectX::XMVECTOR targetPos = DirectX::XMLoadFloat3(&m_cameraTarget);
		DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(
			m_cameraDistance * cosf(m_cameraPitch) * sinf(m_cameraYaw),
			m_cameraDistance * sinf(m_cameraPitch),
			-m_cameraDistance * cosf(m_cameraPitch) * cosf(m_cameraYaw),
			1.0f
		) + targetPos;

		_vMatrix = DirectX::XMMatrixLookAtLH(eyePos, targetPos, DirectX::XMVectorSet(0, 1, 0, 0));

		if (_mapTransformMatrix) _mapTransformMatrix->world = DirectX::XMMatrixScaling(m_modelScale, m_modelScale, m_modelScale);
		_mapSceneMatrix->view = _vMatrix; _mapSceneMatrix->proj = _pMatrix;

		auto cmd = _graphicsDevice->GetCommandList();
		cmd->RSSetViewports(1, &vp); cmd->RSSetScissorRects(1, &sr);
		cmd->SetDescriptorHeaps(1, g_resourceDescriptorHeapWrapper->GetAddressOf());

		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		if (_model) { 
			if (m_useGpuSkinning) {
				_model->ExecuteSkinning(cmd, _computeRootSignature.Get(), _computePipelineState.Get());
				for (const auto& mesh : _model->GetMeshDrawInfos()) {
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(mesh.pOutputVertexBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
				}
				cmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
			}
		}

		auto barrierRT = CD3DX12_RESOURCE_BARRIER::Transition(_postProcessResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &barrierRT);
		
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE postRTV = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
		postRTV.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * 2; 
		cmd->OMSetRenderTargets(1, &postRTV, false, &dsv);
		float clr[] = { 0.2f, 0.2f, 0.2f, 1.0f };
		cmd->ClearRenderTargetView(postRTV, clr, 0, nullptr);
		cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		cmd->SetGraphicsRootSignature(m_rootSignature->GetRootSignaturePointer());
		cmd->SetPipelineState(_pipelineState.Get());
		cmd->SetGraphicsRootDescriptorTable(0, _transformCBVHandle);
		cmd->SetGraphicsRootDescriptorTable(2, _lightDepthSRVHandle);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		if (_model) _model->Draw(cmd);

		auto barrierSRV = CD3DX12_RESOURCE_BARRIER::Transition(_postProcessResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &barrierSRV);

		auto barrierBB_RT = CD3DX12_RESOURCE_BARRIER::Transition(_graphicsDevice->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &barrierBB_RT);
		D3D12_CPU_DESCRIPTOR_HANDLE bbRTV = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
		bbRTV.ptr += _graphicsDevice->GetCurrentBackBufferIndex() * _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		cmd->OMSetRenderTargets(1, &bbRTV, false, &dsv);
		cmd->SetGraphicsRootSignature(_canvasRootSignature.Get());
		cmd->SetPipelineState(_canvasPipelineState.Get());
		cmd->SetGraphicsRootDescriptorTable(0, _postProcessSRVHandle);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		cmd->IASetVertexBuffers(0, 1, &_canvasVBV);
		cmd->DrawInstanced(4, 1, 0, 0);

		DrawImGui();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

		auto barrierBB_Pres = CD3DX12_RESOURCE_BARRIER::Transition(_graphicsDevice->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		cmd->ResourceBarrier(1, &barrierBB_Pres);

		cmd->Close();
		ID3D12CommandList* lists[] = { cmd };
		_graphicsDevice->GetCommandQueue()->ExecuteCommandLists(1, lists);
		_graphicsDevice->WaitDrawDone();
		_graphicsDevice->GetCommandAllocator()->Reset();
		_graphicsDevice->GetCommandList()->Reset(_graphicsDevice->GetCommandAllocator(), nullptr);
		_graphicsDevice->GetSwapChain()->Present(2, 0);
	}
}

void Application::Terminate() {
	static bool isTerminated = false;
	if (isTerminated) return;
	isTerminated = true;

	CleanupImGui();

	if (_graphicsDevice) {
		_graphicsDevice->WaitDrawDone();
	}

	// Release resources in correct order (Resources -> Device)
	_model.reset();
	_modelImporter.reset();

	_transformCB.reset();
	_sceneCB.reset();

	m_rootSignature.reset();
	_canvasRootSignature.Reset();
	_computeRootSignature.Reset();

	_pipelineState.Reset();
	_shadowPipelineState.Reset();
	_canvasPipelineState.Reset();
	_computePipelineState.Reset();

	_depthBuffer.Reset();
	_lightDepthBuffer.Reset();
	_postProcessResource.Reset();
	_canvasVertexResource.Reset();
	_rtvHeap.Reset();
	_dsvHeap.Reset();

	errorBlob.Reset();

	// Very important: Reset static descriptor heap wrapper
	g_resourceDescriptorHeapWrapper.reset();

	windowManager.reset();

	// Release device last
	_graphicsDevice.reset();
}
