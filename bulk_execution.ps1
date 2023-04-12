$env:OCL_PLATFORM = "0"

$lwss = @(1, 2, 4, 8)

foreach ($i in 1..8)
{
    $input = "./inputs/$i.txt"
    foreach ($lws in $lwss)
    {
        Write-Host "Running $i with $lws"
        foreach ($j in 0..9)
        {
            Write-Host "."
            $null = .\cmake-build-debug\dlx_parallel.exe $input 1 GPU2.csv
        }
    }
}
