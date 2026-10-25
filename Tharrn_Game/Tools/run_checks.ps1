# Build, check asset loading, run the automation tests and the in-game milestone self-test.
#   powershell -File Tools/run_checks.ps1 [-Milestone M1] [-SkipBuild] [-NoShots] [-RebuildMap]
# Results: Saved/TharrnChecks/*.log, Saved/TharrnSelfTest/<Milestone>.json and screenshots.
# Never run while the editor has this project open.
param(
	[string]$Milestone = "M1",
	[switch]$SkipBuild,
	[switch]$NoShots,
	[switch]$RebuildMap,
	[string]$Map = "/Game/Tharrn/Maps/L_Slice_DeepCaverns",
	[string]$Engine = "C:\UE_5.8"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Project = (Get-ChildItem $Root -Filter *.uproject | Select-Object -First 1).FullName
$Target = [IO.Path]::GetFileNameWithoutExtension($Project) + "Editor"
$Out = Join-Path $Root "Saved\TharrnChecks"
New-Item -ItemType Directory -Force $Out | Out-Null
$EditorCmd = "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$failed = $false

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Output "UnrealEditor is running; close it first"; exit 2 }

if (-not $SkipBuild) {
	& "$Engine\Engine\Build\BatchFiles\Build.bat" $Target Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE *> "$Out\build.log"
	if ($LASTEXITCODE -ne 0) { Write-Output "BUILD FAILED (see $Out\build.log)"; exit 1 }
	Write-Output "build: ok ($Target)"
}

if ($RebuildMap) {
	& $EditorCmd $Project -run=pythonscript "-script=$PSScriptRoot\create_slice_map.py" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput *> "$Out\map.log"
	$saved = Select-String -Path "$Out\map.log" -Pattern "Tharrn: slice map saved" -Quiet
	Write-Output ("map: " + $(if ($saved) { "regenerated" } else { "NOT saved (see map.log)" }))
	if (-not $saved) { $failed = $true }
}

& $EditorCmd $Project -run=pythonscript "-script=$PSScriptRoot\check_assets.py" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput *> "$Out\assets.log"
$summary = Select-String -Path "$Out\assets.log" -Pattern "THARRN_ASSET_CHECK summary (.*)" | Select-Object -Last 1
$problems = Select-String -Path "$Out\assets.log" -Pattern "THARRN_ASSET_CHECK problem: (.*)|Failed to load Class|Failed to load '/Script" | ForEach-Object { $_.Line.Trim() } | Select-Object -Unique
Write-Output ("assets: " + $(if ($summary) { $summary.Matches[0].Groups[1].Value } else { "no summary (see assets.log)" }))
$problems | Select-Object -First 20 | ForEach-Object { Write-Output "  $_" }
if (-not $summary -or $problems) { $failed = $true }

& $EditorCmd $Project "-ExecCmds=Automation RunTests Tharrn; Quit" "-TestExit=Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -nosound -stdout -FullStdOutLogOutput *> "$Out\automation.log"
$results = Select-String -Path "$Out\automation.log" -Pattern "Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}" | ForEach-Object { "$($_.Matches[0].Groups[1].Value)  $($_.Matches[0].Groups[2].Value)" }
$results | ForEach-Object { Write-Output "automation: $_" }
if (-not $results -or ($results | Where-Object { $_ -notmatch "^Success" })) { $failed = $true }

if ($NoShots) { $shots = @("-nullrhi", "-TharrnNoShots") } else { $shots = @("-windowed", "-ResX=1600", "-ResY=900") }
& "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" $Project $Map -game "-TharrnSelfTest=$Milestone" -TharrnFresh -TharrnProfile=selftest -TharrnSlot=TharrnSelfTestRun -nosound -unattended -log "-abslog=$Out\selftest.log" @shots | Out-Null
$report = Join-Path $Root "Saved\TharrnSelfTest\$Milestone.json"
if (Test-Path $report) {
	$json = Get-Content $report -Raw | ConvertFrom-Json
	Write-Output ("selftest {0}: {1} passed, {2} failed" -f $Milestone, $json.passed, $json.failed)
	$json.checks | Where-Object { -not $_.pass } | ForEach-Object { Write-Output "  FAIL [$($_.step)] $($_.check): $($_.detail)" }
	if ($json.failed -gt 0) { $failed = $true }
} else {
	Write-Output "selftest: no report (see $Out\selftest.log)"
	$failed = $true
}
if ($failed) { exit 1 } else { exit 0 }
