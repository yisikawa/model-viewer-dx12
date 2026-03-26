
#pragma warning(disable: 4819)
#pragma warning(disable: 26827)

#include "Application.h"
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

static constexpr int APP_NUM_FRAMES_IN_FLIGHT = 2;
std::unique_ptr<TDX12DescriptorHeap> Application::g_resourceDescriptorHeapWrapper = nullptr;

// @brief	コンソールにフォーマット付き文字列を表示
// @param	format フォーマット %d or %f etc
// @param	可変長引数
// @remarks	for debug
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

void Application::CheckError(LPCSTR msg, HRESULT result) {
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
		std::cerr << msg << " is OK!!!" << std::endl;
	}
}


void Application::CreateDepthStencilView() {
	//深度バッファの仕様
	D3D12_RESOURCE_DESC depthResDesc = {};
	depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthResDesc.Width = windowManager->GetWidth();
	depthResDesc.Height = windowManager->GetHeight();
	depthResDesc.DepthOrArraySize = 1; // テクスチャ配列でもないし3Dテクスチャでもない
	// depthResDesc.Format = DXGI_FORMAT_D32_FLOAT;//深度値書き込み用フォーマット
	depthResDesc.Format = DXGI_FORMAT_R32_TYPELESS; // バッファのビット数は32だけど扱い方はView側が決めてよい
	depthResDesc.SampleDesc.Count = 1;// サンプルは1ピクセル当たり1つ
	depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;//このバッファは深度ステンシル
	depthResDesc.MipLevels = 1;
	depthResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthResDesc.Alignment = 0;

	D3D12_HEAP_PROPERTIES depthHeapProp = {};
	depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;//32bit深度値としてクリア

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
	// DSV and RTV, Flags is not visible from shader.
	// Descriptor heap for DSV
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
	g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT);
	g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _lightDepthBuffer.Get(), DXGI_FORMAT_R32_FLOAT);
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
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // is it saying draw pixels as 2d texture?

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * 2; // RenderTarget * 2
	_graphicsDevice->GetDevice()->CreateRenderTargetView(_postProcessResource.Get(), &rtvDesc, cpuHandle);

	g_resourceDescriptorHeapWrapper->AddSRV(_graphicsDevice->GetDevice(), _postProcessResource.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	// MEMO: _postProcessResource is used both as Render Target and Shader Resource. 
	// Through _postProcessRTVHeap, write draw output of first pass to _postProcessResource. After that, through _postProcessSRVHeap, use it as texture for post processing.
}

