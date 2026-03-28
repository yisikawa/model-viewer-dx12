#include "ModelImporter.h"

void ModelImporter::LoadMesh(aiMesh* mesh, unsigned int meshIndex) {
	std::string mesh_base_name = mesh->mName.C_Str();
	if (mesh_base_name.empty()) mesh_base_name = "Mesh";
	const std::string mesh_name = mesh_base_name + "_" + std::to_string(meshIndex);
	std::cout << "Load " << mesh_name << " Data" << std::endl;
	// 1. 頂点情報の読み込み: pos, normal, uv
	std::vector<ModelViewer::Vertex> vertices;
	for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
		ModelViewer::Vertex vert{};
		auto p = mesh->mVertices[v];
		vert.pos = { p.x, p.y, p.z };
		if (mesh->HasNormals()) {
			auto n = mesh->mNormals[v];
			vert.normal = { n.x, n.y, n.z };
		}
		if (mesh->HasTextureCoords(0)) {
			auto uv = mesh->mTextureCoords[0][v];
			vert.uv = { uv.x, uv.y };
		}
		vertices.push_back(vert);
	}
	mesh_vertices[mesh_name] = vertices;


 // 2. 頂点インデックスの読み込み
	for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
		aiFace& face = mesh->mFaces[f];
		for (unsigned int ind = 0; ind < face.mNumIndices; ++ind) {
			mesh_indices[mesh_name].push_back(face.mIndices[ind]);
		}
	}


	// 3. 頂点情報の読み込み: weight, boneid
	auto AddBoneInfo = [](ModelViewer::Vertex& v, int boneid, float weight) {
		for (int i = 0; i < 4; ++i) {
			if (v.weight[i] == 0.0f) {
				v.weight[i] = weight;
				v.boneid[i] = (uint32_t)boneid;
				break;
			}
		}
	};

	// boneid, weight
	for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
		aiBone* bone = mesh->mBones[i];
		std::string boneName = bone->mName.C_Str();
			
		unsigned int boneIndex;
		// [改善] ボーン名ですでに登録されているか確認
		if (node_bone_map.count(boneName)) {
			// 登録済みならそのインデックスを使用
			boneIndex = node_bone_map[boneName];
		} else {
			// 未登録なら新しいインデックス（現在のマップサイズ）を割り当て
			boneIndex = (unsigned int)node_bone_map.size();
			node_bone_map[boneName] = boneIndex;
			boneOffsets[boneIndex] = bone->mOffsetMatrix;
		}

		std::cout << "Mesh: " << mesh_name << " | Bone: " << boneName << " -> Global Index: " << boneIndex << std::endl;

		for (unsigned int j = 0; j < bone->mNumWeights; ++j) {
			aiVertexWeight weight = bone->mWeights[j];
			int vertexId = weight.mVertexId;
			// 頂点にグローバルなボーンIDを書き込む
			AddBoneInfo(mesh_vertices[mesh_name][vertexId], boneIndex, weight.mWeight);
		}
	}


	// meshとマテリアルの結び付け。メッシュ単位で描画するので、そこからマテリアルが取れればさらにそこからテクスチャが取れて、無事UVマッピング。
	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	std::string materialName = material->GetName().C_Str();
	aiString texPath;
	mesh_material_name[mesh_name] = materialName;

	int texIdx = -1;
	// Check all the texture types and collect all paths
	for (int i = 1; i < aiTextureType_UNKNOWN; ++i) {
		aiTextureType type = (aiTextureType)i;
		unsigned int count = material->GetTextureCount(type);
		for (unsigned int j = 0; j < count; ++j) {
			if (material->GetTexture(type, j, &texPath) == aiReturn_SUCCESS) {
				std::string tName = texPath.C_Str();
				mesh_textures_map[mesh_name].push_back({ type, tName });

				// Add to overall unique texture list
				auto it = std::find(texture_names.begin(), texture_names.end(), tName);
				if (it == texture_names.end()) {
					texture_names.push_back(tName);
				}

				// Still set primary texture for legacy support and initial setup
				if (mesh_texture_name.count(mesh_name) == 0) {
					mesh_texture_name[mesh_name] = tName;
					auto it_primary = std::find(texture_names.begin(), texture_names.end(), tName);
					texIdx = (int)std::distance(texture_names.begin(), it_primary);
				}
			}
		}
	}
	mesh_texture_indices.push_back(texIdx);
}

