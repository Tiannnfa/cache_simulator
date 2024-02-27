#!/bin/bash
set -e

student_stat_dir=student_outs
default_benchmarks=( gcc leela linpack matmul_naive matmul_tiled mcf )

flags_nol2_l1_dm='-D -c 10 -s 0'
flags_nol2_l1_sa='-D -c 14 -s 4'
flags_nol2_l1_fa='-D -c 10 -s 4'
flags_nopref='-P 0'
flags_plus_one_pref='-P 1'
flags_mip_plus_one_pref='-P 1 -I mip'
flags_stride='-P 2'
flags_lfu='-P 2 -r lfu'
flags_mip_lfu='-P 2 -I mip -r lfu'

banner() {
    local message=$1
    printf '%s\n' "$message"
    yes = | head -n ${#message} | tr -d '\n'
    printf '\n'
}

student_stat_path() {
    local config=$1
    local benchmark=$2

    printf '%s' "${student_stat_dir}/${config}_${benchmark}.out"
}

ta_stat_path() {
    local config=$1
    local benchmark=$2

    printf '%s' "ref_outs/${config}_${benchmark}.out"
}

human_friendly_flags() {
    local config=$1

    local config_flags_var=flags_$config
    local flags="${!config_flags_var}"
    if [[ -n $flags ]]; then
        printf '%s' "$flags"
    else
        printf '(none)'
    fi
}

generate_stats() {
    local config=$1
    local benchmark=$2

    local config_flags_var=flags_$config
    bash run.sh ${!config_flags_var} -f "traces/$benchmark.trace" >"$(student_stat_path "$config" "$benchmark")"
}

generate_stats_and_diff() {
    local config=$1
    local benchmark=$2

    printf '==> Running %s...\n' "$benchmark"
    generate_stats "$config" "$benchmark"
    if diff -u "$(ta_stat_path "$config" "$benchmark")" "$(student_stat_path "$config" "$benchmark")"; then
        printf 'Matched!\n\n'
    else
        printf '\nPlease examine the differences printed above. Benchmark: %s. Config name: %s. Flags to cachesim used: %s\n\n' "$benchmark" "$config" "$(human_friendly_flags "$config")"
    fi
}

main() {
    mkdir -p "$student_stat_dir"

    banner "Testing only L1 cache (direct-mapped, set-associative, fully-associative)..."
    generate_stats_and_diff nol2_l1_dm gcc
    generate_stats_and_diff nol2_l1_sa gcc
    generate_stats_and_diff nol2_l1_fa gcc

    banner "Testing L1 and L2 without prefetcher..."
    generate_stats_and_diff nopref gcc

    banner "Testing default configuration (L1, L2, and +1 prefetcher)..."
    for benchmark in "${default_benchmarks[@]}"; do
        generate_stats_and_diff plus_one_pref "$benchmark"
    done

    banner "Testing L1, L2, +1 prefetcher, and MIP..."
    generate_stats_and_diff mip_plus_one_pref gcc

    banner "(Grad only) Testing L1, L2, and strided prefetcher"
    generate_stats_and_diff stride gcc

    banner "(Grad only) Testing default configuration (L1, L2, strided prefetcher, and LFU)"
    for benchmark in "${default_benchmarks[@]}"; do
        generate_stats_and_diff lfu "$benchmark"
    done

    banner "(Grad only) Testing L1, L2, strided prefetcher, LFU, and MIP"
    generate_stats_and_diff mip_lfu gcc

}

main
