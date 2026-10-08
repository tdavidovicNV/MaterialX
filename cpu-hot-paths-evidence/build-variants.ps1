param(
    [Parameter(Mandatory=$true)][string]$Source,
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$Source = (Resolve-Path $Source).Path
$evidence = $PSScriptRoot
$files = @('source/MaterialXGenShader/Syntax.cpp', 'source/MaterialXCore/Element.cpp',
           'source/MaterialXCore/Definition.cpp', 'source/MaterialXCore/Node.cpp', 'source/MaterialXCore/Interface.cpp')
$groups = @{ baseline = @(); reserved = @(0); targets = @(1); lookup = @(2,3); interfaces = @(4); combined = @(0,1,2,3,4) }
$final = @{}
$baseline = @{}
Push-Location $Source
try {
    if ((& git rev-parse HEAD) -ne '3806b06b7a7813f257945d563869b9dbc27bbddd') { throw 'Source must be the measured combined commit 3806b06b' }
    & git diff --quiet HEAD -- source
    if ($LASTEXITCODE) { throw 'Source has local changes; use a clean task worktree' }
    & $CMake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DMATERIALX_BUILD_TESTS=ON -DMATERIALX_BUILD_RENDER=OFF '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7' '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL' "-DCMAKE_PROJECT_MaterialX_INCLUDE=$evidence/harness.cmake"
    if ($LASTEXITCODE) { throw 'Configure failed' }
    foreach ($file in $files) {
        $final[$file] = [IO.File]::ReadAllText((Join-Path $Source $file))
        $lines = & git show "d0792106:$file"
        if ($LASTEXITCODE) { throw 'git show failed' }
        $baseline[$file] = ($lines -join "`n") + "`n"
    }
    foreach ($variant in @('baseline', 'reserved', 'targets', 'lookup', 'interfaces', 'combined')) {
        for ($i = 0; $i -lt $files.Count; ++$i) {
            $file = $files[$i]
            $content = if ($groups[$variant] -contains $i) { $final[$file] } else { $baseline[$file] }
            [IO.File]::WriteAllText((Join-Path $Source $file), $content)
        }
        $out = Join-Path $evidence $variant
        New-Item -ItemType Directory -Force $out | Out-Null
        & git diff d0792106 -- $files | Out-File "$out/source.patch" -Encoding utf8
        & $CMake --build build --config Release --parallel 12 --target CpuHotPaths MaterialXTest *> "$out/build.log"
        if ($LASTEXITCODE) { throw "Build failed: $variant" }
        Copy-Item build/bin/Release/CpuHotPaths.exe,build/bin/Release/CpuHotPaths.pdb $out
        & ./build/bin/Release/MaterialXTest.exe 'Target string matching,Active interface inheritance,Active document library interfaces,Qualified definition lookup,Qualified implementation lookup,GenShader: GLSL Syntax Check' *> "$out/tests.txt"
        if ($LASTEXITCODE) { throw "Tests failed: $variant" }
        & "$out/CpuHotPaths.exe" $Source dump "$out/shaders"
        if ($LASTEXITCODE) { throw "Shader generation failed: $variant" }
        foreach ($shader in Get-ChildItem "$out/shaders/*.glsl") {
            $baseHash = (Get-FileHash "$evidence/baseline/shaders/$($shader.Name)").Hash
            if ((Get-FileHash $shader.FullName).Hash -ne $baseHash) { throw "Shader mismatch: $variant $($shader.Name)" }
        }
        Get-FileHash "$out/CpuHotPaths.exe" | Format-List | Out-File "$out/binary-hash.txt"
        Write-Output "$variant built, focused tests passed, shaders byte-identical"
    }
} finally {
    foreach ($file in $files) {
        if ($final.ContainsKey($file)) { [IO.File]::WriteAllText((Join-Path $Source $file), $final[$file]) }
    }
    Pop-Location
}
