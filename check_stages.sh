#!/bin/bash
stages=(
    "seeding_water"
    "refine_water"
    "init_regions"
    "making_regions"
    "spread_green_raw"
    "spread_green_recalc"
)

for stage in "${stages[@]}"; do
    cli_file="build/main_repro_62298_stage_after_${stage}.bin"
    game_file="snapshot_gamemd_62298/_verify_rules_stage_after_${stage}.bin"
    
    if [ -f "$cli_file" ] && [ -f "$game_file" ]; then
        if diff -q "$cli_file" "$game_file" > /dev/null 2>&1; then
            echo "✓ $stage - MATCH"
        else
            echo "✗ $stage - DIFFER"
            echo "  CLI:  $(ls -lh $cli_file | awk '{print $5}')"
            echo "  Game: $(ls -lh $game_file | awk '{print $5}')"
        fi
    else
        echo "? $stage - missing files"
    fi
done
