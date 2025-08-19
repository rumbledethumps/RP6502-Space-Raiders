cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(raiders)

rp6502_asset(raiders 0x10200 assets/bin/spc_inv_bunker_template_new-spr32.bin)
rp6502_asset(raiders 0x10A00 assets/bin/spc_inv_bunker_0_plyr1_new-spr32.bin)
rp6502_asset(raiders 0x11200 assets/bin/spc_inv_bunker_1_plyr1_new-spr32.bin)
rp6502_asset(raiders 0x11A00 assets/bin/spc_inv_bunker_2_plyr1_new-spr32.bin)
rp6502_asset(raiders 0x12200 assets/bin/spc_inv_bunker_3_plyr1_new-spr32.bin)
rp6502_asset(raiders 0x12A00 assets/bin/spc_inv_bunker_0_plyr2_new-spr32.bin)
rp6502_asset(raiders 0x13200 assets/bin/spc_inv_bunker_1_plyr2_new-spr32.bin)
rp6502_asset(raiders 0x13A00 assets/bin/spc_inv_bunker_2_plyr2_new-spr32.bin)
rp6502_asset(raiders 0x14200 assets/bin/spc_inv_bunker_3_plyr2_new-spr32.bin)
rp6502_asset(raiders 0x14A00 assets/bin/spc_inv_saucer_magenta_explosion-spr32.bin)
rp6502_asset(raiders 0x15200 assets/bin/spc_inv_score50-spr32.bin)
rp6502_asset(raiders 0x15A00 assets/bin/spc_inv_score100-spr32.bin)
rp6502_asset(raiders 0x16200 assets/bin/spc_inv_score150-spr32.bin)
rp6502_asset(raiders 0x16A00 assets/bin/spc_inv_score300-spr32.bin)
rp6502_asset(raiders 0x17200 assets/bin/spc_inv_greenie1-spr16.bin)
rp6502_asset(raiders 0x17400 assets/bin/spc_inv_greenie2-spr16.bin)
rp6502_asset(raiders 0x17600 assets/bin/spc_inv_bluie1-spr16.bin)
rp6502_asset(raiders 0x17800 assets/bin/spc_inv_bluie2-spr16.bin)
rp6502_asset(raiders 0x17A00 assets/bin/spc_inv_magentaie1-spr16.bin)
rp6502_asset(raiders 0x17C00 assets/bin/spc_inv_magentaie2-spr16.bin)
rp6502_asset(raiders 0x17E00 assets/bin/spc_inv_magenta_saucer-spr16.bin)
rp6502_asset(raiders 0x18000 assets/bin/spc_inv_type2_saucer-spr16.bin)
rp6502_asset(raiders 0x18200 assets/bin/spc_inv_plyr1_blue_gunner_new-spr16.bin)
rp6502_asset(raiders 0x18400 assets/bin/spc_inv_blue_gunner_explosion_0-spr16.bin)
rp6502_asset(raiders 0x18600 assets/bin/spc_inv_blue_gunner_explosion_1-spr16.bin)
rp6502_asset(raiders 0x18800 assets/bin/spc_inv_plyr2_yellow_gunner_new-spr16.bin)
rp6502_asset(raiders 0x18A00 assets/bin/spc_inv_yellow_gunner_explosion_0-spr16.bin)
rp6502_asset(raiders 0x18C00 assets/bin/spc_inv_yellow_gunner_explosion_1-spr16.bin)
rp6502_asset(raiders 0x18E00 assets/bin/spc_inv_bonus_magenta_gunner_new-spr16.bin)
rp6502_asset(raiders 0x19000 assets/bin/spc_inv_invader_explosion-spr16.bin)
rp6502_asset(raiders 0x19200 assets/bin/spc_inv_bullet_explosion-spr8.bin)
rp6502_asset(raiders 0x19280 assets/bin/spc_inv_bomb_explosion-spr8.bin)
rp6502_asset(raiders 0x19300 assets/bin/spc_inv_bullet_new-spr8.bin)
rp6502_asset(raiders 0x19380 assets/bin/spc_inv_screw_bomb_0-spr8.bin)
rp6502_asset(raiders 0x19400 assets/bin/spc_inv_screw_bomb_1-spr8.bin)
rp6502_asset(raiders 0x19480 assets/bin/spc_inv_screw_bomb_2-spr8.bin)
rp6502_asset(raiders 0x19500 assets/bin/spc_inv_screw_bomb_3-spr8.bin)
rp6502_asset(raiders 0x19580 assets/bin/spc_inv_spike_bomb_0-spr8.bin)
rp6502_asset(raiders 0x19600 assets/bin/spc_inv_spike_bomb_1-spr8.bin)
rp6502_asset(raiders 0x19680 assets/bin/spc_inv_spike_bomb_2-spr8.bin)
rp6502_asset(raiders 0x19700 assets/bin/spc_inv_spike_bomb_3-spr8.bin)
rp6502_asset(raiders 0x19780 assets/bin/spc_inv_sawtooth_bomb_0-spr8.bin)
rp6502_asset(raiders 0x19800 assets/bin/spc_inv_sawtooth_bomb_1-spr8.bin)
rp6502_asset(raiders 0x19880 assets/bin/spc_inv_sawtooth_bomb_2-spr8.bin)
rp6502_asset(raiders 0x19900 assets/bin/spc_inv_sawtooth_bomb_3-spr8.bin)
rp6502_asset(raiders 0x19980 assets/bin/font5x7_4.bin)
rp6502_executable(raiders
    DATA 0x200
    RESET 0x200
    spc_inv_bunker_template_new-spr32.bin.rp6502
    spc_inv_bunker_0_plyr1_new-spr32.bin.rp6502
    spc_inv_bunker_1_plyr1_new-spr32.bin.rp6502
    spc_inv_bunker_2_plyr1_new-spr32.bin.rp6502
    spc_inv_bunker_3_plyr1_new-spr32.bin.rp6502
    spc_inv_bunker_0_plyr2_new-spr32.bin.rp6502
    spc_inv_bunker_1_plyr2_new-spr32.bin.rp6502
    spc_inv_bunker_2_plyr2_new-spr32.bin.rp6502
    spc_inv_bunker_3_plyr2_new-spr32.bin.rp6502
    spc_inv_saucer_magenta_explosion-spr32.bin.rp6502
    spc_inv_score50-spr32.bin.rp6502
    spc_inv_score100-spr32.bin.rp6502
    spc_inv_score150-spr32.bin.rp6502
    spc_inv_score300-spr32.bin.rp6502
    spc_inv_greenie1-spr16.bin.rp6502
    spc_inv_greenie2-spr16.bin.rp6502
    spc_inv_bluie1-spr16.bin.rp6502
    spc_inv_bluie2-spr16.bin.rp6502
    spc_inv_magentaie1-spr16.bin.rp6502
    spc_inv_magentaie2-spr16.bin.rp6502
    spc_inv_magenta_saucer-spr16.bin.rp6502
    spc_inv_type2_saucer-spr16.bin.rp6502
    spc_inv_plyr1_blue_gunner_new-spr16.bin.rp6502
    spc_inv_blue_gunner_explosion_0-spr16.bin.rp6502
    spc_inv_blue_gunner_explosion_1-spr16.bin.rp6502
    spc_inv_plyr2_yellow_gunner_new-spr16.bin.rp6502
    spc_inv_yellow_gunner_explosion_0-spr16.bin.rp6502
    spc_inv_yellow_gunner_explosion_1-spr16.bin.rp6502
    spc_inv_bonus_magenta_gunner_new-spr16.bin.rp6502
    spc_inv_invader_explosion-spr16.bin.rp6502
    spc_inv_bullet_explosion-spr8.bin.rp6502
    spc_inv_bomb_explosion-spr8.bin.rp6502
    spc_inv_bullet_new-spr8.bin.rp6502
    spc_inv_screw_bomb_0-spr8.bin.rp6502
    spc_inv_screw_bomb_1-spr8.bin.rp6502
    spc_inv_screw_bomb_2-spr8.bin.rp6502
    spc_inv_screw_bomb_3-spr8.bin.rp6502
    spc_inv_spike_bomb_0-spr8.bin.rp6502
    spc_inv_spike_bomb_1-spr8.bin.rp6502
    spc_inv_spike_bomb_2-spr8.bin.rp6502
    spc_inv_spike_bomb_3-spr8.bin.rp6502
    spc_inv_sawtooth_bomb_0-spr8.bin.rp6502
    spc_inv_sawtooth_bomb_1-spr8.bin.rp6502
    spc_inv_sawtooth_bomb_2-spr8.bin.rp6502
    spc_inv_sawtooth_bomb_3-spr8.bin.rp6502
    font5x7_4.bin.rp6502
)
target_include_directories(raiders PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
target_sources(raiders PRIVATE
    src/raiders.c
    src/ezpsg.c
)