bool ModelImporter::CreateModelImporter(const std::string& inFbxFileName) {
	
	scene = importer.ReadFile(inFbxFileName, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder);

	if (!scene) {
		std::cout << importer.GetErrorString() << std::endl;
		return false;
	}

	if (scene->mNumAnimations > 0) {
		SetCurrentAnimation(0);
	}


	int meshCount = scene->mNumMeshes;
	for (int i = 0; i < meshCount; ++i) {
		aiMesh* mesh = scene->mMeshes[i];
		std::string mesh_name = mesh->mName.C_Str();
		if (mesh_name.empty()) mesh_name = "Mesh";
		std::string unique_name = mesh_name + "_" + std::to_string(i);

		std::cout << i << " th mesh unique name is " << unique_name << std::endl;
		mesh_names.push_back(unique_name);
		LoadMesh(mesh, (unsigned int)i);
	}

	return true;
}

ModelViewer::AnimState ModelImporter::GetDefaultAnimState() {
	ModelViewer::AnimState animState;

	animState.sceneAnimCount = scene->mNumAnimations;
	if (animState.sceneAnimCount > 0) {
		animState.currentAnimIdx = 0;
		animState.currentAnimDuration = (float)scene->mAnimations[0]->mDuration / mAnimTicksPerSecond;
		for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
			animState.animationNames.push_back(scene->mAnimations[i]->mName.C_Str());
		}
	}
	animState.isLooping = true;
	animState.isPlaying = true;
	animState.playingTime = 0.f;
	animState.playingSpeed = 1.f;
	animState.showBindPose = false;

	return animState;
}

void ModelImporter::SetCurrentAnimation(UINT currentAnimIdx) {
	if (currentAnimIdx >= scene->mNumAnimations) {
		std::cout << "Invalid animation index specified!" << std::endl;
		return;
	}

	node_anim_map.clear();
	for (unsigned int j = 0; j < scene->mAnimations[currentAnimIdx]->mNumChannels; ++j) {
		aiNodeAnim* node_anim = scene->mAnimations[currentAnimIdx]->mChannels[j];
		node_anim_map[node_anim->mNodeName.C_Str()] = node_anim;
	}

	mCurrentAnimIdx = currentAnimIdx;
	mAnimDurationTicks = (float)scene->mAnimations[currentAnimIdx]->mDuration;
	mAnimTicksPerSecond = scene->mAnimations[currentAnimIdx]->mTicksPerSecond != 0 ? (float)scene->mAnimations[currentAnimIdx]->mTicksPerSecond : 25.f;
	mAnimCurrentTicks = 0.;
}

void ModelImporter::UpdateBoneMatrices(float deltaTime, ModelViewer::AnimState &animState) {
	if (animState.isPlaying) {
		mAnimCurrentTicks = mAnimCurrentTicks + mAnimTicksPerSecond * deltaTime * animState.playingSpeed;
	}

	if (animState.isLooping) {
		mAnimCurrentTicks = fmod(mAnimCurrentTicks, mAnimDurationTicks);
	}
	else {
		mAnimCurrentTicks = std::fmin(mAnimCurrentTicks, mAnimDurationTicks - 0.01f); // Setting exact value leads to broken model due to decimal error, substract some small value
	}

	if (mCurrentAnimIdx != animState.currentAnimIdx) {
		SetCurrentAnimation(animState.currentAnimIdx);
	}
	animState.playingTime = mAnimCurrentTicks / mAnimTicksPerSecond;
	animState.currentAnimDuration = mAnimDurationTicks / mAnimTicksPerSecond;
	
	UpdateBoneMatrices_internal(scene->mRootNode, aiMatrix4x4(), animState);
}

