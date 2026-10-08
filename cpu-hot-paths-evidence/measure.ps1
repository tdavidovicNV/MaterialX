param([Parameter(Mandatory=$true)][string]$Source, [int]$Rounds = 8)
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'timings.csv'
'round,candidate,variant,order,mode,workload,iterations,ns_per_op,checksum' | Set-Content $out
$variants = @('reserved','targets','lookup','interfaces','combined')
for ($round = 0; $round -lt $Rounds; ++$round) {
    for ($offset = 0; $offset -lt $variants.Count; ++$offset) {
        $candidate = $variants[($offset + $round) % $variants.Count]
        $pair = if ($round % 2) { @($candidate, 'baseline') } else { @('baseline', $candidate) }
        for ($order = 0; $order -lt 2; ++$order) {
            $variant = $pair[$order]
            foreach ($mode in @('micro', 'generate')) {
                $count = if ($mode -eq 'micro') { 100000 } else { 150 }
                $lines = & "$PSScriptRoot/$variant/CpuHotPaths.exe" $Source $mode $count
                if ($LASTEXITCODE) { throw "Failed: $variant $mode" }
                foreach ($line in $lines) { "$round,$candidate,$variant,$order,$mode,$line" | Add-Content $out }
            }
        }
        Write-Output "Round $($round + 1)/$Rounds : $candidate complete"
    }
}
