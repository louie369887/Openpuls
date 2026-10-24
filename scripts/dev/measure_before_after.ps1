# 屏蔽 3 个 WPS 右键扩展，测量前后差异，然后恢复
$exe = 'F:\openpuls\build\OpenPulsShellOpt.exe'
$sample = Join-Path $env:LOCALAPPDATA 'OpenPuls\bench_sample.txt'
if (-not (Test-Path $sample)) {
    New-Item -ItemType Directory (Split-Path $sample) -Force | Out-Null
    Set-Content -Path $sample -Value 'openpuls bench sample'
}
$blocked = @{
    '{67F4D210-BFC2-4ADD-9A2A-C9B9E1F42C4F}' = 'qingshellext'
    '{AF1D7D2F-6AE8-4BAA-ABFA-738201F4871C}' = 'kwpsshellext'
    '{AA147FFB-0B1F-4BB1-9B1E-8D062B35C146}' = 'QingNseContextMenu'
}

function Measure-Menu([string]$label) {
    $out = Join-Path $env:TEMP 'bm2.txt'
    Remove-Item $out -ErrorAction SilentlyContinue
    Start-Process -FilePath $exe -ArgumentList @('--bench-menu', 'file', $sample, '5', $out) -Wait -WindowStyle Hidden | Out-Null
    $txt = Get-Content $out -Raw -ErrorAction SilentlyContinue
    $runs = ($txt -split "`n") | Where-Object { $_ -match '^(run|items)' }
    Write-Output ($label.PadRight(10) + ' | ' + ($runs -join '  '))
}

Measure-Menu 'BEFORE'

$bk = 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked'
if (-not (Test-Path -LiteralPath $bk)) { New-Item -Path $bk -Force | Out-Null }
foreach ($c in $blocked.Keys) {
    New-ItemProperty -Path $bk -Name $c -Value $blocked[$c] -PropertyType String -Force | Out-Null
}
Measure-Menu 'BLOCKED'

foreach ($c in $blocked.Keys) { Remove-ItemProperty -Path $bk -Name $c -ErrorAction SilentlyContinue }
$item = Get-Item -LiteralPath $bk -ErrorAction SilentlyContinue
if ($item) {
    if (($item.Property | Measure-Object).Count -le 0) { Remove-Item -LiteralPath $bk -Force }
}
Measure-Menu 'RESTORED'
Write-Output ('blocked key left: ' + (Test-Path -LiteralPath $bk))