bool Application::CreatePipelineState() {

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};

	{ // 1. Input layout of shader
		D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
			{
				"POSITION",
				0,
				DXGI_FORMAT_R32G32B32_FLOAT,
				0,
				0, // D3D12_APPEND_ALIGNED_ELEMENT is also ok.
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				0
			},
			{
				"NORMAL",
				0,
				DXGI_FORMAT_R32G32B32_FLOAT,
				0,
				16, // (R32G32B32 = 4byte * 3)
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				0
			},
			{
				"TEXCOORD",
				0,
				DXGI_FORMAT_R32G32_FLOAT,
				0,
				32,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				0
			},
			{
				"BONEID",
				0,
				DXGI_FORMAT_R16G16_UINT,
				0,
				40,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			},
			{
				"WEIGHT",
				0,
				DXGI_FORMAT_R32G32_FLOAT,
				0,
				48,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
			}
		};
		gpipeline.InputLayout.pInputElementDescs = inputLayout;//レイアウト先頭アドレス
		gpipeline.InputLayout.NumElements = _countof(inputLayout);//レイアウト配列数
	}

	TShader vs, ps;
	{ // 2. Register shaders and their settings
		// Compile shader, using d3dcompiler
		vs.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "MainVS", "vs_5_0");
		ps.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "MainPS", "ps_5_0");
		if (!vs.IsValid() || !ps.IsValid()) {
			std::cout << "Failed to load shader." << std::endl;
			return false;
		}

		gpipeline.pRootSignature = m_rootSignature->GetRootSignaturePointer();
		gpipeline.VS = vs.GetShaderBytecode();
		gpipeline.PS = ps.GetShaderBytecode();

		gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;//中身は0xffffffff
		gpipeline.HS.BytecodeLength = 0;
		gpipeline.HS.pShaderBytecode = nullptr;
		gpipeline.DS.BytecodeLength = 0;
		gpipeline.DS.pShaderBytecode = nullptr;
		gpipeline.GS.BytecodeLength = 0;
		gpipeline.GS.pShaderBytecode = nullptr;

		// ラスタライザの設定
		gpipeline.RasterizerState.MultisampleEnable = false;
		gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		gpipeline.RasterizerState.DepthClipEnable = true;
		//残り
		gpipeline.RasterizerState.FrontCounterClockwise = false;
		gpipeline.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		gpipeline.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		gpipeline.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		gpipeline.RasterizerState.AntialiasedLineEnable = false;
		gpipeline.RasterizerState.ForcedSampleCount = 0;
		gpipeline.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		// OutputMerger部分
		gpipeline.NumRenderTargets = 1;//今は１つのみ
		gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;//0～1に正規化されたRGBA

		//深度ステンシル
		gpipeline.DepthStencilState.DepthEnable = true;
		gpipeline.DepthStencilState.StencilEnable = false;
		gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	}

	{ // 3. Blend settings
		D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
		renderTargetBlendDesc.BlendEnable = true;
		renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // Write to all channels (RGBA)

		// Final Color = SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)
		renderTargetBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		renderTargetBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		renderTargetBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;

		// Final Alpha = SrcAlpha * ONE + DestAlpha * (1 - SrcAlpha)
		renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE; // Source alpha blend factor is ONE
		renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; // Destination alpha blend factor is inverse source alpha
		renderTargetBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD; // Alpha blending operation is addition

		//ひとまず論理演算は使用しない
		renderTargetBlendDesc.LogicOpEnable = false;
		renderTargetBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;

		gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;
		// アルファブレンドON, アルファテストOFFだとa==0の時にもPSが走って無駄。
		// 伝統的にアルファブレンドするときにはアルファテストもする。これは疎の設定。
		// これは、従来のに加えてマルチサンプリング時の網羅率が入るからアンチエイリアス時にきれいになる？？
		gpipeline.BlendState.AlphaToCoverageEnable = false;
		gpipeline.BlendState.IndependentBlendEnable = false;
	}

	gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;//ストリップ時のカットなし
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;//三角形で構成

	// AAについて
	gpipeline.SampleDesc.Count = 1;//サンプリングは1ピクセルにつき１
	gpipeline.SampleDesc.Quality = 0;//クオリティは最低

	CheckError("CreateGraphicsPipelineState", _graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(_pipelineState.ReleaseAndGetAddressOf())));
	CreateShadowMapPipelineState(gpipeline);
	return true;
}
void Application::CreateCanvasPipelineState() {


	D3D12_INPUT_ELEMENT_DESC canvasLayout[] = {
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0, // D3D12_APPEND_ALIGNED_ELEMENT is also ok.
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			12,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
	};
	TShader vs, ps;
	vs.Load(L"../model-viewer-dx12/shaders/CanvasShader.hlsl", "MainVS", "vs_5_0");
	ps.Load(L"../model-viewer-dx12/shaders/CanvasShader.hlsl", "MainPS", "ps_5_0");
	if (!vs.IsValid() || !ps.IsValid()) {
		std::cout << "Failed to load shader." << std::endl;
		return;
	}

	// create root signature
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // texture, register using texture like this (such as t0)
	range.BaseShaderRegister = 0; // t0
	range.NumDescriptors = 1;
	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameter.DescriptorTable.NumDescriptorRanges = 1;
	rootParameter.DescriptorTable.pDescriptorRanges = &range;
	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0); // s0

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 1;
	rsDesc.pParameters = &rootParameter;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &sampler;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
	CheckError("SerializeCanvasRootSignature", D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, rsBlob.ReleaseAndGetAddressOf(), &errorBlob));
	CheckError("CreateCanvasRootSignature", _graphicsDevice->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(_canvasRootSignature.ReleaseAndGetAddressOf())));



	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _canvasRootSignature.Get();
	gpipeline.VS = vs.GetShaderBytecode();
	gpipeline.PS = ps.GetShaderBytecode();

	gpipeline.InputLayout.NumElements = _countof(canvasLayout);
	gpipeline.InputLayout.pInputElementDescs = canvasLayout;


	gpipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.NumRenderTargets = 1; // これでいいのか？このパイプラインはこれ？
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.SampleDesc.Count = 1;
	gpipeline.SampleDesc.Quality = 0;
	gpipeline.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckError("CreateGraphicsPipelineState", _graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(_canvasPipelineState.ReleaseAndGetAddressOf())));
}

