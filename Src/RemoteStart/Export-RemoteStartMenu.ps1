# Runs on the remote PC. Writes its Start menu for Build-RemoteStartMenu.ps1:
#   <OutFolder>\manifest.tsv   kind, item, target or name, icon location
#   <OutFolder>\Icons\<key>.ico one 16-pixel icon per item
param(
    [Parameter(Mandatory = $true)][string]$OutFolder,
    [int]$IconSize = 16
)

$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'IconTool.cs') -ReferencedAssemblies System.Drawing
$icons = Join-Path $OutFolder 'Icons'
New-Item -ItemType Directory -Force -Path $icons | Out-Null
$md5 = [Security.Cryptography.MD5]::Create()
function Get-ItemKey([string]$item) {
    -join ($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($item.ToLowerInvariant()))[0..7] | ForEach-Object { $_.ToString('x2') })
}
function Save-Icon([string]$item, [string]$parseName) {
    $bytes = [RemoteStartIconTool]::IcoBytes($parseName, $IconSize)
    if ($bytes) { [IO.File]::WriteAllBytes((Join-Path $icons ((Get-ItemKey $item) + '.ico')), $bytes) }
}

$shell = New-Object -ComObject WScript.Shell
$seen = @{}
$lines = New-Object System.Collections.Generic.List[string]
$roots = @(
    @('user', (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs')),
    @('common', (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'))
)
foreach ($root in $roots) {
    $base = $root[1].TrimEnd('\')
    Get-ChildItem -LiteralPath $base -Recurse -File -Include *.lnk, *.url, *.appref-ms -ErrorAction SilentlyContinue | ForEach-Object {
        $relative = $_.FullName.Substring($base.Length + 1)
        if ($seen[$relative.ToLowerInvariant()]) { return } # the user's copy wins over the shared one
        $seen[$relative.ToLowerInvariant()] = $true
        $target = ''; $location = ''
        if ($_.Extension -eq '.lnk') {
            try { $link = $shell.CreateShortcut($_.FullName); $target = $link.TargetPath; $location = $link.IconLocation } catch { }
        }
        $item = '{0}:{1}' -f $root[0], $relative
        $lines.Add(("lnk`t{0}`t{1}`t{2}" -f $item, $target, $location))
        Save-Icon $item $_.FullName
    }
}
Get-StartApps | Where-Object AppID -match '^[^\\]+_[a-z0-9]{13}![^\\]+$' | ForEach-Object {
    $item = 'app:' + $_.AppID
    $lines.Add(("app`t{0}`t{1}`t" -f $item, $_.Name))
    Save-Icon $item ('shell:AppsFolder\' + $_.AppID)
}
[IO.File]::WriteAllLines((Join-Path $OutFolder 'manifest.tsv'), $lines)
"Exported $($lines.Count) items and $((Get-ChildItem -LiteralPath $icons -Filter *.ico).Count) icons."
