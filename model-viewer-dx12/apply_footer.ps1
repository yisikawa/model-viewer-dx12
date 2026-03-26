$path = "d:\projects\model-viewer-dx12\model-viewer-dx12\Application.cpp"
$footerPath = "d:\projects\model-viewer-dx12\model-viewer-dx12\correct_footer.txt"

if (Test-Path $path) {
    $lines = Get-Content $path
    # 1009行目までを取得 (0-based index 0 to 1008)
    $header = $lines[0..1008]
    $footer = Get-Content $footerPath -Raw
    
    # 全部結合して書き出す
    $final = ($header -join "`r`n") + "`r`n" + $footer
    [System.IO.File]::WriteAllText($path, $final, [System.Text.Encoding]::UTF8)
    Write-Host "Successfully reconstructed $path"
}