void Application::CreateShadowMapPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipelineDesc) {
	TShader vs;
	vs.Load(L"../model-viewer-dx12/shaders/BasicShader.hlsl", "ShadowVS", "vs_5_0");
	gpipelineDesc.VS = vs.GetShaderBytecode();
	gpipelineDesc.PS.pShaderBytecode = nullptr;
	gpipelineDesc.PS.BytecodeLength = 0;
	gpipelineDesc.NumRenderTargets = 0;
	gpipelineDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	CheckError("CreateGraphicsPipelineState", _graphicsDevice->GetDevice()->CreateGraphicsPipelineState(&gpipelineDesc, IID_PPV_ARGS(_shadowPipelineState.ReleaseAndGetAddressOf())));
}

void Application::CreateCBV() {
	// TODO : ここらへんinputから動かせるようにする　分かるように左上にprint
	XMMATRIX mMatrix = XMMatrixIdentity();
	XMVECTOR eyePos = { 0, 13., -30 }; // 視点
	XMVECTOR targetPos = { 0, 10.5, 0 }; // 注視点
	XMVECTOR upVec = { 0, 1, 0 };
	_vMatrix = XMMatrixLookAtLH(eyePos, targetPos, upVec);
	// FOV, aspect ratio, near, far
	_pMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV2, static_cast<float>(windowManager->GetWidth()) / static_cast<float>(windowManager->GetHeight()), 1.0f, 200.0f);

	// shadow matrix
	XMVECTOR lightVec = { 1, -1, 1 };
	XMVECTOR planeVec = { 0, 1, 0, 0 };

	// light pos: 視点と注視点の距離を維持
	auto lightPos = targetPos + XMVector3Normalize(lightVec) * XMVector3Length(XMVectorSubtract(targetPos, eyePos)).m128_f32[0];

	_transformCB = std::make_unique<TDX12ConstantBuffer>(sizeof(TransformMatrices), _graphicsDevice->GetDevice());
	_sceneCB = std::make_unique<TDX12ConstantBuffer>(sizeof(SceneMatrices), _graphicsDevice->GetDevice());
	{
		_transformCB->Map((void**)&_mapTransformMatrix);
		_mapTransformMatrix->world = mMatrix;
		std::vector<XMMATRIX> boneMatrices(256, XMMatrixIdentity());
		std::copy(boneMatrices.begin(), boneMatrices.end(), _mapTransformMatrix->bones);
		// TODO : ここらへん設定しやすいように, Map interfaceを消す
		_sceneCB->Map((void**)&_mapSceneMatrix);
		_mapSceneMatrix->view = _vMatrix;
		_mapSceneMatrix->proj = _pMatrix;
		_mapSceneMatrix->lightViewProj = XMMatrixLookAtLH(lightPos, targetPos, upVec) * XMMatrixOrthographicLH(40, 40, 1.0f, 100.0f); // lightView * lightProj
		// xmmatrixortho: view width, view height, nearz, farz
		_mapSceneMatrix->eye = XMFLOAT3(eyePos.m128_f32[0], eyePos.m128_f32[1], eyePos.m128_f32[2]);
		_mapSceneMatrix->shadow = XMMatrixShadow(planeVec, -lightVec);
	}
	g_resourceDescriptorHeapWrapper->AddCBV(_graphicsDevice->GetDevice(), _transformCB->m_constantBuffer);
	g_resourceDescriptorHeapWrapper->AddCBV(_graphicsDevice->GetDevice(), _sceneCB->m_constantBuffer);
	{ // Send handle data to CBV which each mesh will use (mainly for bone matrices on Compute Pass)
		if (_model) {
			_model->SetBoneCBV(_transformCB->m_constantBuffer->GetGPUVirtualAddress());
		}
	}
}


void Application::SetupImGui() {
	// Code from: https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx12/main.cpp
	
	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(windowManager->GetHandle());

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = _graphicsDevice->GetDevice();
	init_info.CommandQueue = _graphicsDevice->GetCommandQueue();
	init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	// Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
	// (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
	init_info.SrvDescriptorHeap = g_resourceDescriptorHeapWrapper->Get();
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_resourceDescriptorHeapWrapper->AllocDynamic(out_cpu_handle, out_gpu_handle); };
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_resourceDescriptorHeapWrapper->FreeDynamic(cpu_handle, gpu_handle); };
	ImGui_ImplDX12_Init(&init_info);
	
	// ImGuiはこちらのシェーダー側で飼養するわけではないので、RootSignature側で特にシェーダー側での使い方を定義する必要はない。
	// DescriptorHeap上のD3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLEが使用時に取れれば良い。
}

