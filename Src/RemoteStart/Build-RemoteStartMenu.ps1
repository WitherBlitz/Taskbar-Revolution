# Builds the local mirror of a remote PC's Start menu that the red Start button shows.
#   Build-RemoteStartMenu.ps1 -Folder "%LOCALAPPDATA%\RemoteAppLaunchers\<PC>" -Alias <RemoteApp alias of RemoteStart.exe>
# Reads manifest.tsv (kind, item, target or name, icon location) exported from the remote PC and writes:
#   Start Menu\...   one shortcut per item, in the remote PC's folders
#   Start.rdp        the RemoteApp connection to RemoteStart.exe (made from an existing .rdp in the folder)
#   Icons\           icons extracted on this PC; Icons\remote\ holds icons copied from the remote PC
#   need-icons.txt   items whose icon only exists on the remote PC
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [Parameter(Mandatory = $true)][string]$Alias
)

$ErrorActionPreference = 'Stop'
$Folder = (Resolve-Path -LiteralPath $Folder).Path
$pc = Split-Path $Folder -Leaf
Add-Type -Path (Join-Path $PSScriptRoot 'IconTool.cs') -ReferencedAssemblies System.Drawing

# Scripts and connection
Copy-Item (Join-Path $PSScriptRoot 'Launch-RemoteStart.ps1') $Folder -Force
$rdp = Join-Path $Folder 'Start.rdp'
$source = Get-ChildItem -LiteralPath $Folder -Filter *.rdp | Where-Object Name -ne 'Start.rdp' | Select-Object -First 1
if (-not $source) { throw "No .rdp file in $Folder to copy the connection settings from." }
$lines = [IO.File]::ReadAllLines($source.FullName) | ForEach-Object {
    if ($_ -match '^remoteapplicationprogram:') { "remoteapplicationprogram:s:||$Alias" }
    elseif ($_ -match '^remoteapplicationname:') { "remoteapplicationname:s:Start menu on $pc" }
    elseif ($_ -match '^remoteapplicationcmdline:') { 'remoteapplicationcmdline:s:' }
    else { $_ }
}
[IO.File]::WriteAllLines($rdp, $lines, [Text.Encoding]::Unicode)

