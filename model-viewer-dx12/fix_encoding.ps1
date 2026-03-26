$files = @(
    "d:\projects\model-viewer-dx12\model-viewer-dx12\Application.cpp",
    "d:\projects\model-viewer-dx12\model-viewer-dx12\Application.h"
)

foreach ($f in $files) {
    if (Test-Path $f) {
        $content = Get-Content $f -Raw
        [System.IO.File]::WriteAllText($f, $content, [System.Text.Encoding]::UTF8)
        Write-Host "Converted $f to UTF-8 BOM"
    }
}
