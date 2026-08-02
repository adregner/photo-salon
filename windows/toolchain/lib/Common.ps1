# Shared helpers for the Windows toolchain scripts. Dot-source, don't execute.
#
# PowerShell 5.1 compatible on purpose: that is what ships with Windows Server
# and Windows 11, so a fresh box needs nothing installed to run these.

Set-StrictMode -Version Latest

# Short work root. Qt's build tree nests deeply enough that a long path (a user
# profile directory, say) hits MAX_PATH and fails in confusing ways mid-build.
$script:WorkRoot = 'C:\pst'

function Get-ToolchainConfig {
    <#  Loads versions.psd1 and resolves the host toolchain paths it names,
        failing loudly if the pinned toolset or SDK is not installed. #>
    param([string] $Root = (Split-Path -Parent $PSScriptRoot))

    $cfg = Import-PowerShellDataFile (Join-Path $Root 'versions.psd1')

    $vsRoot = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*' `
                  -Directory -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -eq $cfg.MsvcToolset } |
              Select-Object -First 1
    if (-not $vsRoot) {
        $found = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*' `
                      -Directory -ErrorAction SilentlyContinue |
                  Select-Object -ExpandProperty Name) -join ', '
        throw ("MSVC toolset $($cfg.MsvcToolset) is not installed. Found: $found`n" +
               "Install it via the Visual Studio Installer (Individual components ->" +
               " 'MSVC v___ - VS C++ x64/x86 build tools'), or repin MsvcToolset in versions.psd1.")
    }
    $cfg.MsvcDir = $vsRoot.FullName
    # ...\VC\Tools\MSVC\<toolset>  ->  ...\VC\Auxiliary\Build\vcvars64.bat
    $cfg.VcvarsBat = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $vsRoot.FullName))) `
                               'Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $cfg.VcvarsBat)) { throw "vcvars64.bat not found at $($cfg.VcvarsBat)" }
    # vcvars wants major.minor, not the full three-part toolset number.
    $cfg.VcvarsVer = ($cfg.MsvcToolset -split '\.')[0..1] -join '.'

    $cfg.SdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
    if (-not (Test-Path (Join-Path $cfg.SdkRoot "Lib\$($cfg.WindowsSdk)"))) {
        $found = (Get-ChildItem (Join-Path $cfg.SdkRoot 'Lib') -Directory -ErrorAction SilentlyContinue |
                  Select-Object -ExpandProperty Name) -join ', '
        throw "Windows SDK $($cfg.WindowsSdk) is not installed. Found: $found"
    }

    # CMake and Ninja ship with Qt Creator; no separate install needed.
    $cfg.CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
    $cfg.Ninja = 'C:\Qt\Tools\Ninja\ninja.exe'
    foreach ($t in @($cfg.CMake, $cfg.Ninja)) {
        if (-not (Test-Path $t)) { throw "Required tool not found: $t (install Qt's CMake/Ninja components)" }
    }

    # windows/qt-6.11/x64 in the repo -- the major.minor in the directory name
    # means a Qt bump lands in a new directory instead of silently reusing the
    # old one, and it is what the cross-compile toolchain file points at.
    $cfg.QtDirName = 'qt-' + (($cfg.QtVersion -split '\.')[0..1] -join '.')

    $cfg.Work   = $script:WorkRoot
    $cfg.Src    = Join-Path $script:WorkRoot 'src'
    $cfg.Build  = Join-Path $script:WorkRoot 'build'
    $cfg.Stage  = Join-Path $script:WorkRoot 'stage'
    $cfg.Out    = Join-Path $script:WorkRoot 'out'
    $cfg.Logs   = Join-Path $script:WorkRoot 'logs'
    foreach ($d in @($cfg.Work, $cfg.Src, $cfg.Build, $cfg.Stage, $cfg.Out, $cfg.Logs)) {
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    }
    return $cfg
}

function Write-Step {
    param([string] $Message)
    Write-Host ("==> " + $Message) -ForegroundColor Cyan
}

function Invoke-Vcvars {
    <#  Runs a command line inside the pinned toolset's vcvars64 environment.

        Everything the bundle contains must be compiled by ONE toolset. Calling
        vcvars64 with -vcvars_ver pins both the compiler and the STL headers on
        INCLUDE; letting them drift apart is what produces "error STL1001:
        Unexpected compiler version".

        Output is written to -LogFile by cmd itself rather than through a
        PowerShell pipe, so a multi-hour build streams to disk and can be
        watched from another session. #>
    param(
        [Parameter(Mandatory)] $Config,
        [Parameter(Mandatory)] [string] $Command,
        [Parameter(Mandatory)] [string] $LogFile,
        [string] $WorkingDirectory = $PWD.Path
    )

    $bat = [System.IO.Path]::GetTempFileName() + '.cmd'
    $lines = @(
        '@echo off'
        "cd /d `"$WorkingDirectory`" || exit /b 1"
        "call `"$($Config.VcvarsBat)`" -vcvars_ver=$($Config.VcvarsVer) >nul || exit /b 1"
        # Qt's CMake and Ninja, ahead of anything else that might be on PATH.
        "set `"PATH=$(Split-Path -Parent $Config.CMake);$(Split-Path -Parent $Config.Ninja);%PATH%`""
        $Command
        'exit /b %ERRORLEVEL%'
    )
    Set-Content -Path $bat -Value $lines -Encoding ascii

    try {
        $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$bat`"" `
                 -NoNewWindow -Wait -PassThru `
                 -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"
        if (Test-Path "$LogFile.err") {
            Get-Content "$LogFile.err" | Add-Content $LogFile
            Remove-Item "$LogFile.err" -Force
        }
        return $p.ExitCode
    } finally {
        Remove-Item $bat -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-VcvarsChecked {
    param(
        [Parameter(Mandatory)] $Config,
        [Parameter(Mandatory)] [string] $Command,
        [Parameter(Mandatory)] [string] $LogFile,
        [string] $WorkingDirectory = $PWD.Path,
        [string] $What = 'command'
    )
    $code = Invoke-Vcvars -Config $Config -Command $Command -LogFile $LogFile `
                          -WorkingDirectory $WorkingDirectory
    if ($code -ne 0) {
        Write-Host "--- last 40 lines of $LogFile ---" -ForegroundColor Yellow
        Get-Content $LogFile -Tail 40 | ForEach-Object { Write-Host $_ }
        throw "$What failed (exit $code). Full log: $LogFile"
    }
}

