<#
.SYNOPSIS
    Cut an Astralay release: bump the version in CMakeLists.txt, tag it, push the tag.

.DESCRIPTION
    Invoked as `git release`. The project version and the tag are one contract: the plugins and
    the installers take their version from CMakeLists.txt, and .github/workflows/release.yml
    refuses to build a tag whose name disagrees with it. This does both, in the one order that
    works.

    The bump is committed before the tag is created. A tag points at a commit, so tagging with
    the new version still sitting in the working tree would tag the old one, and the workflow,
    which checks out the tag, would stop at its version check.

    Only CMakeLists.txt is committed. Unrelated work in progress stays out of the release commit.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Fail($message) {
    Write-Host "release: $message" -ForegroundColor Red
    exit 1
}

# Enter accepts the default, so a run with no console would silently take it and push a release
# nobody asked for. Refuse rather than guess.
if ([Console]::IsInputRedirected) {
    Fail "this must be run interactively: it prompts for the version."
}

# --- Locate the repo and the version ---------------------------------------

$repo = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $repo) { Fail "not inside a git repository." }
$repo = $repo.Trim()

$cmakePath = Join-Path $repo 'CMakeLists.txt'
if (-not (Test-Path $cmakePath)) { Fail "CMakeLists.txt not found at $cmakePath." }

# The same line the workflow's version job reads. Keep the two in step.
$pattern = '(?m)^(project\(Astralay VERSION )([0-9.]+)'
$content = [System.IO.File]::ReadAllText($cmakePath)
$match = [regex]::Match($content, $pattern)
if (-not $match.Success) { Fail "the project version was not found in CMakeLists.txt." }
$current = $match.Groups[2].Value

# --- Work out the default next version -------------------------------------
# Increment the last component.

$parts = $current -split '\.'
$default = $null
if ($parts[-1] -match '^\d+$') {
    $parts[-1] = "$([int]$parts[-1] + 1)"
    $default = ($parts -join '.')
}

# --- Ask --------------------------------------------------------------------

Write-Host ""
Write-Host "Current version: $current"
if ($default) {
    $answer = Read-Host "Version to release [$default]"
} else {
    Write-Host "(cannot derive a default from '$current'; type the version in full)"
    $answer = Read-Host "Version to release"
}
if ([string]::IsNullOrWhiteSpace($answer)) { $answer = $default }
if ([string]::IsNullOrWhiteSpace($answer)) { Fail "no version given." }

# Accept a leading v so a typed "v0.2.0" does the obvious thing rather than producing a tag
# called "vv0.2.0".
$version = $answer.Trim()
$version = $version -replace '^[vV]', ''

# CMake takes up to four numeric components and nothing else, so no pre-release suffixes.
if ($version -notmatch '^\d+(\.\d+){0,3}$') {
    Fail "'$version' is not a version CMake accepts: use up to four numbers separated by dots."
}

$tag = "v$version"

# --- Conflict checks --------------------------------------------------------
# Local tags first, then the remote's, because a tag that exists only on origin still makes the
# push fail, and it fails after the commit has been made.

& git fetch --tags --quiet 2>$null | Out-Null

$existing = (& git tag --list $tag)
if ($existing) { Fail "tag $tag already exists locally. Nothing has been changed." }

$remote = (& git ls-remote --tags origin "refs/tags/$tag" 2>$null)
if ($LASTEXITCODE -eq 0 -and $remote) {
    Fail "tag $tag already exists on origin. Nothing has been changed."
}

if ($version -eq $current) {
    Fail "the version is already $current and no tag exists for it. Pick a new version, or tag $tag by hand."
}

$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -eq 'HEAD') { Fail "HEAD is detached; check out a branch first." }

# --- Write the version ------------------------------------------------------

Write-Host ""
Write-Host "Releasing $tag (version $current -> $version) from branch $branch"

$updated = [regex]::Replace($content, $pattern, "`${1}$version", 1)
if ($updated -eq $content) { Fail "failed to rewrite the version." }
# No byte order mark: CMake would read it as part of the first command.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($cmakePath, $updated, $utf8NoBom)
Write-Host "  updated CMakeLists.txt"

# --- Commit, tag, push ------------------------------------------------------

& git add -- $cmakePath
if ($LASTEXITCODE -ne 0) { Fail "git add failed." }

& git commit -m "Release $tag" -- $cmakePath
if ($LASTEXITCODE -ne 0) { Fail "git commit failed. CMakeLists.txt has been edited but nothing was tagged." }
Write-Host "  committed the version bump"

& git tag -a $tag -m "Release $tag"
if ($LASTEXITCODE -ne 0) { Fail "git tag failed. The bump is committed; tag it by hand." }
Write-Host "  created annotated tag $tag"

# The branch goes first. Pushing a tag alone uploads the objects it needs but leaves origin's
# branch behind it, so the release commit would exist on the remote with nothing pointing at it
# but the tag.
& git push origin $branch
if ($LASTEXITCODE -ne 0) { Fail "pushing $branch failed. The tag exists locally; push it once the branch is up." }

& git push origin $tag
if ($LASTEXITCODE -ne 0) { Fail "pushing $tag failed. Run: git push origin $tag" }

Write-Host ""
Write-Host "Pushed $tag. The release workflow builds and publishes it:" -ForegroundColor Green
Write-Host "  https://github.com/ironcross32/astralay/actions"