void ModelImporter::UpdateBoneMatrices_internal(aiNode* pNode, const aiMatrix4x4& parentTransform, const ModelViewer::AnimState& animState) {
	std::string nodeName = pNode->mName.C_Str();
	aiMatrix4x4 nodeTransform = pNode->mTransformation;
	// このノードに対応するアニメーションチャンネルがあれば補間行列を取得
	const aiNodeAnim* pNodeAnim = node_anim_map[nodeName];
	if (pNodeAnim && !animState.showBindPose) {
		nodeTransform = InterpolateTransform(pNodeAnim, mAnimCurrentTicks);
	}

	// 親と掛け合わせてこのノードのグローバル行列
	aiMatrix4x4 globalTransform = parentTransform * nodeTransform;
	// 
	if (node_bone_map.count(nodeName)) {
		unsigned int boneIndex = node_bone_map[nodeName];
		// 実際の座標にはboneOffsets -> globalTransform -> globalInverseTransformの順にかかる。
		// 頂点をvとすると, 
		// boneOffsetsはvをモデル空間からボーン空間へtransform
		// globalTransformは親ボーンから蓄積された（現在のアニメーション再生時間を考慮）transform
		// globalInverseTransformはモデル全体にかかってるtransformを打ち消す（ワールド座標系で扱いやすく？）
		boneMatrices[boneIndex] = ConvertFbxMatrix(globalInverseTransform * globalTransform * boneOffsets[boneIndex]);
	}

	for (unsigned int i = 0; i < pNode->mNumChildren; ++i) {
		UpdateBoneMatrices_internal(pNode->mChildren[i], globalTransform, animState);
	}
}

