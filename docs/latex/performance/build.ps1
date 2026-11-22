param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
    & python timing_table.py
    if ($LASTEXITCODE -ne 0) { throw '无法生成性能表，请检查上面指出的原始帧、回放记录或计时数据。' }
    & python compute_tables.py
    if ($LASTEXITCODE -ne 0) { throw '请核对 Sponza 的 RenderDoc 帧和回放结果。' }
    & python fine_mask_tables.py
    if ($LASTEXITCODE -ne 0) { throw '请核对 Sponza 镂空贴图的两份截帧和编辑器计时记录。' }
    & python ssaa_tables.py
    if ($LASTEXITCODE -ne 0) { throw '请核对 SSAA 截帧回放与同版本的编辑器计时。' }
    for ($pass = 0; $pass -lt 2; $pass++) {
        & xelatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
        if ($LASTEXITCODE -ne 0) { throw 'XeLaTeX 编译失败，请查看 build/main.log。' }
    }
    if (-not $BuildOnly) {
        $output = Join-Path $PSScriptRoot '../../../output/pdf/Redflag-Rendering-Performance.pdf'
        Copy-Item -LiteralPath 'build/main.pdf' -Destination $output -Force
        Write-Output "生成：$output"
    }
}
finally { Pop-Location }