void Application::DrawImGui(bool &useGpuSkinning, ModelViewer::AnimState& animState) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open...")) {
				OpenFileDialog();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

		ImGuiIO& io = ImGui::GetIO();

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		//if (show_demo_window)
			//ImGui::ShowDemoWindow(&show_demo_window);

		{
			ImGui::Begin("Animation Settings");

			ImGui::Checkbox("Use GPU Skinning", &useGpuSkinning);
			ImGui::Checkbox("Is Playing", &animState.isPlaying);
			ImGui::SameLine();
			ImGui::Checkbox("Is Looping", &animState.isLooping);

			ImGui::SliderFloat("Playing Time", &animState.playingTime, 0.f, animState.currentAnimDuration);
			ImGui::SliderFloat("Playing Speed", &animState.playingSpeed, 0.f, 3.f);

			ImGui::Text("Scene Animation Count = %d", animState.sceneAnimCount);
			if (animState.sceneAnimCount > 0) {
				if (ImGui::BeginCombo("Selected Animation", animState.animationNames[animState.currentAnimIdx].c_str())) {
					for (int i = 0; i < animState.animationNames.size(); ++i) {
						bool isSelected = (i == animState.currentAnimIdx);
						if (ImGui::Selectable(animState.animationNames[i].c_str(), isSelected)) {
							animState.currentAnimIdx = i;
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}


			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();
		}


		// Rendering
		ImGui::Render();
}

void Application::CleanupImGui() {
	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Application::ReleaseModelResources() {
	_graphicsDevice->WaitDrawDone();
	_model.reset();
	if (g_resourceDescriptorHeapWrapper) {
		g_resourceDescriptorHeapWrapper->Reset(3);
	}
}

void Application::OpenFileDialog() {
	IFileOpenDialog* pFileOpen;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
	if (SUCCEEDED(hr)) {
		COMDLG_FILTERSPEC rgSpec[] = {
			{ L"Model Files", L"*.gltf;*.fbx;*.obj;*.glb" },
			{ L"All Files", L"*.*" }
		};
		pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
		hr = pFileOpen->Show(windowManager->GetHandle());
		if (SUCCEEDED(hr)) {
			IShellItem* pItem;
			hr = pFileOpen->GetResult(&pItem);
			if (SUCCEEDED(hr)) {
				PWSTR pszFilePath;
				hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
				if (SUCCEEDED(hr)) {
					int size = WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
					std::vector<char> path(size);
					WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, path.data(), size, NULL, NULL);
					_pendingModelPath = path.data();
					_shouldReloadModel = true;
					CoTaskMemFree(pszFilePath);
				}
				else {
					std::cout << "Failed to get display name: HR=" << std::hex << hr << std::endl;
				}
				pItem->Release();
			}
			else {
				std::cout << "Failed to get result: HR=" << std::hex << hr << std::endl;
			}
		}
		else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
			std::cout << "Failed to show dialog: HR=" << std::hex << hr << std::endl;
		}
		pFileOpen->Release();
	}
	else {
		std::cout << "Failed to create FileOpenDialog: HR=" << std::hex << hr << std::endl;
	}
}

bool Application::LoadModel(const std::string& path) {
	std::cout << "[Debug] Application::LoadModel called with path: " << path << std::endl;
	
	ReleaseModelResources(); // Ensure old model data is cleared

	_modelImporter = std::make_unique<ModelImporter>();
	if (!_modelImporter->CreateModelImporter(path)) {
		std::cout << "Failed to load model: " << path << std::endl;
		return false;
	}

	std::string modelDir = path.substr(0, path.find_last_of("\\/")) + "/";

	_model = std::make_unique<Model>();
	if (!_model->Initialize(_graphicsDevice->GetDevice(), _modelImporter.get(), modelDir, g_resourceDescriptorHeapWrapper.get())) {
		std::cout << "Failed to initialize model resources." << std::endl;
		_model.reset();
		return false;
	}

	CreateCBV();
	SetupComputePass();
	return true;
}

bool Application::Init() {
	DebugOutput("Show window test");
	windowManager = std::make_unique<TWindowManager>(1280, 720);
#ifdef _DEBUG
	EnableDebugLayer();
#endif
	_graphicsDevice = std::make_unique<DX12GraphicsDevice>();
	if (!_graphicsDevice->Initialize(windowManager->GetHandle(), windowManager->GetWidth(), windowManager->GetHeight())) {
		return false;
	}

	if (g_resourceDescriptorHeapWrapper == nullptr) {
		g_resourceDescriptorHeapWrapper = std::make_unique<TDX12DescriptorHeap>(_graphicsDevice->GetDevice());
	}

	// Create Fence は _graphicsDevice->Initialize 内で行われます

	// ImGui setup requires Device, CommandQueue, SRV Descriptor Heap.
	SetupImGui();

	// 起動時の自動ロードを削除
	// (以前のコメントアウト箇所を完全に削除)
	std::cout << "[Debug] Application::Init - skipping default model load" << std::endl;

	// 初期ロードがコメントアウトされているため、最低限の定数バッファを作成しておく
	CreateCBV();

	m_rootSignature = std::make_unique<TDX12RootSignature>();
	m_rootSignature->Initialize(_graphicsDevice->GetDevice());

	{ // Descriptor heap for RTV
		D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
		descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descHeapDesc.NumDescriptors = 3; // 表 + 裏 + ポストプロセス用
		CheckError("Create RTV DescriptorHeap", _graphicsDevice->GetDevice()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(_rtvHeap.ReleaseAndGetAddressOf())));
	}
	// SwapChain は _graphicsDevice->Initialize 内で作成されます

	CreatePostProcessResourceAndView();
	CreateDepthStencilView();



	if (!CreatePipelineState()) {
		std::cout << "Failed to create pipeline state." << std::endl;
		return false;
	}
	std::cout << "[Debug] Graphics Pipeline State created" << std::endl;
	CreateCanvasPipelineState();
	std::cout << "[Debug] Application::Init COMPLETED successfully" << std::endl;
	return true;
}