# Icons
$icons = Join-Path $Folder 'Icons'
$remoteIcons = Join-Path $icons 'remote'
New-Item -ItemType Directory -Force -Path $icons, $remoteIcons | Out-Null
# Icons copied from the remote PC arrive as "<key><TAB><base64 .ico>" lines in Icons\remote\*.tsv
foreach ($batch in Get-ChildItem -LiteralPath $remoteIcons -Filter *.tsv) {
    foreach ($entry in [IO.File]::ReadAllLines($batch.FullName)) {
        $key, $data = $entry.Split("`t")
        if (-not $data -or $data -eq 'FAIL') { continue }
        try {
            $bytes = [Convert]::FromBase64String($data)
            $png = New-Object IO.MemoryStream(, $bytes[22..($bytes.Length - 1)])
            [Drawing.Image]::FromStream($png).Dispose() # rejects damaged images
            [IO.File]::WriteAllBytes((Join-Path $remoteIcons "$key.ico"), $bytes)
        } catch {
            Write-Warning "Skipped a damaged icon ($key)"
        }
    }
}
$md5 = [Security.Cryptography.MD5]::Create()
function Get-ItemKey([string]$item) {
    -join ($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($item.ToLowerInvariant()))[0..7] | ForEach-Object { $_.ToString('x2') })
}
function Get-LocalPath([string]$path) {
    if (-not $path) { return $null }
    $path = [Environment]::ExpandEnvironmentVariables($path)
    if (Test-Path -LiteralPath $path) { return $path }
    # The remote user's profile folder maps to this user's profile
    $mapped = $path -replace '^C:\\Users\\[^\\]+', [regex]::Escape($env:USERPROFILE).Replace('\\', '\')
    if ($mapped -ne $path -and (Test-Path -LiteralPath $mapped)) { return $mapped }
    return $null
}
$localApps = @{}
Get-StartApps | ForEach-Object { $localApps[$_.AppID.ToLowerInvariant()] = $true }

$menu = Join-Path $Folder 'Start Menu'
if (Test-Path -LiteralPath $menu) { Remove-Item -LiteralPath $menu -Recurse -Force }
New-Item -ItemType Directory -Force -Path $menu | Out-Null

$shell = New-Object -ComObject WScript.Shell
$launcher = Join-Path $Folder 'Launch-RemoteStart.ps1'
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$fallbackIcon = Join-Path $env:SystemRoot 'System32\imageres.dll'
$need = New-Object System.Collections.Generic.List[string]
$count = 0

foreach ($line in [IO.File]::ReadAllLines((Join-Path $Folder 'manifest.tsv'))) {
    $fields = $line.Split("`t")
    if ($fields.Count -lt 3) { continue }
    $kind, $item = $fields[0], $fields[1]
    if ($kind -eq 'lnk') {
        $relative = $item.Substring($item.IndexOf(':') + 1)
        if ($relative -match '^Startup\\') { continue } # Windows hides the Startup folder from Start
        $directory = Split-Path $relative -Parent
        $name = [IO.Path]::GetFileNameWithoutExtension($relative)
    } elseif ($kind -eq 'app') {
        $directory = ''
        $name = $fields[2]
    } else { continue }

    # Icon: a matching file or app on this PC, else one copied from the remote PC
    $key = Get-ItemKey $item
    $icon = $null
    if ($kind -eq 'lnk') {
        $location = if ($fields.Count -gt 3) { $fields[3] } else { '' }
        $comma = $location.LastIndexOf(',')
        $iconFile = if ($comma -ge 0) { $location.Substring(0, $comma) } else { $location }
        $iconIndex = if ($comma -ge 0) { $location.Substring($comma + 1) } else { '0' }
        $path = Get-LocalPath $iconFile
        if ($path) { $icon = "$path,$iconIndex" }
        elseif ($path = Get-LocalPath $fields[2]) {
            if ($path -match '\.(exe|dll|ico)$') { $icon = "$path,0" }
            else {
                $bytes = [RemoteStartIconTool]::IcoBytes($path, 32)
                if ($bytes) { [IO.File]::WriteAllBytes((Join-Path $icons "$key.ico"), $bytes); $icon = (Join-Path $icons "$key.ico") + ',0' }
            }
        }
    } elseif ($localApps.ContainsKey($item.Substring(4).ToLowerInvariant())) {
        $bytes = [RemoteStartIconTool]::IcoBytes('shell:AppsFolder\' + $item.Substring(4), 32)
        if ($bytes) { [IO.File]::WriteAllBytes((Join-Path $icons "$key.ico"), $bytes); $icon = (Join-Path $icons "$key.ico") + ',0' }
    }
    if (-not $icon) {
        $copied = Join-Path $remoteIcons "$key.ico"
        if (Test-Path -LiteralPath $copied) { $icon = "$copied,0" }
        else { $need.Add($item); $icon = "$fallbackIcon,11" }
    }

    $target = Join-Path $menu $directory
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    $file = Join-Path $target "$name.lnk"
    for ($n = 2; Test-Path -LiteralPath $file; $n++) { $file = Join-Path $target "$name ($n).lnk" }
    $link = $shell.CreateShortcut($file)
    $link.TargetPath = $powershell
    $link.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}" -Item "{1}"' -f $launcher, $item
    $link.WindowStyle = 7
    $link.IconLocation = $icon
    $link.Description = "$name on $pc"
    $link.Save()
    $count++
}

[IO.File]::WriteAllLines((Join-Path $Folder 'need-icons.txt'), $need)
"Built $count items for $pc; $($need.Count) icons only exist on $pc."
