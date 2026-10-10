[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Compo')][string]$Configuration = 'Debug',
    [switch]$VerifyTools,
    # Runs the Compo shader-minification pass for a Debug build too, so a change
    # to the minifier flags can be compared frame-by-frame with the capture
    # options in src/intro.c rather than only by eye on the Compo build.
    [switch]$MinifyShaders,
    # Debug build only: compiles with the Compo optimization flags instead of
    # /Od, so the synthesized ocean.wav is what the Compo build really plays.
    # Optimized code can round floating point differently from unoptimized code.
    [switch]$Optimize,
    [ValidateSet('', '1920x1080', '1280x720')][string]$Resolution = '',
    [switch]$Run,
    # Compo build only: also copies the verified executable to release/ so the
    # shippable binary can be committed and carried to the party. Without
    # -Resolution it builds all three entries: plankton.exe (desktop resolution)
    # and the two fixed-resolution executables.
    [switch]$Release
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSCommandPath
$toolLock = Get-Content -Raw (Join-Path $repoRoot 'tools.lock.json') | ConvertFrom-Json

function Fail([string]$Message) { throw "in4k build: $Message" }
function Get-VsWherePath {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $candidate)) { Fail 'Visual Studio Build Tools 2026 is not installed. Install its C++ Build Tools workload; no IDE is required.' }
    $candidate
}
function Get-BuildToolsInstallation {
    $json = & (Get-VsWherePath) -latest -products Microsoft.VisualStudio.Product.BuildTools -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if (!$json) { Fail "Visual Studio Build Tools $($toolLock.tools.buildTools.release) with MSVC x64/x86 tools was not found." }
    $instance = $json | ConvertFrom-Json
    if ($instance.catalog.productDisplayVersion -ne $toolLock.tools.buildTools.release) { Fail "Build Tools mismatch: expected $($toolLock.tools.buildTools.release), found $($instance.catalog.productDisplayVersion)." }
    $instance
}
function Import-MsvcEnvironment($Installation) {
    $devCmd = Join-Path $Installation.installationPath 'Common7\Tools\VsDevCmd.bat'
    if (!(Test-Path -LiteralPath $devCmd)) { Fail "MSVC environment script was not found at $devCmd." }
    $commandLine = 'call "{0}" -arch=x86 -host_arch=x64 >nul && set' -f $devCmd
    $lines = & cmd.exe /d /c $commandLine
    if ($LASTEXITCODE -ne 0) { Fail 'Unable to initialize the MSVC x86 command-line environment.' }
    foreach ($line in $lines) { $at = $line.IndexOf('='); if ($at -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $at), $line.Substring($at + 1), 'Process') } }
}
function Get-CrinklerPath { if ($env:IN4K_CRINKLER) { $env:IN4K_CRINKLER } else { Join-Path $repoRoot $toolLock.tools.crinkler.relativePath } }
function Get-ShaderMinifierPath { if ($env:IN4K_SHADER_MINIFIER) { $env:IN4K_SHADER_MINIFIER } else { Join-Path $repoRoot $toolLock.tools.shaderMinifier.relativePath } }
function Test-WindowsSdkRelease {
    $uninstallPaths = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*')
    $expectedName = "Windows Software Development Kit - Windows $($toolLock.tools.windowsSdk.release)"
    $installed = Get-ItemProperty $uninstallPaths -ErrorAction SilentlyContinue | Where-Object {
        $_.PSObject.Properties['DisplayName'] -and $_.DisplayName -eq $expectedName -and $_.DisplayVersion -eq $toolLock.tools.windowsSdk.installedVersion
    }
    if (!$installed) { Fail "Windows SDK release mismatch: expected $expectedName ($($toolLock.tools.windowsSdk.installedVersion))." }
}
function Get-Toolchain([bool]$RequireCrinkler) {
    $installation = Get-BuildToolsInstallation; Import-MsvcEnvironment $installation
    $msvcVersion = $env:VCToolsVersion.TrimEnd('\\'); $sdkVersion = $env:WindowsSDKVersion.TrimEnd('\\')
    if ($msvcVersion -ne $toolLock.tools.msvc.version) { Fail "MSVC mismatch: expected $($toolLock.tools.msvc.version), found $msvcVersion." }
    $compilerVersion = (Get-Item (Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x86\cl.exe')).VersionInfo.FileVersion
    if ($compilerVersion -ne $toolLock.tools.msvc.compilerFileVersion) { Fail "MSVC compiler mismatch: expected $($toolLock.tools.msvc.compilerFileVersion), found $compilerVersion." }
    if ($sdkVersion -ne $toolLock.tools.windowsSdk.toolchainVersion) { Fail "Windows SDK toolchain mismatch: expected $($toolLock.tools.windowsSdk.toolchainVersion), found $sdkVersion." }
    Test-WindowsSdkRelease
    if ($PSVersionTable.PSVersion.ToString() -ne $toolLock.tools.powerShell.version) { Fail "PowerShell mismatch: expected $($toolLock.tools.powerShell.version), found $($PSVersionTable.PSVersion)." }
    $crinkler = Get-CrinklerPath
    if ($RequireCrinkler) {
        if (!(Test-Path -LiteralPath $crinkler)) { Fail "Crinkler $($toolLock.tools.crinkler.version) was not found at $crinkler. Download it into .tools or set IN4K_CRINKLER." }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $crinkler).Hash
        if ($hash -ne $toolLock.tools.crinkler.sha256) { Fail "Crinkler hash mismatch: expected $($toolLock.tools.crinkler.sha256), found $hash." }
        $versionText = (& $crinkler 2>&1 | Select-Object -First 1)
        if ($versionText -notmatch "Crinkler $([regex]::Escape($toolLock.tools.crinkler.version))") { Fail "Crinkler version mismatch: expected $($toolLock.tools.crinkler.version), found '$versionText'." }
        $shaderMinifier = Get-ShaderMinifierPath
        if (!(Test-Path -LiteralPath $shaderMinifier)) { Fail "Shader Minifier $($toolLock.tools.shaderMinifier.version) was not found at $shaderMinifier." }
        $shaderHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $shaderMinifier).Hash
        if ($shaderHash -ne $toolLock.tools.shaderMinifier.sha256) { Fail "Shader Minifier hash mismatch: expected $($toolLock.tools.shaderMinifier.sha256), found $shaderHash." }
        $shaderVersion = (& $shaderMinifier --version 2>&1 | Select-Object -First 1)
        if ($shaderVersion -notmatch [regex]::Escape($toolLock.tools.shaderMinifier.version)) { Fail "Shader Minifier version mismatch: expected $($toolLock.tools.shaderMinifier.version), found '$shaderVersion'." }
    }
    [ordered]@{ buildTools = $installation.catalog.productDisplayVersion; buildToolsInstallationVersion = $installation.installationVersion; msvc = $msvcVersion; compilerFileVersion = $compilerVersion; windowsSdkRelease = $toolLock.tools.windowsSdk.release; windowsSdkToolchain = $sdkVersion; powerShell = $PSVersionTable.PSVersion.ToString(); crinkler = if ($RequireCrinkler) { $crinkler } else { $null }; shaderMinifier = if ($RequireCrinkler) { $shaderMinifier } else { $null } }
}
function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    Write-Host "> $FilePath $($Arguments -join ' ')"
    $output = & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "Command failed with exit code ${LASTEXITCODE}: $FilePath" }
    # Echoed explicitly rather than left to flow through: cl.exe writes each
    # source filename to stdout even under /nologo, which would otherwise leak
    # into any pipeline a caller builds around this function (as it did when
    # $objects collected an /nologo-invisible "intro.c" alongside its object path).
    if ($output) { $output | ForEach-Object { Write-Host $_ } }
}
function Write-ToolchainReport($Toolchain, [string]$OutputDirectory) { $Toolchain | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'toolchain.json') }
function ConvertTo-CString([string]$Text) {
    $lines = ($Text -replace "`r`n", "`n" -replace "`r", "`n") -split "`n"
    (($lines | ForEach-Object { '"' + $_.Replace('\', '\\').Replace('"', '\"') + '\n"' }) -join [Environment]::NewLine)
}
$shaders = @(
    @{ file = 'particles.comp.glsl'; symbol = 'particles_compute_shader' }
    @{ file = 'particles.vert.glsl'; symbol = 'particles_vertex_shader' }
    @{ file = 'particles.frag.glsl'; symbol = 'particles_fragment_shader' }
    @{ file = 'background.frag.glsl'; symbol = 'background_fragment_shader' }
)
function Invoke-ShaderGeneration([bool]$Minify, [string]$OutputDirectory, $Toolchain) {
    $shaderDirectory = Join-Path $OutputDirectory 'generated'
    New-Item -ItemType Directory -Force -Path $shaderDirectory | Out-Null
    $declarations = foreach ($shader in $shaders) {
        $source = Join-Path $repoRoot (Join-Path 'src\shaders' $shader.file)
        $shaderInput = $source
        if ($Minify) {
            $shaderInput = Join-Path $shaderDirectory $shader.file
            Invoke-Native $Toolchain.shaderMinifier @('--format', 'text', '--preserve-externals', '--field-names', 'stpq', '-o', $shaderInput, $source)
        }
        "static const char* $($shader.symbol) =`r`n$(ConvertTo-CString (Get-Content -Raw $shaderInput));"
    }
    $header = @"
#ifndef IN4K_SHADERS_H
#define IN4K_SHADERS_H
$($declarations -join [Environment]::NewLine)
#endif
"@
    Set-Content -Encoding utf8 (Join-Path $shaderDirectory 'shaders.h') $header
    return $shaderDirectory
}

if ($Release -or $Resolution) { $Configuration = 'Compo' }
$requireCrinkler = $VerifyTools -or $MinifyShaders -or $Configuration -eq 'Compo'; $toolchain = Get-Toolchain $requireCrinkler
if ($VerifyTools) { $toolchain | ConvertTo-Json; if (!$PSBoundParameters.ContainsKey('Configuration')) { exit 0 } }
$variant = if ($Resolution) { "Compo-$Resolution" } else { $Configuration }
$exeName = if ($Resolution) { "plankton_$Resolution.exe" } else { 'plankton.exe' }
$outputDirectory = Join-Path $repoRoot (Join-Path 'out' $variant); $objectDirectory = Join-Path $outputDirectory 'obj'
New-Item -ItemType Directory -Force -Path $objectDirectory | Out-Null
$generatedDirectory = Invoke-ShaderGeneration ($MinifyShaders -or $Configuration -eq 'Compo') $outputDirectory $toolchain
# /Oy- keeps a frame pointer, which makes the many float spills in the audio code one
# byte shorter each; /Ob0 stops inlining, which the size of the audio code does not
# survive. Together they are 75 bytes smaller than /O1 /Os alone.
$optimizationFlags = @('/O1', '/Os', '/Oy-', '/Ob0')
$sourceFiles = @('intro.c', 'audio.c'); $executable = Join-Path $outputDirectory $exeName
$objects = foreach ($sourceFile in $sourceFiles) {
    $source = Join-Path $repoRoot (Join-Path 'src' $sourceFile)
    $object = Join-Path $objectDirectory ($sourceFile -replace '\.c$', '.obj')
    $compileArguments = @('/nologo', '/c', '/TC', '/Zl', '/GS-', '/W4', "/I$generatedDirectory", "/Fo$object", $source)
    if ($Configuration -eq 'Debug') { $compileArguments += @('/Z7', '/DDEBUG') + $(if ($Optimize) { $optimizationFlags } else { @('/Od') }) } else { $compileArguments += $optimizationFlags + @('/DWIN32', '/DCOMPO'); if ($Resolution) { $compileArguments += @("/DCOMPO_WIDTH=$($Resolution.Split('x')[0])", "/DCOMPO_HEIGHT=$($Resolution.Split('x')[1])") } }
    Invoke-Native 'cl.exe' $compileArguments
    $object
}
$libraries = @('user32.lib', 'gdi32.lib', 'opengl32.lib', 'winmm.lib', 'kernel32.lib')
if ($Configuration -eq 'Debug') {
    Invoke-Native 'link.exe' (@('/nologo', '/subsystem:windows', '/entry:WinMainCRTStartup', '/nodefaultlib', "/out:$executable") + $objects + $libraries)
} else {
    $report = Join-Path $outputDirectory 'crinkler-report.html'
    Invoke-Native $toolchain.crinkler (@('/CRINKLER', '/SUBSYSTEM:WINDOWS', '/ENTRY:WinMainCRTStartup', '/NODEFAULTLIB', '/COMPMODE:VERYSLOW', '/SATURATE', '/TRUNCATEFLOATS:16', '/TINYIMPORT', '/ORDERTRIES:10000', "/OUT:$executable", "/REPORT:$report") + $objects + $libraries)
    $bytes = (Get-Item -LiteralPath $executable).Length
    [ordered]@{ executable = $exeName; bytes = $bytes; limit = 4096; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outputDirectory 'size.json')
    Write-Host "Compo size: $bytes / 4096 bytes"; if ($bytes -gt 4096) { Fail "Compo executable exceeds the 4096-byte limit by $($bytes - 4096) bytes." }
}
Write-ToolchainReport $toolchain $outputDirectory; Write-Host "Built $executable"
if ($Release) {
    $releaseDirectory = Join-Path $repoRoot 'release'
    New-Item -ItemType Directory -Force -Path $releaseDirectory | Out-Null
    Copy-Item -LiteralPath $executable -Destination $releaseDirectory -Force
    Write-Host "Released $(Join-Path $releaseDirectory $exeName)"
    if (!$Resolution) { foreach ($size in '1920x1080', '1280x720') { & $PSCommandPath -Release -Resolution $size } }
}
if ($Run) { Start-Process -FilePath $executable }
