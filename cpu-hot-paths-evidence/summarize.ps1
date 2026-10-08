param([string]$Csv = "$PSScriptRoot/timings.csv")
$rows = Import-Csv $Csv
function Median($values) {
    $sorted = @($values | Sort-Object)
    $n = $sorted.Count
    if ($n % 2) { return $sorted[[int][math]::Floor($n / 2)] }
    return ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2
}
$results = foreach ($candidate in @('reserved','targets','lookup','interfaces','combined')) {
    foreach ($workload in ($rows | Select-Object -ExpandProperty workload -Unique)) {
        $selected = @($rows | Where-Object { $_.candidate -eq $candidate -and $_.workload -eq $workload })
        $base = @($selected | Where-Object variant -eq baseline)
        $changed = @($selected | Where-Object variant -eq $candidate)
        $improvements = @()
        foreach ($b in $base) {
            $c = $changed | Where-Object round -eq $b.round
            if ($c) { $improvements += 100 * (1 - [double]$c.ns_per_op / [double]$b.ns_per_op) }
        }
        if ($improvements.Count) {
            [PSCustomObject]@{
                candidate = $candidate; workload = $workload; pairs = $improvements.Count
                baseline_ns = [math]::Round((Median @($base | ForEach-Object { [double]$_.ns_per_op })), 3)
                candidate_ns = [math]::Round((Median @($changed | ForEach-Object { [double]$_.ns_per_op })), 3)
                median_reduction_percent = [math]::Round((Median $improvements), 2)
                min_reduction_percent = [math]::Round(($improvements | Measure-Object -Minimum).Minimum, 2)
                max_reduction_percent = [math]::Round(($improvements | Measure-Object -Maximum).Maximum, 2)
            }
        }
    }
}
$results | Export-Csv "$PSScriptRoot/summary.csv" -NoTypeInformation
$results | Where-Object { $_.workload -in @('standard_surface','open_pbr_surface') -or
    ($_.candidate -eq 'reserved' -and $_.workload -in @('valid_name','reserved_name')) -or
    ($_.candidate -eq 'targets' -and $_.workload -like 'target_*') -or
    ($_.candidate -eq 'lookup' -and $_.workload -in @('node_def','implementation')) -or
    ($_.candidate -eq 'interfaces' -and $_.workload -like 'active_*') } | Format-Table -AutoSize
