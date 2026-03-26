#include "DX12GraphicsDevice.h"
#include <iostream>

DX12GraphicsDevice::DX12GraphicsDevice() {}
DX12GraphicsDevice::~DX12GraphicsDevice() {
    WaitDrawDone();
}

bool DX12GraphicsDevice::Initialize(HWND hwnd, unsigned int width, unsigned int height) {
    if (!CreateDevice()) return false;
    if (!CreateCommandList()) return false;
    if (!CreateSwapChain(hwnd, width, height)) return false;
    if (!CreateFence()) return false;
    return true;
}

bool DX12GraphicsDevice::CreateDevice() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugLayer;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
        debugLayer->EnableDebugLayer();
    }
    CheckError("CreateDXGIFactory2", CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));
#else
    CheckError("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));
#endif

    IDXGIAdapter* adapter = nullptr;
    for (int i = 0; m_dxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC adesc = {};
        adapter->GetDesc(&adesc);
        std::wstring strDesc = adesc.Description;
        if (strDesc.find(L"NVIDIA") != std::string::npos) {
            break;
        }
    }

    for (D3D_FEATURE_LEVEL level : {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}) {
        if (D3D12CreateDevice(adapter, level, IID_PPV_ARGS(m_dev.ReleaseAndGetAddressOf())) == S_OK) {
            break;
        }
    }
    return m_dev != nullptr;
}

bool DX12GraphicsDevice::CreateCommandList() {
    CheckError("CreateCommandAllocator", m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_cmdAllocator.ReleaseAndGetAddressOf())));
    CheckError("CreateCommandList", m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator.Get(), nullptr, IID_PPV_ARGS(m_cmdList.ReleaseAndGetAddressOf())));
    
    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
    cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CheckError("CreateCommandQueue", m_dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(m_cmdQueue.ReleaseAndGetAddressOf())));
    return true;
}

bool DX12GraphicsDevice::CreateSwapChain(HWND hwnd, unsigned int width, unsigned int height) {
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = width;
    swapchainDesc.Height = height;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.Stereo = false;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
    swapchainDesc.BufferCount = 2;
    swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    CheckError("CreateSwapChainForHwnd", m_dxgiFactory->CreateSwapChainForHwnd(m_cmdQueue.Get(), hwnd, &swapchainDesc, nullptr, nullptr, &swapchain1));
    CheckError("QueryInterface SwapChain4", swapchain1.As(&m_swapchain));

    for (UINT i = 0; i < 2; ++i) {
        CheckError("GetBuffer", m_swapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].ReleaseAndGetAddressOf())));
    }
    return true;
}

bool DX12GraphicsDevice::CreateFence() {
    CheckError("CreateFence", m_dev->CreateFence(m_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    return true;
}

void DX12GraphicsDevice::WaitDrawDone() {
    m_cmdQueue->Signal(m_fence.Get(), ++m_fenceVal);
    if (m_fence->GetCompletedValue() != m_fenceVal) {
        HANDLE event = CreateEvent(nullptr, false, false, nullptr);
        if (event) {
            m_fence->SetEventOnCompletion(m_fenceVal, event);
            WaitForSingleObject(event, INFINITE);
            CloseHandle(event);
        }
    }
}

void DX12GraphicsDevice::Present(unsigned int syncInterval) {
    m_swapchain->Present(syncInterval, 0);
}

void DX12GraphicsDevice::ResetCommandList() {
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
}

void DX12GraphicsDevice::ExecuteCommandList() {
    m_cmdList->Close();
    ID3D12CommandList* cmdlists[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, cmdlists);
}

ID3D12Resource* DX12GraphicsDevice::GetCurrentBackBuffer() const {
    return m_renderTargets[GetCurrentBackBufferIndex()].Get();
}

unsigned int DX12GraphicsDevice::GetCurrentBackBufferIndex() const {
    return m_swapchain->GetCurrentBackBufferIndex();
}

void DX12GraphicsDevice::CheckError(const char* msg, HRESULT hr) {
    if (FAILED(hr)) {
        std::cerr << msg << " failed with HRESULT: " << std::hex << hr << std::endl;
        exit(1);
    }
}
