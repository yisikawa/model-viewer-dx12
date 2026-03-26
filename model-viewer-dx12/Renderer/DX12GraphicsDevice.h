#pragma once

#include "../Common.h"
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <vector>

class DX12GraphicsDevice {
public:
    DX12GraphicsDevice();
    ~DX12GraphicsDevice();

    bool Initialize(HWND hwnd, unsigned int width, unsigned int height);
    
    void WaitDrawDone();
    void Present(unsigned int syncInterval);
    void ResetCommandList();
    void ExecuteCommandList();

    // Getters
    ID3D12Device* GetDevice() const { return m_dev.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_cmdList.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_cmdQueue.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator() const { return m_cmdAllocator.Get(); }
    IDXGISwapChain4* GetSwapChain() const { return m_swapchain.Get(); }
    
    ID3D12Resource* GetCurrentBackBuffer() const;
    unsigned int GetCurrentBackBufferIndex() const;
    ID3D12Resource* GetRenderTarget(UINT index) const { return m_renderTargets[index].Get(); }

private:
    bool CreateDevice();
    bool CreateCommandList();
    bool CreateSwapChain(HWND hwnd, unsigned int width, unsigned int height);
    bool CreateFence();

    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_dev;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_cmdQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapchain;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[2];
    
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceVal = 0;

    void CheckError(const char* msg, HRESULT hr);
};
