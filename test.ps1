$ml = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\14.29.30133\bin\HostX64\x86\ml.exe"
$link = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\14.29.30133\bin\HostX64\x86\link.exe"
$kernel32 = "C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0\um\x86\kernel32.lib"
$user32 = "C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0\um\x86\user32.lib"
$irvine = "C:\Irvine32"

$src = "src/RevStr.asm"
$base = "RevStr"

Write-Host "::group::Building $src"

& $ml /c $src
if ($LASTEXITCODE -ne 0) {
  Write-Host "::error::Failed to assemble $src"
  exit 1
}

& $link /SUBSYSTEM:CONSOLE /OUT:$($base).exe "$base.obj" "$irvine\Irvine32.lib" $kernel32 $user32
if ($LASTEXITCODE -ne 0) {
  Write-Host "::error::Failed to link $base"
  exit 1
}
Write-Host "::endgroup::"

Write-Host "=== Running $base ==="
$output = (& ".\$($base).exe" 2>&1 | Out-String) -replace '\r', ''
Write-Host $output

$expectedRevStr = "Abraham Lincoln`nnlocniL maharbA"
if ($output.Trim() -eq $expectedRevStr) {
  Write-Host "PASS: RevStr"
} else {
  Write-Host "##[error]FAIL: RevStr"
  exit 1
}
