# Builds the controller firmware, one image per language, and (optionally) publishes it on a Hinasto server;
# controllers update themselves, each one to the image of its own language.
#   tools\publish-firmware.ps1 -NoPublish                                    build all languages into firmware\
#   tools\publish-firmware.ps1 -Server https://hinasto.example.com           build all languages and publish them
#   tools\publish-firmware.ps1 -Server https://hinasto.example.com -Languages PL,EN
#   tools\publish-firmware.ps1 -Server https://hinasto.example.com -SkipBuild   publish the images already in firmware\
# Raise FW_VERSION in sniffer\control.h first: controllers only update to a higher number.
# Images are named hinasto-fw<version>-<LANG>.bin (PL, EN, DE, CS, SK). The language is compiled in with -DHN_LANG_<LANG>.
param(
  [string]$Server = '',
  [string[]]$Languages = @('PL', 'EN', 'DE', 'CS', 'SK'),
  [switch]$SkipBuild,
  [switch]$NoPublish
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = "$env:LOCALAPPDATA\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
$out = Join-Path $root 'firmware'
$Server = $Server.TrimEnd('/')
if (-not $NoPublish -and $Server -eq '') { throw 'give -Server <url> to publish, or -NoPublish to only build' }

$version = [regex]::Match((Get-Content "$root\sniffer\control.h" -Raw), '#define FW_VERSION (\d+)').Groups[1].Value
if (-not $version) { throw 'FW_VERSION not found in sniffer\control.h' }
New-Item -ItemType Directory -Force $out | Out-Null
$images = @()
foreach ($lang in $Languages) {
  $lang = $lang.ToUpper()
  $image = Join-Path $out "hinasto-fw$version-$lang.bin"
  if (-not $SkipBuild) {
    # A temporary copy of the sketch holds lang_select.h with the language: the compiler flags stay the same for every
    # language and all builds share one build folder, so the Arduino core and the libraries are compiled only once.
    $build = Join-Path $env:TEMP 'hinasto-build'
    $copy = Join-Path $env:TEMP 'hinasto-sketch\sniffer'
    if (Test-Path (Split-Path $copy)) { Remove-Item (Split-Path $copy) -Recurse -Force }
    New-Item -ItemType Directory -Force $copy | Out-Null
    Copy-Item "$root\sniffer\*" $copy -Recurse
    [IO.File]::WriteAllText("$copy\lang_select.h", "#define HN_LANG_$lang`n", (New-Object Text.UTF8Encoding($false)))
    Write-Host "== building $lang (firmware $version)"
    & $cli compile --fqbn esp32:esp32:lolin_s2_mini:CDCOnBoot=default --build-path $build $copy
    if ($LASTEXITCODE -ne 0) { throw "build failed ($lang)" }
    Copy-Item (Join-Path $build 'sniffer.ino.bin') $image -Force
    if (Test-Path "$root\sniffer\signing_key.h") {  # this build only accepts signed updates (docs/signing.md): sign the image
      python "$PSScriptRoot\ota-sign.py" sign $image --keys "$root\keys"
      if ($LASTEXITCODE -ne 0) { throw "signing failed ($lang): needs Python 3 with the cryptography package, or use the builder image" }
    }
  }
  if (-not (Test-Path $image)) { throw "no image found: $image" }
  Write-Host ("   {0}: {1:N0} bytes" -f (Split-Path $image -Leaf), (Get-Item $image).Length)
  $images += $image
}
if ($NoPublish) { return }

$secure = Read-Host 'Panel password' -AsSecureString
$plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
$jar = Join-Path $env:TEMP 'hinasto-cookies.txt'
$body = Join-Path $env:TEMP 'hinasto-login.json'
[IO.File]::WriteAllText($body, (@{ password = $plain } | ConvertTo-Json -Compress), (New-Object Text.UTF8Encoding($false)))
try {
  $login = curl.exe --ssl-no-revoke -s -c $jar -X POST "$Server/api/panel/login" -H 'Content-Type: application/json' --data-binary "@$body" | ConvertFrom-Json
  if (-not $login.csrf) { throw 'login failed (wrong password?)' }
  foreach ($image in $images) {
    $answer = curl.exe --ssl-no-revoke -s -b $jar -H "X-CSRF: $($login.csrf)" -F "file=@$image" "$Server/api/panel/firmware" | ConvertFrom-Json
    if ($answer.ok) { "Published v$($answer.version) $($answer.lang.ToUpper()), $([math]::Round($answer.size / 1MB, 2)) MB." }
    else { throw "server refused $(Split-Path $image -Leaf): $($answer.error)" }
  }
  'Controllers update themselves when idle.'
} finally {
  Remove-Item $body, $jar -ErrorAction SilentlyContinue
}
