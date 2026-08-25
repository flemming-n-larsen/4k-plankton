[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Compo')][string]$Configuration = 'Debug',
    [switch]$VerifyTools,
    [switch]$Run
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
    Write-Host "> $FilePath $($Arguments -join ' ')"; & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "Command failed with exit code ${LASTEXITCODE}: $FilePath" }
}
function Write-ToolchainReport($Toolchain, [string]$OutputDirectory) { $Toolchain | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'toolchain.json') }
function ConvertTo-CString([string]$Text) {
    $lines = ($Text -replace "`r`n", "`n" -replace "`r", "`n") -split "`n"
    (($lines | ForEach-Object { '"' + $_.Replace('\', '\\').Replace('"', '\"') + '\n"' }) -join [Environment]::NewLine)
}
function Invoke-ShaderGeneration([string]$Configuration, [string]$OutputDirectory, $Toolchain) {
    $shaderDirectory = Join-Path $OutputDirectory 'generated'
    New-Item -ItemType Directory -Force -Path $shaderDirectory | Out-Null
    $vertexSource = Join-Path $repoRoot 'src\shaders\triangle.vert.glsl'
    $fragmentSource = Join-Path $repoRoot 'src\shaders\triangle.frag.glsl'
    $vertexInput = $vertexSource; $fragmentInput = $fragmentSource
    if ($Configuration -eq 'Compo') {
        $vertexInput = Join-Path $shaderDirectory 'triangle.vert.glsl'
        $fragmentInput = Join-Path $shaderDirectory 'triangle.frag.glsl'
        Invoke-Native $Toolchain.shaderMinifier @('--format', 'text', '--preserve-externals', '-o', $vertexInput, $vertexSource)
        Invoke-Native $Toolchain.shaderMinifier @('--format', 'text', '--preserve-externals', '-o', $fragmentInput, $fragmentSource)
    }
    $header = @"
#ifndef IN4K_SHADERS_H
#define IN4K_SHADERS_H
static const char* triangle_vertex_shader =
$(ConvertTo-CString (Get-Content -Raw $vertexInput));
static const char* triangle_fragment_shader =
$(ConvertTo-CString (Get-Content -Raw $fragmentInput));
#endif
"@
    Set-Content -Encoding utf8 (Join-Path $shaderDirectory 'shaders.h') $header
    return $shaderDirectory
}

$requireCrinkler = $VerifyTools -or $Configuration -eq 'Compo'; $toolchain = Get-Toolchain $requireCrinkler
if ($VerifyTools) { $toolchain | ConvertTo-Json; if (!$PSBoundParameters.ContainsKey('Configuration')) { exit 0 } }
$outputDirectory = Join-Path $repoRoot (Join-Path 'out' $Configuration); $objectDirectory = Join-Path $outputDirectory 'obj'
New-Item -ItemType Directory -Force -Path $objectDirectory | Out-Null
$generatedDirectory = Invoke-ShaderGeneration $Configuration $outputDirectory $toolchain
$source = Join-Path $repoRoot 'src\rectangle.c'; $object = Join-Path $objectDirectory 'rectangle.obj'; $executable = Join-Path $outputDirectory 'in4k-rectangle.exe'
$compileArguments = @('/nologo', '/c', '/TC', '/Zl', '/GS-', '/W4', "/I$generatedDirectory", "/Fo$object", $source)
if ($Configuration -eq 'Debug') { $compileArguments += @('/Od', '/Z7', '/DDEBUG') } else { $compileArguments += @('/O1', '/Os', '/DWIN32', '/DCOMPO') }
Invoke-Native 'cl.exe' $compileArguments
$libraries = @('user32.lib', 'gdi32.lib', 'opengl32.lib', 'kernel32.lib')
if ($Configuration -eq 'Debug') {
    Invoke-Native 'link.exe' (@('/nologo', '/subsystem:windows', '/entry:WinMainCRTStartup', '/nodefaultlib', "/out:$executable", $object) + $libraries)
} else {
    $report = Join-Path $outputDirectory 'crinkler-report.html'
    Invoke-Native $toolchain.crinkler (@('/CRINKLER', '/SUBSYSTEM:WINDOWS', '/ENTRY:WinMainCRTStartup', '/NODEFAULTLIB', '/COMPMODE:VERYSLOW', "/OUT:$executable", "/REPORT:$report", $object) + $libraries)
    $bytes = (Get-Item -LiteralPath $executable).Length
    [ordered]@{ executable = 'in4k-rectangle.exe'; bytes = $bytes; limit = 4096; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash } | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outputDirectory 'size.json')
    Write-Host "Compo size: $bytes / 4096 bytes"; if ($bytes -gt 4096) { Fail "Compo executable exceeds the 4096-byte limit by $($bytes - 4096) bytes." }
}
Write-ToolchainReport $toolchain $outputDirectory; Write-Host "Built $executable"
if ($Run) { Start-Process -FilePath $executable }
