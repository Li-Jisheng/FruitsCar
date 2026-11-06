# 证据包的机器检查（任务书 4.5）：manifest 解析、
# 必填字段、SUMMARY 一致性、产物存在且非空、SUMMARY 位于最后。
param(
    [string]$Module,
    [switch]$All,
    [switch]$Quiet,
    [int]$Phase = 1
)
. (Join-Path $PSScriptRoot 'common.ps1')

$moduleMap = if ($Phase -eq 3) { $script:Phase3ModuleDirectoryMap } elseif ($Phase -eq 2) { $script:Phase2ModuleDirectoryMap } else { $script:ModuleDirectoryMap }

$failures = New-Object System.Collections.Generic.List[string]
$checkedModules = New-Object System.Collections.Generic.List[string]

function Fail {
    param([string]$Message)
    $failures.Add($Message) | Out-Null
    if (-not $Quiet) { Write-Output ('  FAIL ' + $Message) }
}

function Test-EvidenceDirectory {
    param([string]$ModuleId, [string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory)) {
        Fail ("$ModuleId directory missing: $Directory")
        return
    }
    $manifestPath = Join-Path $Directory 'run_manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        Fail ("$ModuleId manifest missing: $manifestPath")
        return
    }
    try {
        $manifest = Read-JsonFile $manifestPath
    } catch {
        Fail ("$ModuleId manifest is not valid JSON: " + $_.Exception.Message)
        return
    }

    $requiredFields = @('module', 'title', 'generatedAt', 'operator', 'commit', 'buildVersion',
        'toolchain', 'command', 'seed', 'levelId', 'environment', 'result',
        'caseCount', 'passCount', 'failCount', 'artifacts', 'notes')
    foreach ($field in $requiredFields) {
        $value = $manifest.$field
        if ($null -eq $value -or [string]$value -eq '') {
            Fail ("$ModuleId manifest field missing or empty: $field")
        }
    }
    if ([string]$manifest.module -ne $ModuleId) {
        Fail ("$ModuleId manifest.module mismatch: " + $manifest.module)
    }

    $artifacts = @($manifest.artifacts)
    if ($artifacts.Count -eq 0) {
        Fail ("$ModuleId artifacts list is empty")
    }
    foreach ($artifact in $artifacts) {
        $path = Join-Path $Directory ([string]$artifact)
        if (-not (Test-Path -LiteralPath $path)) {
            Fail ("$ModuleId artifact missing: " + $artifact)
            continue
        }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -le 0) {
            Fail ("$ModuleId artifact is empty: " + $artifact)
        }
    }

    foreach ($artifact in $artifacts) {
        if ([string]$artifact -like 'TC-*.log' -or [string]$artifact -eq 'run_all.log') {
            $path = Join-Path $Directory ([string]$artifact)
            if (-not (Test-Path -LiteralPath $path)) { continue }
            $lines = @(Get-Content -LiteralPath $path | Where-Object { $_.Trim().Length -gt 0 })
            if ($lines.Count -eq 0) {
                Fail ("$ModuleId log has no lines: " + $artifact)
                continue
            }
            $isMainLog = ([string]$artifact -eq 'run_all.log')
            $badLines = 0
            foreach ($line in $lines) {
                try { $null = $line | ConvertFrom-Json } catch { $badLines++ }
            }
            if ($badLines -gt 0) {
                Fail ("$ModuleId has $badLines non-JSON log lines in " + $artifact)
            }
            if (-not $isMainLog) { continue }
            $last = $lines[$lines.Count - 1]
            try {
                $lastObject = $last | ConvertFrom-Json
            } catch {
                Fail ("$ModuleId last log line is not JSON: " + $artifact)
                continue
            }
            if ($lastObject.kind -ne 'SUMMARY') {
                Fail ("$ModuleId log does not end with SUMMARY: " + $artifact)
            } else {
                if ([string]$lastObject.result -ne [string]$manifest.result) {
                    Fail ("$ModuleId manifest.result=" + $manifest.result + ' but log SUMMARY result=' + $lastObject.result)
                }
                if ($manifest.result -eq 'PASS' -and ([int]$lastObject.fail -ne 0 -or [int]$lastObject.skipped -ne 0)) {
                    Fail ("$ModuleId PASS manifest but SUMMARY fail/skipped is non-zero")
                }
            }
        }
    }

    if (-not $Quiet) { Write-Output ('  OK   ' + $ModuleId + ' (' + $artifacts.Count + ' artifacts)') }
}

if ($All) {
    foreach ($key in $moduleMap.Keys) {
        $checkedModules.Add($key) | Out-Null
    }
} elseif ($Module) {
    if (-not $moduleMap.Contains($Module)) {
        Write-Output ('EVIDENCE_INVALID unknown module ' + $Module)
        exit 2
    }
    $checkedModules.Add($Module) | Out-Null
} else {
    Write-Output 'usage: verify_evidence.ps1 -Module M5 | -All [-Phase 1|2]'
    exit 2
}

foreach ($moduleId in $checkedModules) {
    $directory = Get-ModuleEvidenceDirectory -Module $moduleId -Phase $Phase
    if (-not $Quiet) { Write-Output ('CHECK ' + $moduleId + ' ' + $directory) }
    Test-EvidenceDirectory -ModuleId $moduleId -Directory $directory
}

if ($failures.Count -gt 0) {
    Write-Output ('EVIDENCE_INVALID count=' + $failures.Count)
    foreach ($failure in $failures) { Write-Output (' - ' + $failure) }
    exit 1
}
Write-Output ('EVIDENCE_OK modules=' + $checkedModules.Count)
exit 0