// Application::SetVerticesInfo() has been migrated to Model::Initialize()

void Application::SetupComputePass() {
	// Shader compile -> Create Root signature -> Create compute pipeline -> Create DescHeap -> Create Resources -> Create UAV -> Map

	TShader cs;
	cs.Load(L"../model-viewer-dx12/shaders/SkinningCS.hlsl", "main", "cs_5_0");

	// Root Signature info from CS?

	// TODO: Integrate with TDX12RootSignature::Initialize
	D3D12_DESCRIPTOR_RANGE descRanges[2]; // type is like: D3D12_DESCRIPTOR_RANGE_TYPE RangeType; UINT NumDescriptors; UINT BaseShaderRegister; UINT RegisterSpace; UINT OffsetInDescriptorsFromTableStart;
	descRanges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0/*t0*/, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}; // t0: InputVertices
	descRanges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0/*u0*/, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}; // u0: OutputVertices

	D3D12_ROOT_PARAMETER rootParams[3]; // SRV, UAV, CBV
	// ----- t0 -----
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[0].DescriptorTable.pDescriptorRanges = &descRanges[0];
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	// ----- u0 -----
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &descRanges[1];
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	// ----- b0 -----
	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[2].Descriptor.ShaderRegister = 0; // b0: BoneMatrices
	rootParams[2].Descriptor.RegisterSpace = 0;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// Seriarize and create Root Signature
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = { 3, rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };

	ID3DBlob* rootSignatureBlob = nullptr;
	// Selialize Root Signature?
	ID3DBlob* errorBlob = nullptr;
	CheckError("SerializeComputeRootSignature", D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSignatureBlob, &errorBlob));
	_graphicsDevice->GetDevice()->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(_computeRootSignature.ReleaseAndGetAddressOf()));
	rootSignatureBlob->Release();

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
	computePipelineStateDesc.pRootSignature = _computeRootSignature.Get();
	computePipelineStateDesc.CS = cs.GetShaderBytecode();
	computePipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	_graphicsDevice->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(_computePipelineState.ReleaseAndGetAddressOf()));
}

