$utf8bom = New-Object System.Text.UTF8Encoding($true)
$files = @(
    "D:\projects\model-viewer-dx12\model-viewer-dx12\Application.cpp",
    "D:\projects\model-viewer-dx12\model-viewer-dx12\Renderer\Model.cpp",
    "D:\projects\model-viewer-dx12\model-viewer-dx12\Renderer\Model.h",
    "D:\projects\model-viewer-dx12\model-viewer-dx12\Types.h"
)
foreach ($f in $files) {
    if (Test-Path $f) {
        $content = [System.IO.File]::ReadAllText($f)
        [System.IO.File]::WriteAllText($f, $content, $utf8bom)
        Write-Host "Fixed: $f"
    }
}
