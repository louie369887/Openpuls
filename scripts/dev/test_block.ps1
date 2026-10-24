# 验证：移除 HKCU 注册的 3 个 WPS 右键扩展后，菜单项数与耗时是否变化（测完立即恢复）
$exe = 'F:\openpuls\build\OpenPulsShellOpt.exe'
$sample = Join-Path $env:LOCALAPPDATA 'OpenPuls\bench_sample.txt'
$base = 'HKCU:\SOFTWARE\Classes\*\shellex\ContextMenuHandlers'
$names = @('Open With qingshellext', 'Open With kwpsshellext', 'QingNseContextMenu')

function Measure-Menu([string]$label) {
    $out = Join-Path $env:TEMP 'bm3.txt'
    Remove-Item $out -ErrorAction SilentlyContinue
    Start-Process -FilePath $exe -ArgumentList @('--bench-menu', 'file', $sample, '4', $out) -Wait -WindowStyle Hidden | Out-Null
    $txt = Get-Content $out -Raw -ErrorAction SilentlyContinue
    $runs = ($txt -split "`n") | Where-Object { $_ -match '^(run|ms|items)' }
    Write-Output ($label.PadRight(9) + ' | ' + ($runs -join '  '))
}

Measure-Menu 'BEFORE'

$saved = @{}
foreach ($n in $names) {
    $p = Join-Path $base $n
    if (Test-Path -LiteralPath $p) {
        $saved[$n] = (Get-ItemProperty -LiteralPath $p).'(default)'
    }
}
Write-Output ('saved: ' + ($saved.Keys -join ', '))

foreach ($n in $saved.Keys) {
    Remove-Item -LiteralPath (Join-Path $base $n) -Force -Recurse
}
Measure-Menu 'REMOVED'

foreach ($n in $saved.Keys) {
    $p = Join-Path $base $n
    New-Item -Path $p -Force | Out-Null
    $v = $saved[$n]
    if ($v) { Set-ItemProperty -Path $p -Name '(default)' -Value $v }
}
Measure-Menu 'RESTORED'

foreach ($n in $saved.Keys) {
    $p = Join-Path $base $n
    $v = (Get-ItemProperty -LiteralPath $p -ErrorAction SilentlyContinue).'(default)'
    Write-Output ('  check ' + $n + ' -> ' + $v)
}
