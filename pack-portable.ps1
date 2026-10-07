param(
    [Parameter(Mandatory)] [string] $Runtime,
    [string] $Output = "",
    [string] $SevenZip = 'C:/Program Files/7-Zip'
)
$ErrorActionPreference = 'Stop'
if (!$Output) { $Output = Join-Path $PSScriptRoot '../outputs/Athanor-Alpha-1.1.1-Windows-x64.exe' }
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$outputPath = [IO.Path]::GetFullPath($Output)
$outputFolder = [IO.Path]::GetDirectoryName($outputPath)
New-Item -ItemType Directory -Path $outputFolder -Force | Out-Null
$stage = Join-Path $outputFolder ('.athanor-pack-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $bundle = Join-Path $stage 'Athanor'
    Copy-Item -LiteralPath $runtimePath -Destination $bundle -Recurse
    if (Test-Path -LiteralPath "$bundle/settings.json") { Remove-Item -LiteralPath "$bundle/settings.json" }
    Copy-Item -LiteralPath "$SevenZip/License.txt" -Destination "$bundle/licenses/7-Zip-License.txt"
    $files = @(Get-ChildItem -LiteralPath $bundle -Recurse -File)
    if (Get-ChildItem -LiteralPath $bundle -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw 'The runtime must contain real files, not links.'
    }
    $payload = Join-Path $stage 'payload.7z'
    Push-Location $stage
    try {
        & "$SevenZip/7z.exe" a -t7z -mx=5 -mmt=4 $payload 'Athanor' | Out-Null
        if ($LASTEXITCODE) { throw 'Runtime compression failed.' }
    } finally { Pop-Location }
    $stream = [IO.File]::OpenRead($payload)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $hash = -join ($hasher.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) }
    finally { $stream.Dispose(); $hasher.Dispose() }
    $lines = @('#pragma once', '#include <cstdint>', "constexpr wchar_t payloadHash[] = L`"$hash`";", 'struct BundleFile { const wchar_t *name; std::uint64_t bytes; };', 'constexpr BundleFile bundleFiles[] = {')
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
        $lines += "{L`"$relative`", $($file.Length)ULL},"
    }
    $lines += '};'
    [IO.File]::WriteAllLines("$stage/payload_manifest.h", $lines, [Text.UTF8Encoding]::new($false))
    $resources = @(
        ('1 ICON "' + "$PSScriptRoot/assets/athanor.ico".Replace('\','/') + '"')
        ('101 RCDATA "' + $payload.Replace('\','/') + '"')
        ('102 RCDATA "' + "$SevenZip/7z.exe".Replace('\','/') + '"')
        ('103 RCDATA "' + "$SevenZip/7z.dll".Replace('\','/') + '"')
    )
    [IO.File]::WriteAllLines("$stage/launcher.rc", $resources, [Text.UTF8Encoding]::new($false))
    & rc.exe /nologo /c65001 "/fo$stage/launcher.res" "$stage/launcher.rc"
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    & cl.exe /nologo /O2 /MT /EHsc /std:c++20 /utf-8 "/I$stage" "/Fo$stage/launcher.obj" "/Fe$outputPath" "$PSScriptRoot/launcher/launcher.cpp" "$stage/launcher.res" /link /SUBSYSTEM:WINDOWS kernel32.lib user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib dwmapi.lib
    if ($LASTEXITCODE) { throw 'Portable launcher compilation failed.' }
    & "$SevenZip/7z.exe" t $payload | Out-Null
    if ($LASTEXITCODE) { throw 'Runtime archive verification failed.' }
    Write-Output $outputPath
} finally {
    $checked = [IO.Path]::GetFullPath($stage)
    if ([IO.Path]::GetDirectoryName($checked) -ne $outputFolder -or ![IO.Path]::GetFileName($checked).StartsWith('.athanor-pack-')) { throw 'Invalid packaging cleanup path.' }
    Remove-Item -LiteralPath $checked -Recurse -Force
}
