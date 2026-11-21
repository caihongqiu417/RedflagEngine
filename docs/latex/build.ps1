$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
    for ($pass = 0; $pass -lt 2; $pass++) {
        & xelatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
        if ($LASTEXITCODE -ne 0) { throw 'XeLaTeX 编译失败，请查看 build/main.log。' }
    }
    $output = Join-Path $PSScriptRoot '../../output/pdf/Redflag-Architecture-LaTeX.pdf'
    Copy-Item -LiteralPath 'build/main.pdf' -Destination $output -Force
    Write-Output "生成：$output"
}
finally { Pop-Location }
