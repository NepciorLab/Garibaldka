param([string]$Out="Garibaldi.exe",[string]$Opt="-O2")
$ErrorActionPreference="Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

# C++ compiler: Zig as mingw-w64 (no Visual Studio / MinGW install needed): pip install ziglang
$zigCmd = $null
if(Get-Command zig -ErrorAction SilentlyContinue){ $zigCmd=@("zig") }
else { $zigCmd=@("python","-m","ziglang") }

function Invoke-Zig([string[]]$ZigArgs){
   $prev=$ErrorActionPreference; $ErrorActionPreference="Continue"
   & $zigCmd[0] @($zigCmd[1..($zigCmd.Length-1)] + $ZigArgs) 2>&1 | Where-Object { $_ -notmatch 'nullability|_Nullable|_Nonnull|^\s*\d+ \||^\s*\|' } | Out-Host
   $code=$LASTEXITCODE; $ErrorActionPreference=$prev
   if($code -ne 0){ throw "zig failed (exit $code): $ZigArgs" }
}

Invoke-Zig @("rc","res/app.rc","build/app_res.res")
Invoke-Zig @("rc","-I","res","res/cards.rc","build/cards_res.res")
Invoke-Zig @(
   "c++","-target","x86_64-windows-gnu","-std=c++17",$Opt,"-mwindows","-Wl,--subsystem,windows","-static",
   "-o","build/$Out","src/main.cpp","build/app_res.res","build/cards_res.res",
   "-ld2d1","-ldwrite","-lwindowscodecs","-ldsound","-lwinmm","-lole32","-lgdi32","-luser32","-lshell32","-luuid","-lwinhttp"
)
"OK build/$Out"
