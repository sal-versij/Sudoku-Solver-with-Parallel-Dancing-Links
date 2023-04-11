
function Measure-Command2 ([ScriptBlock]$Expression, [int]$Samples = 1, [Switch]$Silent, [Switch]$Compact) {
<#
.SYNOPSIS
  Runs the given script block and returns the execution duration.
  Discovered on StackOverflow. http://stackoverflow.com/questions/3513650/timing-a-commands-execution-in-powershell
  
.EXAMPLE
  Measure-Command2 { ping -n 1 google.com }
#>
  $timings = @()
  do {
    $sw = New-Object Diagnostics.Stopwatch
    if ($Silent) {
      $sw.Start()
      $null = & $Expression
      $sw.Stop()
      Write-Host "." -NoNewLine
    }
    else {
      $sw.Start()
      & $Expression
      $sw.Stop()
    }
    $timings += $sw.Elapsed
    
    $Samples--
  }
  while ($Samples -gt 0)
  
  Write-Host
  
  $stats = $timings | Measure-Object -Average -Minimum -Maximum -Property Ticks
  
  if($Compact){
    Write-Output "$((New-Object System.TimeSpan $stats.Average).TotalMilliseconds);$((New-Object System.TimeSpan $stats.Minimum).TotalMilliseconds);$((New-Object System.TimeSpan $stats.Maximum).TotalMilliseconds)"
  }else{
    Write-Output "Avg: $((New-Object System.TimeSpan $stats.Average).TotalMilliseconds)ms"
    Write-Output "Min: $((New-Object System.TimeSpan $stats.Minimum).TotalMilliseconds)ms"
    Write-Output "Max: $((New-Object System.TimeSpan $stats.Maximum).TotalMilliseconds)ms"
  }
}

Set-Alias time Measure-Command2

$tasks = @(
    @(16, 32, 64, 128, 256, 512),
    @(16, 32, 64, 128, 256, 512),
    @(16, 32, 64, 128),
    @(16, 32, 64, 128),
    @(16, 32, 64, 128),
    @(16, 32, 64, 128),
    @(16, 32),
    @(16, 32),
    @(16, 32)
)

for ($i=1; $i -le $tasks.length; $i++){
    foreach ($lws in $tasks[$i - 1]){
        Write-Output "$i;$lws;"
        time { .\cmake-build-debug\dlx_parallel.exe ./inputs/$i.txt $lws } -Samples 10 -Silent -Compact
    }
}