function Invoke-Native {
    <#  Runs a native executable and checks its exit code.

        Windows PowerShell 5.1 wraps every stderr line from a native program in
        an ErrorRecord, so under $ErrorActionPreference = 'Stop' a program that
        merely chats on stderr -- git and robocopy both do -- aborts the script
        despite exiting 0. Exit code is the only reliable signal, so pin the
        preference to Continue for the call and judge by that. #>
    param(
        [Parameter(Mandatory)] [scriptblock] $Script,
        [string] $What = 'command',
        [switch] $AllowFailure
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out  = & $Script 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    if (-not $AllowFailure -and $code -ne 0) {
        $out | ForEach-Object { Write-Host $_ }
        throw "$What failed (exit $code)"
    }
    return $out
}

function Get-GitSource {
    <#  Clones a repo at a pinned tag into $Config.Src and verifies the commit
        matches versions.psd1. A moved tag is a silent supply-chain change, so
        it is an error rather than a warning. #>
    param(
        [Parameter(Mandatory)] $Config,
        [Parameter(Mandatory)] [hashtable] $Codec
    )
    $dir = Join-Path $Config.Src $Codec.Name
    if (-not (Test-Path $dir)) {
        Write-Step "Cloning $($Codec.Name) $($Codec.Tag)"
        Invoke-Native -What "git clone $($Codec.Name)" -Script {
            git clone --quiet --depth 1 --branch $Codec.Tag $Codec.Repo $dir
        } | Out-Null
    }
    $head = (Invoke-Native -What 'git rev-parse' -Script { git -C $dir rev-parse HEAD } |
             Select-Object -First 1).ToString().Trim()
    if ($Codec.Commit -and $head -ne $Codec.Commit) {
        throw ("$($Codec.Name) tag $($Codec.Tag) resolves to $head but versions.psd1 pins " +
               "$($Codec.Commit). The upstream tag moved -- review the change, then update " +
               "versions.psd1 deliberately.")
    }
    return @{ Dir = $dir; Commit = $head }
}

function Get-Sha256 {
    param([Parameter(Mandatory)] [string] $Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

function New-Tarball {
    <#  tar.exe on Windows 10+ is bsdtar. -C keeps the archive root at the
        directory name the fetch script extracts into, so the tarball's paths
        line up with windows/<name>/ in the repo. #>
    param(
        [Parameter(Mandatory)] [string] $SourceParent,
        [Parameter(Mandatory)] [string] $SourceName,
        [Parameter(Mandatory)] [string] $OutFile
    )
    if (Test-Path $OutFile) { Remove-Item $OutFile -Force }
    Invoke-Native -What "tar $SourceName" -Script {
        tar.exe -czf $OutFile -C $SourceParent $SourceName
    } | Out-Null
    return $OutFile
}

function Copy-Tree {
    param(
        [Parameter(Mandatory)] [string] $From,
        [Parameter(Mandatory)] [string] $To
    )
    if (-not (Test-Path $To)) { New-Item -ItemType Directory -Path $To -Force | Out-Null }
    # /MIR would delete; /E just merges. /NJH /NJS /NP /NFL /NDL keep the log quiet.
    # robocopy signals success with exit codes below 8 (0 = nothing to copy,
    # 1 = files copied, ...), so it needs its own check rather than -ne 0.
    Invoke-Native -AllowFailure -Script {
        robocopy $From $To /E /NJH /NJS /NP /NFL /NDL
    } | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $From -> $To failed with $LASTEXITCODE" }
    $global:LASTEXITCODE = 0
}
