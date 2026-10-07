param(
    [Parameter(Mandatory)] [string] $QtRoot,
    [Parameter(Mandatory)] [string] $CodecRoot,
    [Parameter(Mandatory)] [string] $RuntimeRoot,
    [string] $Output = "$PSScriptRoot/../outputs/Athanor",
    [string] $Build = "$PSScriptRoot/.build"
)
$ErrorActionPreference = 'Stop'
$qtPath = (Resolve-Path -LiteralPath $QtRoot).Path
$codecPath = (Resolve-Path -LiteralPath $CodecRoot).Path
$runtimePath = (Resolve-Path -LiteralPath $RuntimeRoot).Path
if ([IO.Path]::GetFullPath($Output).TrimEnd('\') -eq $runtimePath.TrimEnd('\')) {
    throw 'Output must be separate from the dependency runtime folder.'
}
& cmake -S $PSScriptRoot -B $Build "-DCMAKE_PREFIX_PATH=$qtPath" "-DATHANOR_CODEC_ROOT=$codecPath"
if ($LASTEXITCODE) { throw 'Configuration failed.' }
& cmake --build $Build --config Release
if ($LASTEXITCODE) { throw 'Build failed.' }
New-Item -ItemType Directory -Path $Output -Force | Out-Null
& cmake --install $Build --config Release --prefix $Output
if ($LASTEXITCODE) { throw 'Installation failed.' }
& "$qtPath/bin/windeployqt.exe" --release --no-translations --no-compiler-runtime --qmldir "$PSScriptRoot/native" "$Output/Athanor.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
foreach ($folder in @('tools', 'licenses')) {
    Copy-Item -LiteralPath "$runtimePath/$folder" -Destination $Output -Recurse -Force
}
Copy-Item -LiteralPath "$PSScriptRoot/assets" -Destination $Output -Recurse -Force
Get-ChildItem -LiteralPath $runtimePath -Filter '*.dll' -File | Copy-Item -Destination $Output -Force
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath "$runtimePath/$name" -Destination "$Output/$name" -Force
}
if (!(Test-Path -LiteralPath "$Output/settings.json")) {
    Copy-Item -LiteralPath "$runtimePath/settings.json" -Destination "$Output/settings.json"
}

Copy-Item -Path "$PSScriptRoot/licenses/*" -Destination "$Output/licenses" -Recurse -Force
Copy-Item -LiteralPath "$PSScriptRoot/THIRD-PARTY-NOTICES.md" -Destination "$Output/THIRD-PARTY-NOTICES.md" -Force
