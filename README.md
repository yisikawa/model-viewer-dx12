# Model Viewer & Animation System (DirectX 12 implementation)

## Overview
A 3D model viewer & animation system built from scratch using DirectX 12.

This project focuses on low-level graphics programming.

## Features

### Implemented
- Model loading via Assimp (FBX, glTF, OBJ, etc.)
- Root signature design
- Descriptor heap management
- Shadow mapping
- Control various animation properties from ImGui
- GPU skinning (compute shader)

### Planned
- Deferred rendering
- Physically Based Rendering (PBR)
- Image-Based Lighting (IBL)

### Current progress (v0.3.0)

<img width="682" height="452" alt="Image" src="https://github.com/user-attachments/assets/b7c54acf-6e3c-41a8-9acd-591a2a46b332" />

A skinned animated model rendered with:
- basic lighting
- shadow mapping

## 操作方法

### マウス操作
- **左ドラッグ**: カメラの回転（ピッチ・ヨー）
- **Shift + 左ドラッグ**: カメラの平行移動（パン）
- **Ctrl + 左ドラッグ**: 光源の回転（影の方向変更）
- **マウスホイール**: モデルの拡大・縮小（スケーリング）

### モデルの読み込み
- **File > Open**: ファイル選択ダイアログを表示して3Dモデル（FBX, glTF, OBJ, GLB, DirectXファイル(.x)等）を読み込みます。

## How to build

### 0. Requirements
- Windows 10/11
- Visual Studio 2022
- CMake

### 1. Setup
```bash
git clone --recursive https://github.com/tonyu0/model-viewer-dx12.git
cd model-viewer-dx12
```

### 2. Build assimp
```bash
cd external/assimp
mkdir build
cmake -S . -B build
# cmake --build build --config Debug # when a debug build is necessary
cmake --build build --config Release
```

### 3. Open the solution file (.sln) and build in Visual Studio.
