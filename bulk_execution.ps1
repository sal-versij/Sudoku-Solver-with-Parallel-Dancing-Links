$env:OCL_PLATFORM="0"

foreach ($i in 1..8)
{
    $input = "./inputs/$i.txt"
    Write-Host "Running $i"
    foreach ($j in 0..9)
    {
        Write-Host "Run $j"
        .\cmake-build-debug\dlx_parallel.exe $input 1 out.csv
    }
}
