# =============================================================================
# set_version.ps1 - TStar Version Management Utility
# =============================================================================
#
# Updates the project version by modifying changelog.txt, which serves as the
# single source of truth for CMake and the build system.
#
# Usage:
#   .\set_version.ps1 -NewVersion "1.9.0"                        # Update latest entry
#   .\set_version.ps1 -NewVersion "1.9.0" -ChangelogMessage "..."  # Prepend new entry
#
# =============================================================================

param(
    [Parameter(Mandatory = $true)]
    [string]$NewVersion,

    [Parameter(Mandatory = $false)]
    [string]$ChangelogMessage
)

# ---------------------------------------------------------------------------
# Resolve and validate the changelog file path.
# ---------------------------------------------------------------------------

$ChangelogFile = Join-Path $PSScriptRoot "changelog.txt"

if (!(Test-Path $ChangelogFile)) {
    Write-Host "[ERROR] Changelog file not found: $ChangelogFile"
    exit 1
}

$Content = Get-Content $ChangelogFile -Raw
$Date = Get-Date -Format "yyyy-MM-dd"

# ---------------------------------------------------------------------------
# Branch: prepend a new version entry or update the existing latest version.
# ---------------------------------------------------------------------------

if ($ChangelogMessage) {
    # Prepend a brand-new version block when a changelog message is supplied.
    Write-Host "Prepending new changelog entry for version $NewVersion..."

    $NewEntry = @"

Version $NewVersion
-------------
($Date)
- $ChangelogMessage
"@

    # Insert the new entry immediately after the title separator line.
    $SplitIndex = $Content.IndexOf("===============") + 15
    if ($SplitIndex -lt 15) { $SplitIndex = 0 }

    $Pre  = $Content.Substring(0, $SplitIndex)
    $Post = $Content.Substring($SplitIndex)

    $NewContent = $Pre + "`n" + $NewEntry + "`n" + $Post
    Set-Content -Path $ChangelogFile -Value $NewContent
}
else {
    # No message provided -- simply update the version number on the most
    # recent entry (useful for bumping the version without adding notes).
    Write-Host "Updating latest version to $NewVersion..."

    $NewContent = $Content -replace "(?m)^Version [0-9.]+", "Version $NewVersion"
    Set-Content -Path $ChangelogFile -Value $NewContent
}

Write-Host "Done. TStar version is now $NewVersion (source: changelog.txt)"