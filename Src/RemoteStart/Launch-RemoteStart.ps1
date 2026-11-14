# Opens one item from a remote PC's Start menu through the Windows App client.
# Lives in %LOCALAPPDATA%\RemoteAppLaunchers\<PC>\ next to Start.rdp, which points at the
# RemoteStart RemoteApp published on that PC.
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^(common|user|app):')]
    [string]$Item
)

$ErrorActionPreference = 'Stop'
try {
    $template = Join-Path $PSScriptRoot 'Start.rdp'
    if (-not (Test-Path -LiteralPath $template)) {
        throw 'Start.rdp is missing. Rebuild the remote Start menu.'
    }
    $lines = @([IO.File]::ReadAllLines($template) | Where-Object { $_ -notmatch '^remoteapplicationcmdline:' })
    $lines += ('remoteapplicationcmdline:s:"{0}"' -f $Item)

    $folder = Join-Path $env:TEMP 'RemoteStart'
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    Get-ChildItem -LiteralPath $folder -Filter *.rdp | Where-Object LastWriteTime -lt (Get-Date).AddMinutes(-10) |
        Remove-Item -ErrorAction SilentlyContinue
    $connection = Join-Path $folder ([guid]::NewGuid().ToString('N') + '.rdp')
    [IO.File]::WriteAllLines($connection, $lines, [Text.Encoding]::Unicode)

    $package = Get-AppxPackage -Name MicrosoftCorporationII.Windows365
    if (-not $package) {
        throw 'Windows App is not installed for this Windows user.'
    }
    $client = Join-Path $package.InstallLocation 'msrdc\msrdc.exe'
    Invoke-CommandInDesktopPackage -PackageFamilyName $package.PackageFamilyName `
        -AppId Windows365 -Command $client -Args ('"{0}"' -f $connection) -PreventBreakaway
} catch {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Remote Start menu',
        [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}
