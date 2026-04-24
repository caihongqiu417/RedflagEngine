$targetPath = ".\assets\cache\shader\opengl"

if (Test-Path $targetPath) {
    $spvFiles = Get-ChildItem -Path $targetPath -Filter "*.spv" -File
    
    if ($spvFiles.Count -gt 0) {
        Write-Host "Found $($spvFiles.Count) spv files, deleting..."
        $spvFiles | ForEach-Object {
            Remove-Item $_.FullName -Force
            Write-Host "Deleted: $($_.Name)"
        }
        Write-Host "Done!"
    } else {
        Write-Host "No spv files found"
    }
} else {
    Write-Host "Path not found: $targetPath"
}

Write-Host "`nPress any key to exit..."
$null = $host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")