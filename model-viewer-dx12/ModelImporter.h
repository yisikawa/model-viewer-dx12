#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Common.h"
#include "Types.h"

class ModelImporter {
public:
	ModelImporter() {
		for (int i = 0; i < 256; ++i) {
			boneMatrices[i] = DirectX::XMMatrixIdentity();
		}
	}
	bool CreateModelImporter(const std::string& inFbxFileName);
private:
	void LoadMesh(aiMesh* mesh, unsigned int meshIndex);
	std::string GetExtension(const std::string& path) {
		auto idx = path.rfind('.');
		return path.substr(idx + 1, path.length() - idx - 1);
	}
	std::wstring GetExtension(const std::wstring& path) {
		auto idx = path.rfind(L'.');
		return path.substr(idx + 1, path.length() - idx - 1);
	}
	void UpdateBoneMatrices_internal(aiNode* pNode, const DirectX::XMMATRIX& parentTransform, const ModelViewer::AnimState& animState);
	DirectX::XMMATRIX InterpolateTransform(const aiNodeAnim* pNodeAnim, float animationTime);

	Assimp::Importer importer;
	const aiScene* scene;
	std::map<std::string, ModelViewer::Material> raw_materials;

	// Animation Parameters
	UINT mCurrentAnimIdx = 0;
	float mAnimCurrentTicks = 0., mAnimDurationTicks = 0., mAnimTicksPerSecond = 0.;

public:
	std::map<std::string, std::vector<ModelViewer::Vertex>> mesh_vertices; 
	std::map<std::string, std::vector<unsigned short>> mesh_indices;
	std::vector<std::string> texture_names;
	std::vector<int> mesh_texture_indices;
	std::vector<std::string> mesh_names;
	
	std::map<std::string, std::string> mesh_material_name;
	struct MeshTextureData {
		aiTextureType type;
		std::string path;
	};
	std::map<std::string, std::vector<MeshTextureData>> mesh_textures_map;
	std::map<std::string, std::string> mesh_texture_name; // Legacy support
	std::map<std::string, unsigned int> node_bone_map;
	std::map<std::string, aiNodeAnim*> node_anim_map;

	struct BoneInfo {
		aiNode* meshNode; 
		aiBone* cluster; 
	};
	// bone information
	BoneInfo bones[256] = {};
	DirectX::XMMATRIX boneOffsets[256] = {};
	DirectX::XMMATRIX boneMatrices[256] = {};
	DirectX::XMMATRIX globalInverseTransform;
	// Animation
	DirectX::XMMATRIX ConvertFbxMatrix(const aiMatrix4x4& src);
	void UpdateBoneMatrices(float deltaTime, ModelViewer::AnimState &animState);

	// ImGui operation
	ModelViewer::AnimState GetDefaultAnimState();
	void SetCurrentAnimation(UINT currentAnimIdx);
};
