# 列出所有右键扩展：键、名称、CLSID、Approved 显示名
$keys = @(
    'HKLM:\SOFTWARE\Classes\*\shellex\ContextMenuHandlers',
    'HKLM:\SOFTWARE\Classes\Directory\shellex\ContextMenuHandlers',
    'HKLM:\SOFTWARE\Classes\Directory\Background\shellex\ContextMenuHandlers',
    'HKLM:\SOFTWARE\Classes\Folder\shellex\ContextMenuHandlers',
    'HKLM:\SOFTWARE\Classes\AllFilesystemObjects\shellex\ContextMenuHandlers',
    'HKLM:\SOFTWARE\Classes\Drive\shellex\ContextMenuHandlers',
    'HKCU:\SOFTWARE\Classes\*\shellex\ContextMenuHandlers',
    'HKCU:\SOFTWARE\Classes\Directory\shellex\ContextMenuHandlers'
)
$apprH = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
$apprU = Get-ItemProperty 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
foreach ($k in $keys) {
    if (-not (Test-Path -LiteralPath $k)) { continue }
    Get-ChildItem -LiteralPath $k -ErrorAction SilentlyContinue | ForEach-Object {
        $p = Get-ItemProperty -LiteralPath $_.PSPath
        $c = $p.'(default)'
        if (-not $c) { $c = $p.CLSID }
        $n = ''
        if ($c) { $n = $apprH.$c; if (-not $n) { $n = $apprU.$c } }
        Write-Output ('{0,-42} {1,-46} {2}' -f $_.PSChildName.Trim(), $c, $n)
    }
}

