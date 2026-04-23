Get-ChildItem -Recurse -Filter "*.exr" | ForEach-Object {
    $png = $_.FullName -replace '\.exr$', '.png'
    magick convert $_.FullName $png
    Write-Host "converted: $($_.Name)"
}