aiMatrix4x4 ModelImporter::InterpolateTransform(const aiNodeAnim* pNodeAnim, float animationTime) {
	//if(pNodeAnim->mNumPositionKeys < 2) {}

	aiVector3D interpolatedPos, interpolatedScale;
	aiQuaternion interpolatedRot;
	// Interpolate position
	if(pNodeAnim->mNumPositionKeys > 1) {
		// 1. 指定したアニメーション時間から、見るべきキーフレームを決定
		unsigned int targetPosKeyIdx = 0;
		for (unsigned int i = 0; i < pNodeAnim->mNumPositionKeys - 1; ++i) {
			if (animationTime < pNodeAnim->mPositionKeys[i + 1].mTime) {
				targetPosKeyIdx = i;
				break;
			}
		}

		// 2. 補間係数を計算
		float t1 = (float)pNodeAnim->mPositionKeys[targetPosKeyIdx].mTime;
		float t2 = (float)pNodeAnim->mPositionKeys[targetPosKeyIdx + 1].mTime;
		float factor = (animationTime - t1) / (t2 - t1);

		// 3. 補間計算
		// ----- Lerp to position -----
		aiVector3D startPos = pNodeAnim->mPositionKeys[targetPosKeyIdx].mValue;
		aiVector3D endPos = pNodeAnim->mPositionKeys[targetPosKeyIdx + 1].mValue;
		interpolatedPos = startPos + (endPos - startPos) * float(factor);
	}

	// Interpolate scaling
	if(pNodeAnim->mNumScalingKeys > 1) {
		// 1. 指定したアニメーション時間から、見るべきキーフレームを決定
		unsigned int targetScaleKeyIdx = 0;
		for (unsigned int i = 0; i < pNodeAnim->mNumScalingKeys - 1; ++i) {
			if (animationTime < pNodeAnim->mScalingKeys[i + 1].mTime) {
				targetScaleKeyIdx = i;
				break;
			}
		}

		// 2. 補間係数を計算
		float t1 = (float)pNodeAnim->mScalingKeys[targetScaleKeyIdx].mTime;
		float t2 = (float)pNodeAnim->mScalingKeys[targetScaleKeyIdx + 1].mTime;
		float factor = (animationTime - t1) / (t2 - t1);

		// ----- Lerp to scale -----
		aiVector3D startScale = pNodeAnim->mScalingKeys[targetScaleKeyIdx].mValue;
		aiVector3D endScale = pNodeAnim->mScalingKeys[targetScaleKeyIdx + 1].mValue;
		interpolatedScale = startScale + (endScale - startScale) * float(factor);
	}

	// Interpolate rotation
	if(pNodeAnim->mNumRotationKeys > 1) {
		// 1. 指定したアニメーション時間から、見るべきキーフレームを決定
		unsigned int targetRotKeyIdx = 0;
		for (unsigned int i = 0; i < pNodeAnim->mNumRotationKeys - 1; ++i) {
			if (animationTime < pNodeAnim->mRotationKeys[i + 1].mTime) {
				targetRotKeyIdx = i;
				break;
			}
		}

		// 2. 補間係数を計算
		float t1 = (float)pNodeAnim->mRotationKeys[targetRotKeyIdx].mTime;
		float t2 = (float)pNodeAnim->mRotationKeys[targetRotKeyIdx + 1].mTime;
		float factor = (animationTime - t1) / (t2 - t1);

		// ----- Slerp to rotation -----
		aiQuaternion startRot = pNodeAnim->mRotationKeys[targetRotKeyIdx].mValue;
		aiQuaternion endRot = pNodeAnim->mRotationKeys[targetRotKeyIdx + 1].mValue;
		aiQuaternion::Interpolate(interpolatedRot, startRot, endRot, float(factor));
	}

	// 4. 変換行列を構築
	// ----- translation -----
	aiMatrix4x4 translationMat;
	aiMatrix4x4::Translation(interpolatedPos, translationMat);
	// ----- scaling -----
	aiMatrix4x4 scalingMat;
	aiMatrix4x4::Scaling(interpolatedScale, scalingMat);
	// ----- rotation -----
	aiMatrix4x4 rotationMat = aiMatrix4x4(interpolatedRot.GetMatrix());

	// Assimpは右からかける
	return translationMat * rotationMat * scalingMat;
}

DirectX::XMMATRIX ModelImporter::ConvertFbxMatrix(const aiMatrix4x4& src)
{
	return		DirectX::XMMatrixSet(
		static_cast<FLOAT>(src.a1), static_cast<FLOAT>(src.b1), static_cast<FLOAT>(src.c1), static_cast<FLOAT>(src.d1),
		static_cast<FLOAT>(src.a2), static_cast<FLOAT>(src.b2), static_cast<FLOAT>(src.c2), static_cast<FLOAT>(src.d2),
		static_cast<FLOAT>(src.a3), static_cast<FLOAT>(src.b3), static_cast<FLOAT>(src.c3), static_cast<FLOAT>(src.d3),
		static_cast<FLOAT>(src.a4), static_cast<FLOAT>(src.b4), static_cast<FLOAT>(src.c4), static_cast<FLOAT>(src.d4));

	//return		XMMatrixSet(
	//	static_cast<FLOAT>(src.a1), static_cast<FLOAT>(src.a2), static_cast<FLOAT>(src.a3), static_cast<FLOAT>(src.a4),
	//	static_cast<FLOAT>(src.b1), static_cast<FLOAT>(src.b2), static_cast<FLOAT>(src.b3), static_cast<FLOAT>(src.b4),
	//	static_cast<FLOAT>(src.c1), static_cast<FLOAT>(src.c2), static_cast<FLOAT>(src.c3), static_cast<FLOAT>(src.c4),
	//	static_cast<FLOAT>(src.d1), static_cast<FLOAT>(src.d2), static_cast<FLOAT>(src.d3), static_cast<FLOAT>(src.d4));
}