param(
    [ValidateSet('fit','16:9','21:9','32:9','off')][string]$Aspect='fit',
    [ValidateSet('viewport','classic')][string]$Enemies='viewport',
    [string]$Rom='',
    [ValidateRange(0,65535)][int]$TcpPort=0
)
$ErrorActionPreference='Stop'
$repoRoot=$PSScriptRoot
$exePath=Join-Path $repoRoot 'build-wide\Release\MegaManRecomp.exe'
if(-not $Rom){
    $candidates=@(
        (Join-Path $repoRoot 'Mega Man (Europe).nes')
    )
    $Rom=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
}
if(-not (Test-Path -LiteralPath $exePath)){throw 'Build build-wide/Release first; see WIDESCREEN.md.'}
if(-not $Rom -or -not (Test-Path -LiteralPath $Rom)){throw 'Supply -Rom with the supported original Mega Man Europe ROM (payload CRC32 94476A70).'}
$evidencePath=Join-Path $repoRoot 'wide-evidence'
[void][System.IO.Directory]::CreateDirectory($evidencePath)
function Quote-Native([string]$value){
    '"'+[regex]::Replace([regex]::Replace($value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
$argsList=@($Rom,'--widescreen',$Aspect,'--widescreen-enemies',$Enemies,'--no-save','--config',(Join-Path $evidencePath 'owner-playtest.ini'))
if($TcpPort){$argsList+=@('--tcp',"$TcpPort")}
$info=New-Object System.Diagnostics.ProcessStartInfo
$info.FileName=$exePath
$info.Arguments=($argsList | ForEach-Object {Quote-Native $_}) -join ' '
$info.WorkingDirectory=Split-Path $exePath -Parent
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.WindowStyle=[System.Diagnostics.ProcessWindowStyle]::Hidden
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$info.EnvironmentVariables['NESRECOMP_NO_LAUNCHER']='1'
$taskProcess=New-Object System.Diagnostics.Process
$taskProcess.StartInfo=$info
[void]$taskProcess.Start()
$outTask=$taskProcess.StandardOutput.ReadToEndAsync()
$errTask=$taskProcess.StandardError.ReadToEndAsync()
$taskProcess.WaitForExit()
[System.IO.File]::WriteAllText((Join-Path $evidencePath 'owner-playtest.log'),$outTask.Result+$errTask.Result)
exit $taskProcess.ExitCode
