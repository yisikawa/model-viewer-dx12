#pragma once
#include "../Common.h"

class TDX12ShaderResource {
public:
	TDX12ShaderResource() = default;
	TDX12ShaderResource(ID3D12Resource* inResource) : m_shaderResource(inResource) {}
	TDX12ShaderResource(const std::string& textureFileName, ID3D12Device* device);
	void Initialize(ID3D12Device* device, const std::string& modelDir, const std::string& textureFileName);
	bool IsValid() {
		return m_shaderResource != nullptr;
	}
	DXGI_FORMAT GetResourceFormat();

	static std::wstring GetWideStringFromString(const std::string& str) {
		auto num1 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
		std::wstring wstr;
		wstr.resize(num1);
		auto num2 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, &wstr[0], num1);
		return wstr;
	}
	static std::string GetExtension(const std::string& path) {
		auto idx = path.rfind('.');
		return (idx == std::string::npos) ? "" : path.substr(idx + 1);
	}
	static std::wstring GetExtension(const std::wstring& path) {
		auto idx = path.rfind(L'.');
		return (idx == std::wstring::npos) ? L"" : path.substr(idx + 1);
	}
	ID3D12Resource* m_shaderResource = nullptr;
	DirectX::TexMetadata m_textureMetadata = {};
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = {};
};