void Application::Run() {
	ShowWindow(windowManager->GetHandle(), SW_SHOW);//ウィンドウ表示

	D3D12_VIEWPORT viewport = {};
	viewport.Width = (float)windowManager->GetWidth(); // pixel
	viewport.Height = (float)windowManager->GetHeight(); // pixel
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MaxDepth = 1.0f;
	viewport.MinDepth = 0.0f;

	D3D12_RECT scissorrect = {};
	scissorrect.top = 0;//切り抜き上座標
	scissorrect.left = 0;//切り抜き左座標
	scissorrect.right = scissorrect.left + windowManager->GetWidth();//切り抜き右座標
	scissorrect.bottom = scissorrect.top + windowManager->GetHeight();//切り抜き下座標



	//ノイズテクスチャの作成
//struct TexRGBA {
//	unsigned char R, G, B, A;
//};
//std::vector<TexRGBA> texturedata(256 * 256);

//for (auto& rgba : texturedata) {
//	rgba.R = rand() % 256;
//	rgba.G = rand() % 256;
//	rgba.B = rand() % 256;
//	rgba.A = 255;//アルファは1.0という事にします。
//}

	//D3D12_CONSTANT_BUFFER_VIEW_DESC materialCBVDesc;
	//materialCBVDesc.BufferLocation = materialBuff->GetGPUVirtualAddress(); // マップ先を押してる
	//materialCBVDesc.SizeInBytes = (sizeof(material) + 0xff) & ~0xff;
	//_dev->CreateConstantBufferView(&materialCBVDesc, basicHeapHandle);

	if (g_resourceDescriptorHeapWrapper->numResources == 0) {
		std::cout << "[Warning] No resources in descriptor heap." << std::endl;
	}


	MSG msg = {};
	float angle = .0;

	bool useGpuSkinning = true;
	AnimState animState;
	if (_modelImporter) {
		animState = _modelImporter->GetDefaultAnimState();
	}
	else {
		animState.isPlaying = false;
		animState.isLooping = true;
		animState.currentAnimIdx = 0;
		animState.playingTime = 0.f;
		animState.playingSpeed = 1.f;
	}

	std::chrono::steady_clock::time_point previousFrameTime = std::chrono::high_resolution_clock::now();
	// Render系のCmdListとかContextみたいなのにまとめる
	while (true) {
		{ // check if application ends
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			if (msg.message == WM_QUIT) { // WM_QUITになるのは終了直前？
				break;
			}
		}

		if (_shouldReloadModel) {
			_shouldReloadModel = false;
			LoadModel(_pendingModelPath);
		}

		{ // Deltatime
			std::chrono::steady_clock::time_point currentFrameTime = std::chrono::high_resolution_clock::now();
			std::chrono::duration<float> deltaTime = currentFrameTime - previousFrameTime;
			previousFrameTime = currentFrameTime;

			if (_modelImporter && _mapTransformMatrix) {
				// Update bone matrices
				_modelImporter->UpdateBoneMatrices(deltaTime.count(), animState);
				// Upload bone CBV after updating bone matrices
				std::copy(_modelImporter->boneMatrices, _modelImporter->boneMatrices + 256, _mapTransformMatrix->bones);
			}
		}

		angle += 0.01f;
		if (_mapTransformMatrix) {
			_mapTransformMatrix->world = XMMatrixRotationY(angle) * XMMatrixTranslation(0, 0, 0);
		}
		_mapSceneMatrix->view = _vMatrix;
		_mapSceneMatrix->proj = _pMatrix;

		// このふたつをいれないと描画されない。(背景しか出ない)
		_graphicsDevice->GetCommandList()->RSSetViewports(1, &viewport);
		_graphicsDevice->GetCommandList()->RSSetScissorRects(1, &scissorrect);

		_graphicsDevice->GetCommandList()->SetDescriptorHeaps(1, g_resourceDescriptorHeapWrapper->GetAddressOf());

		//{ // 0. Shadow pipeline (shadow map light depth)
		//	// depthはbarrierとかいらない?
		//	{
		//		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
		//		dsvHandle.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		//		_graphicsDevice->GetCommandList()->OMSetRenderTargets(0, nullptr, false, &dsvHandle); // no need RT
		//		_graphicsDevice->GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		//		_graphicsDevice->GetCommandList()->SetGraphicsRootSignature(m_rootSignature->GetRootSignaturePointer());
		//		_graphicsDevice->GetCommandList()->SetPipelineState(_shadowPipelineState.Get());
		//	}


		//	{ // Heap start -> SRV of _postProcessResource -> SRV of _depthBuffer -> SRV of _lightDepthBuffer
		//		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(g_resourceDescriptorHeapWrapper->GetGPUDescriptorHandleForHeapStart());
		//		auto srvIncSize = _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		//		gpuHandle.ptr += srvIncSize; // TODO: remove? (SRV of _postProcessResource is the first SRV created from Application::CreateDepthStencilView())
		//		gpuHandle.ptr += srvIncSize * 2; // TODO: remove?

		//		_graphicsDevice->GetCommandList()->SetDescriptorHeaps(1, g_resourceDescriptorHeapWrapper->GetAddressOf());
		//		_graphicsDevice->GetCommandList()->SetGraphicsRootDescriptorTable(0, g_resourceDescriptorHeapWrapper->GetGPUDescriptorHandleForHeapStart());
		//	}

		//	for (auto itr : _modelImporter->mesh_vertices) {
		//		std::string name = itr.first;
		//		_graphicsDevice->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		//		_graphicsDevice->GetCommandList()->IASetVertexBuffers(0, 1, &vertex_buffer_view[name]);
		//		_graphicsDevice->GetCommandList()->IASetIndexBuffer(&index_buffer_view[name]);
		//		_graphicsDevice->GetCommandList()->DrawIndexedInstanced((UINT)_modelImporter->mesh_indices[name].size(), 1, 0, 0, 0);
		//	}
		//}

		std::vector<D3D12_RESOURCE_BARRIER> vertexBarriers;
		if (_model) { // 0 pass (skinning with CS)
			_graphicsDevice->GetCommandList()->SetComputeRootSignature(_computeRootSignature.Get());
			_graphicsDevice->GetCommandList()->SetPipelineState(_computePipelineState.Get());
			
			for (const auto& mesh : _model->GetMeshDrawInfos()) {
				_graphicsDevice->GetCommandList()->SetComputeRootDescriptorTable(0, mesh.srvGpuHandle); // t0: InputVertices
				_graphicsDevice->GetCommandList()->SetComputeRootDescriptorTable(1, mesh.uavGpuHandle); // u0: OutputVertices
				_graphicsDevice->GetCommandList()->SetComputeRootConstantBufferView(2, mesh.cbvGpuHandle); // b0: BoneMatrices

				UINT threadGroupCount = (mesh.vertexCount + 63) / 64; // 64 vertex per thread group
				_graphicsDevice->GetCommandList()->Dispatch(threadGroupCount, 1, 1);
			}

			for (const auto& mesh : _model->GetMeshDrawInfos()) {
				vertexBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
					mesh.pOutputVertexBuffer,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
					D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
				));
			}
			_graphicsDevice->GetCommandList()->ResourceBarrier((UINT)vertexBarriers.size(), vertexBarriers.data());
		}

		{ // 1 pass
			// これunionらしい。Transition, Aliasing, UAV バリアがある。
			D3D12_RESOURCE_BARRIER beforeDrawTransitionDesc = CD3DX12_RESOURCE_BARRIER::Transition(
				_postProcessResource.Get(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);
			_graphicsDevice->GetCommandList()->ResourceBarrier(1, &beforeDrawTransitionDesc);

			{
				D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
				D3D12_CPU_DESCRIPTOR_HANDLE postProcessRTVHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
				postProcessRTVHandle.ptr += _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * 2; // g_pRenderTargets[0], g_pRenderTargets[1], _postProcessResource
				_graphicsDevice->GetCommandList()->OMSetRenderTargets(1, &postProcessRTVHandle, false, &dsvHandle);
				// draw
				float clearColor[] = { 1.0f,1.0f,1.0f,1.0f };
				_graphicsDevice->GetCommandList()->ClearRenderTargetView(postProcessRTVHandle, clearColor, 0, nullptr);
				_graphicsDevice->GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				_graphicsDevice->GetCommandList()->SetGraphicsRootSignature(m_rootSignature->GetRootSignaturePointer());
				_graphicsDevice->GetCommandList()->SetPipelineState(_pipelineState.Get());
			}

			{// Set Resource DescriptorHeap
				D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(g_resourceDescriptorHeapWrapper->GetGPUDescriptorHandleForHeapStart());
				auto srvIncSize = _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


				{ // Heap start -> SRV of _postProcessResource -> SRV of _depthBuffer -> SRV of _lightDepthBuffer
					gpuHandle.ptr += srvIncSize; // TODO: remove? (SRV of _postProcessResource is the first SRV created from Application::CreateDepthStencilView())
					_graphicsDevice->GetCommandList()->SetGraphicsRootDescriptorTable(2, gpuHandle);
					gpuHandle.ptr += srvIncSize * 2; // TODO: remove?
				}

				// SetGraphicsRootDescriptorTable: この関数の役割は、Descriptor Heap内の特定の場所(handle)とRoot Signatureで定義されたスロットをバインドし、シェーダー側から扱えるようにする。
				// SetGraphicsRootDescriptorTableの第一引数はRootParameterのindex(setting in TDX12RootSignature::Initialize)
				// CBV0, CBV1 -> b0, b1, SRV0, SRV1, SRV2 -> t0, t1, t2という紐づけをまとめてできるが、DescriptorHeapに積む順番はこの通りにする必要があるし、同じDescriptorHeap上に別のDescriptorTableを適用したい場合は先頭アドレスを再度動かす必要がある。
				// BasicShader.hlslのt1（メッシュのテクスチャ）だけは更新頻度が異なるので、別のDescriptorTableに割り当てたうえで、メッシュごとに[1. DesciptorHandleを移動させる]->[2. DescriptorTableを適用する]->[3. 描画する]としている。
				_graphicsDevice->GetCommandList()->SetGraphicsRootDescriptorTable(0, gpuHandle);
				gpuHandle.ptr += srvIncSize * 2; // b0, b1

				_graphicsDevice->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				if (_model) {
					int meshIdx = 0;
					for (const auto& mesh : _model->GetMeshDrawInfos()) {
						const std::string& name = _modelImporter->mesh_names[meshIdx++];
						_graphicsDevice->GetCommandList()->SetGraphicsRootDescriptorTable(1, mesh.materialTexGpuHandle);

						auto vbv = _model->GetVBV(name);
						auto ibv = _model->GetIBV(name);
						if (vbv && ibv) {
							_graphicsDevice->GetCommandList()->IASetVertexBuffers(0, 1, vbv);
							_graphicsDevice->GetCommandList()->IASetIndexBuffer(ibv);
							_graphicsDevice->GetCommandList()->DrawIndexedInstanced((UINT)_modelImporter->mesh_indices[name].size(), 2, 0, 0, 0);
						}
					}
				}
			}
			// draw end
			auto afterDrawTransitionDesc = CD3DX12_RESOURCE_BARRIER::Transition(
				_postProcessResource.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);
			_graphicsDevice->GetCommandList()->ResourceBarrier(1, &afterDrawTransitionDesc);
			{ // Vertex barriers
				if (_model) {
					UINT index = 0;
					vertexBarriers.clear();
					for (const auto& mesh : _model->GetMeshDrawInfos()) {
						vertexBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
							mesh.pOutputVertexBuffer,
							D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						));
					}
					if (!vertexBarriers.empty()) {
						_graphicsDevice->GetCommandList()->ResourceBarrier((UINT)vertexBarriers.size(), vertexBarriers.data());
					}
				}
			}
		}
		{ // 2 pass
			// Transition RTV state from PRESENT to RENDER
			D3D12_RESOURCE_BARRIER beforeDrawTransitionDesc = CD3DX12_RESOURCE_BARRIER::Transition(
				_graphicsDevice->GetCurrentBackBuffer(),
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);
			_graphicsDevice->GetCommandList()->ResourceBarrier(1, &beforeDrawTransitionDesc);

			{
				unsigned int bbIdx = _graphicsDevice->GetCurrentBackBufferIndex();
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
				rtvHandle.ptr += bbIdx * _graphicsDevice->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
				_graphicsDevice->GetCommandList()->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
				_graphicsDevice->GetCommandList()->SetGraphicsRootSignature(_canvasRootSignature.Get());
				_graphicsDevice->GetCommandList()->SetPipelineState(_canvasPipelineState.Get());
			}

			{
				{// register 1 pass as texture
					// SRV of _postProcessResource is the first SRV created from Application::CreateDepthStencilView()
					auto handle = g_resourceDescriptorHeapWrapper->GetGPUDescriptorHandleForHeapStart();
					_graphicsDevice->GetCommandList()->SetGraphicsRootDescriptorTable(0, handle);
				}

				_graphicsDevice->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				_graphicsDevice->GetCommandList()->IASetVertexBuffers(0, 1, &_canvasVBV);
				_graphicsDevice->GetCommandList()->DrawInstanced(4, 1, 0, 0);
			}
			{ // ImGui draws to RT set by OMSetRenderTargets, so make commands for rendering ImGui while the state of that RT is D3D12_RESOURCE_STATE_RENDER_TARGET
				DrawImGui(useGpuSkinning, animState);
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), _graphicsDevice->GetCommandList());
			}

			auto afterDrawTransitionDesc = CD3DX12_RESOURCE_BARRIER::Transition(
				_graphicsDevice->GetCurrentBackBuffer(),
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT
			);
			_graphicsDevice->GetCommandList()->ResourceBarrier(1, &afterDrawTransitionDesc);
		}


		_graphicsDevice->GetCommandList()->Close();
		// コマンドリストは複数渡せる？コマンドリストのリストを作成
		ID3D12CommandList* cmdlists[] = { _graphicsDevice->GetCommandList() };
		_graphicsDevice->GetCommandQueue()->ExecuteCommandLists(1, cmdlists);

		// Fenceによる同期待ち
		_graphicsDevice->WaitDrawDone();

		_graphicsDevice->GetCommandAllocator()->Reset();
		_graphicsDevice->GetCommandList()->Reset(_graphicsDevice->GetCommandAllocator(), _pipelineState.Get());
		//フリップ 1は待ちframe数(待つべきvsyncの数), 2にすると30fpsになる
		_graphicsDevice->GetSwapChain()->Present(2, 0);
	}
}

void Application::Terminate() {
	CleanupImGui();
	// windowManager と _modelImporter は unique_ptr なので、Application の破棄時に自動的に削除されます。
}