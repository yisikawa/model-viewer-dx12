#include "DX12ShaderResource.h"

TDX12ShaderResource::TDX12ShaderResource(const std::string& textureFileName, ID3D12Device* device) {
	Initialize(device, "", textureFileName);
}

DXGI_FORMAT TDX12ShaderResource::GetResourceFormat() {
	return m_textureMetadata.format;
}

// 1. テクスチャロード、 2. リソース生成 3. データコピー
void TDX12ShaderResource::Initialize(ID3D12Device* device, const std::string& modelDir, const std::string& textureFileName) {
	DirectX::ScratchImage scratchImg = {};
	std::string fullPath = textureFileName;
	if (!modelDir.empty()) {
		fullPath = modelDir + "/" + textureFileName;
	}
	std::wstring wtexpath = GetWideStringFromString(fullPath);
	std::string ext = GetExtension(fullPath);
	HRESULT result;
	if (ext == "tga") {
		result = LoadFromTGAFile(wtexpath.c_str(), &m_textureMetadata, scratchImg);
	}
	else {
		result = LoadFromWICFile(wtexpath.c_str(), DirectX::WIC_FLAGS_NONE, &m_textureMetadata, scratchImg);
	}

	if (FAILED(result)) {
		return;
	}
	auto img = scratchImg.GetImage(0, 0, 0);

	D3D12_HEAP_PROPERTIES texHeapProp = {};
	texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Format = m_textureMetadata.format;
	resDesc.Width = static_cast<UINT>(m_textureMetadata.width);
	resDesc.Height = static_cast<UINT>(m_textureMetadata.height);
	resDesc.DepthOrArraySize = static_cast<UINT16>(m_textureMetadata.arraySize);
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.MipLevels = static_cast<UINT16>(m_textureMetadata.mipLevels);
	resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(m_textureMetadata.dimension);
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	result = device->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&m_shaderResource)
	);

	if (FAILED(result)) {
		return;
	}
	result = m_shaderResource->WriteToSubresource(0,
		nullptr,
		img->pixels,
		static_cast<UINT>(img->rowPitch),
		static_cast<UINT>(img->slicePitch)
	);
	if (FAILED(result)) {
		return;
	}
}
