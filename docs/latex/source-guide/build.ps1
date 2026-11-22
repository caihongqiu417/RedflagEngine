$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    foreach ($pass in 1..2) {
        & xelatex -halt-on-error -interaction=batchmode -output-directory=build main.tex
        if ($LASTEXITCODE) { throw '请检查 build/main.log 中的 XeLaTeX 错误。' }
    }
    Copy-Item -LiteralPath build/main.pdf -Destination ../../../output/pdf/Redflag-Source-Guide.pdf -Force
}
finally { Pop-Location }
