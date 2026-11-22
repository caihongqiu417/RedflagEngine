param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    for ($pass = 0; $pass -lt 2; $pass++) {
        & xelatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
        if ($LASTEXITCODE) { throw '编译失败，请查看 build/main.log。' }
    }
    if (!$BuildOnly) {
        Copy-Item -LiteralPath build/main.pdf -Destination '../../../output/pdf/Redflag-Editor-Guide.pdf' -Force
    }
}
finally { Pop-Location }
