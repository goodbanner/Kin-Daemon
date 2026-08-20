$ErrorActionPreference = "Stop"

$daemonRoot = Split-Path -Parent $PSScriptRoot
$appRoot = Join-Path (Split-Path -Parent $daemonRoot) "Kin-App\app-src\app\src\main"

$checks = @(
    @{ Path = Join-Path $daemonRoot "include\scheduler.hpp"; Pattern = 'ThreadRules|ThreadPriorityRules|threadAllocTask|Config::ThreadAlloc' },
    @{ Path = Join-Path $daemonRoot "include\Config.hpp"; Pattern = 'namespace ThreadAlloc' },
    @{ Path = Join-Path $daemonRoot "include\JsonConfig.hpp"; Pattern = 'ThreadAlloc::|F\["ThreadAlloc"\]' },
    @{ Path = Join-Path $appRoot "java"; Pattern = 'ThreadAlloc|ThreadRule|ThreadSampler|ThreadLoadScreen|OverlayType\.THREAD' },
    @{ Path = Join-Path $appRoot "AndroidManifest.xml"; Pattern = 'ThreadRuleService' }
)

$violations = foreach ($check in $checks) {
    $items = if (Test-Path $check.Path -PathType Container) {
        Get-ChildItem $check.Path -Recurse -File
    } else {
        Get-Item $check.Path
    }
    $items | Select-String -Pattern $check.Pattern | ForEach-Object {
        "{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()
    }
}

if ($violations) {
    Write-Error ("Removed thread feature still has references:`n" + ($violations -join "`n"))
}

Write-Host "PASS: removed thread feature has no App or Daemon references"
