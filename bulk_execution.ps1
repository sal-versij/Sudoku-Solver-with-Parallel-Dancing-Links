$tasks = @(
@(16, 32, 64, 128, 256, 512),
@(16, 32, 64, 128, 256, 512),
@(16, 32, 64),
@(16, 32, 64),
@(16, 32, 64),
@(16, 32),
@(16, 32),
@(16, 32)
)

$env:OCL_PLATFORM = "0"

foreach ($i in 0..7)
{
    $input = "./inputs/$( $i + 1 ).txt"
    foreach ($lws in $tasks[$i])
    {
        Write-Host "Running $input with $lws"
        foreach ($j in 0..9)
        {
            Write-Host "Run $j"
            $null = .\cmake-build-debug\dlx_parallel_2.exe $input $lws ./out.csv
        }
    }
}
