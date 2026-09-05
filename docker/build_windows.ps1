param(
    [string]$ImageName = "dungeon-underworld-windows-build"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".."))

$dockerInfo = docker info --format '{{.OSType}}'
if ($dockerInfo -ne "windows") {
    throw "Windows build requires Docker Desktop in Windows container mode on a Windows host. Current daemon: $dockerInfo"
}

docker build --platform windows/amd64 -f (Join-Path $RepoRoot "docker\Dockerfile.windows") -t $ImageName $RepoRoot
docker run --rm --mount "type=bind,source=$RepoRoot,target=C:\workspace" $ImageName
