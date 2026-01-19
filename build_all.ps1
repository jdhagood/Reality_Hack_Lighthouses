$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$meshCore = Join-Path $projectRoot "MeshCore"

for ($n = 1; $n -le 30; $n++) {
  $envName = "Lighthouse_sx1262_$n"
  $cmd = "Set-Location `"$meshCore`"; pio run -e $envName"
  Start-Process -FilePath "powershell" -ArgumentList "-NoExit", "-Command", $cmd
}
