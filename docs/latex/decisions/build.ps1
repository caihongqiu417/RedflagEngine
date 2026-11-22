param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
    for ($pass = 0; $pass -lt 2; $pass++) {
        & xelatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
        if ($LASTEXITCODE -ne 0) { throw 'XeLaTeX 编译失败，请查看 build/main.log。' }
    }
    if (-not $BuildOnly) {
        $directory = Join-Path $PSScriptRoot '../../../output/pdf'
        $latest = Join-Path $directory 'Redflag-Technical-Decisions-Updated.pdf'
        Copy-Item -LiteralPath 'build/main.pdf' -Destination $latest -Force
        Write-Output "生成：$latest"

        # 保留原来的阅读链接。Windows 阅读器占用旧文件时，新版仍可从文档目录打开。
        $original = Join-Path $directory 'Redflag-Technical-Decisions.pdf'
        try { Copy-Item -LiteralPath 'build/main.pdf' -Destination $original -Force }
        catch [System.IO.IOException] {
            if (($_.Exception.HResult -band 0xffff) -notin 32, 33) { throw }
            Write-Output "阅读器正在使用 $original"
        }
    }
}
finally { Pop-Location }
