/*
 * raiders.c
 *
 * Written by: Mark Linebaugh
 *
 * This code is free to use, copy, modify and improve ;-)
 *
 */

#include "ezpsg.h"
#include <rp6502.h>
#include <6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raiders.h"
#include "usb_hid_keys.h"
#include <unistd.h>
#include <fcntl.h>

// XRAM MEMORY LAYOUT
//  0x0000 - 0x0200 -- Blank (512)
//  0x0200 - 0x9980 -- Sprite IMAGES (37K)
//  0x9980 - 0xA180 -- Font TABLE (2K)
//  0xB000 - 0xBE10 -- On screen 40x30 CHAR ARRAY (40x30 x3 bytes per char = 3600)
//  0xBE10 - 0xC000 -- Blank (496)
//  0xC000 - 0xC250 -- Sprite Config Array (74 sprites x 8 bytes per sprite = 592)
//  0xFF00 - 0xFF10 -- Character mode config array
//  0xFF10 - 0xFF40 -- Realtime keyboard key state array
//  0xFF40 - 0xFF80 -- Sound FX 8 ch by 8 byte data array

// ###########   KNOWN ISSUES LIST   ###########
// final alien is not exploding
// occasionally a raider will be hit, but remain on the screen, in a stationary position, until the next round of play

// ####################################################################################################
// #################################    ARRAY DECLARATIONS    #########################################
// ####################################################################################################

// DEBUG/PERFORMANCE vars
struct
{
    unsigned char time;
} v_was[256];
struct
{
    unsigned char time;
} v_is[256];

// GAME vars
struct
{
    bool new_game;                 // triggers start of a new game
    bool new_round;                // triggers star of new round of play
    bool level_done;               // successful completion of current level
    unsigned char level_num;       // level # starting at 1
    unsigned char num_players;     // 0 = 1 player, 1 = 2 players
    unsigned hi_score;             // highest score achieved to date (need to store on media to survie power cycle)
    unsigned char cheat_code;      // in case we have some
    bool over;                     // GAME OVER flag to trigger end of game actions
    bool play_mode;                // Indicates normal game play
    bool extended_demo_mode;       // for debug
    bool restart;                  // reboot the game, probably using the reset command after acknowedging on screen receipt of the command
    bool pause;                    // pressing P pauses the game, pressing again resumes
    bool no_inflight_bombs;        // indicates no bombs in flight or blowing up
    bool no_inflight_saucer;       // indicates no saucer in flight or blowing up
    bool no_inflight_bullet;       // indicates no bullets in flight or blowing up
    unsigned char bomb_spawn_time; // time before bombs can be spawned
    bool bomb_spawn_enable;        // true after spawn time delay has expired
} Game;

// one struct for each player to save state when swapping players
struct
{
    unsigned char exists;         // player1 always exists, player2 is optional
    unsigned char lives;          // # lives remaining, initiallly 3, current lives +1 if bonus, 0 when active player's game is over
    bool lives_icons_updated;     // indicates a change (could be up or down) has been made to the # of lives remaining
    unsigned char bonus_achieved; // 0=not achieved, 1=achieved, can only be achieved once per game
    bool bonus_active;            // false=no bonus, true=bonus has occurred and bonus life has not been terminated
    bool bonus_life_incr_done;    // # of lives has been incremented due to bonus
    unsigned level;               // this goes from 1 to 999, it is equal to the highest level started (not finished)
    unsigned char num_of_aliens;
    unsigned char alien_anim;
    bool game_over;                  // turn complete, lost a life, with no lives remaining for player X
    unsigned char index_start_y_pos; // index into 9 variable array, which define upper left of alien matrix starting pos
    signed alien_ref_x;              // matrix position & direction when gunner is terminated
    signed alien_ref_y;
    signed char alien_x_incr;    // right = +2, left equal -2
    unsigned char bullets_fired; // index into saucer score array, needs to be from 0 to 14, then rollover
    unsigned score;
    unsigned hi_score;
    unsigned saucer_spawn_time;  // time remaing before next saucer spawn
    unsigned char alien_1st_col; // these are all indices into arrays
    unsigned char alien_1st_col_rel_x;
    unsigned char alien_last_col;
    unsigned char alien_last_col_rel_x;
    unsigned char col_index_spike; // index to select bomb column from array of columns
    unsigned char col_index_sawtooth;
    unsigned char alien_march_index;
} Player[2];

unsigned char Players_Alien_Exists[2][55];
unsigned char Lives[2][4];         // [player# 0-1][LIVES ICON existence 0 or 1], life 0 is far left, life 3 is far right
unsigned char Alien_width[55];     // Array to hold Width Constants for each specific alien
unsigned int Alien_img_ptr[2][55]; // Array to hold 2 image pointers for all 55 aliens

struct
{
    bool exists;
    bool hit;
    unsigned char explos_ticks;
    bool blown_up; // gunner is dead, is player dead (exists)? are both players dead/game over?
    bool sfx;      // do gunner explosion sound fx
    unsigned char spawn_time;
    unsigned char spawn;
    unsigned x; // current x pos, y pos is static/constant
    unsigned y;
    unsigned char anim_sequ_num; // Gunner = 0, explosion anim = 1 or 2 (they alternate)
    bool direction_right;        // for game play (not demo)
    bool direction_left;
    unsigned loaded; // gun is armed/loaded
    bool shoot;
} Gunner; // one for each player0 = player 1, 1 = player 2 or SHARE???? with just image swaps???

struct
{
    unsigned char exists;
    unsigned char hit; // 0 = no collision, 1 = collision with macro_bunkr, 2 = with micro_bunkr, 3 = bomb, 4 = upper boundary
    unsigned char explos_ticks;
    bool sfx; // one shot sound fx for bullet
    unsigned char toggle_fx;
    unsigned char reload_time; // after gunner (re)spawn, time to (re)spawn bullet
    unsigned char reload;      // signal to spawn new bullet
    bool spawn_enable;
    unsigned x;
    unsigned y;
    unsigned anim_sequ_num; // image # 0 = bullet image, 1 = bullet explosion image
    unsigned x_base;        // starting x,y position (could probably define a constant)
    unsigned y_base;
    unsigned x_path; // retains x position once bullet leaves the barrel
} Bullet;

struct
{
    unsigned char type; // 0 = screw type, 1 = nail/spike, 2 = sawtooth
    bool exists;        // spawned, inflight or exploding
    unsigned char hit;  // 0=no collision, 1=alien, 2=bullet, 3=macro_bunkr, 4=micro_bunkr, 5=gunner, 6=ground
    unsigned char x_aligned_bunkr;
    unsigned char bunkr_macro_hit;
    unsigned bunkr_mem_row_addr; // bunker erosion row/col image memory address
    unsigned bunkr_mem_col_addr;
    bool explos_started;        // false = not started, true = started
    unsigned char explos_ticks; // starting timer value, which decrements each tick
    unsigned char spawn_time;   // time after round starts before bombs can start dropping
    unsigned numbr_steps_taken; // min time since last bomb, before a new bomb can spawn
    unsigned char toggle;
    unsigned char anim_sequ_num;
    int x; // current pos
    int y;
    int x0; // calculate BBOX and update in movement section for use in multiple spots
    int x1;
    int y0;
    int y1;
    unsigned char drop_rel_y;
} Bomb[NUM_BOMB_SPR + 1]; // need an extra slot for the case where last updated = 3 (no bomb was updated)

struct
{
    int x;
    int explosion_x; // position of the saucer explosion, which is determined when/where saucer is hit
    int y;
    bool sfx;
    unsigned char toggle_fx;
    unsigned char score_start_time;
    unsigned char left;
    bool exists;
    bool spawn_enable; // disable spawn while gunner is exploding and until next round
    unsigned next_spawn_time;
} Saucer;

struct
{
    int x;
    unsigned image_ptr;
} Saucer_Score;

// SOUND FX
// 8 channels, 1 structure per channel, 8 bytes per structure, last by of structure is unused
// Decoder ring for the index into the array of structures
//      ch 0 = gunner, 1 = alien, 2 = saucer, 3 = bullet, 4 = alien advance
struct
{
    unsigned starting_address;
    unsigned freq;
    unsigned char freq_lsb;
    unsigned char freq_msb;
    unsigned char duty_cycle;   // For everything but squarewave 255 = 50% duty cycle, for square wave 128 = 50% DC
    unsigned char atten_attack; // LS Nibble = rate of attack, MS Nibble = attenuation level from max volume = 0
    unsigned char atten_decay;  // LS Nibble = rate of decay, MS Nibble = ditto
    unsigned char wave_release; // LS Nibble = rate of release, MS Nibble = waveform 0 = Sin, 1 = Square, 2 = Sawtooth, 3 = Triangle, 4 = Noise
    unsigned char pan_gate;     // LS BIT = sounds on/off, 1 = on, 0 = off, MS 7 BITs = level of left/right pan, from 1 to +/-64 L/R pan
} Sfx[8];

// Matrix Reference Alien's y-axis starting postion table (index increments at level completion)
static const unsigned char Mtrx_Y_Start[9] = {116, 140, 156, 164, 164, 164, 172, 172, 172};

// SAUCER SCORING TABLE - index into this table is number of shots fired by gunner
// thesa are all divided by 10, LSD is always zero
static const unsigned Saucer_Score_Table[15] = {10, 5, 5, 10, 15, 10, 10, 5, 30, 10, 10, 10, 5, 15, 10};

// COLUMN SEQUENCE TABLE for BOMB DROP
// This is the "random" table of column #s, used to sequence columns of sawtooth and spike bombs, screw bombs drop
// ... from the column the player is underneath
// table index is 0 to 20, sawtooth bombs index starts 6 and goes to 20, spike starts at 0 and goes to 15
static const unsigned char Bomb_Column_Sequ[21] = {0, 6, 0, 0, 0, 3, 10, 0, 5, 2, 0, 0, 10, 8, 1, 7, 1, 10, 3, 6, 9};

// ALIEN MARCH SFX RATE TABLES
// 2 tables, 1 defines the threshold of the # of aliens remaining that is used to change MARCH SFX pulse rate
//      the other table defines the pulse rate when the # of aliens is greater than the current threshold
static const unsigned char Alien_March_SFX_Threshold[16] =
    {0x32, 0x2B, 0x24, 0x1C, 0x16, 0x11, 0x0D, 0x0A, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
static const unsigned char Alien_March_SFX_Rate[16] =
    {0x34, 0x2E, 0x27, 0x22, 0x1C, 0x18, 0x15, 0x13, 0x10, 0x0E, 0x0D, 0x0C, 0x0B, 0x09, 0x07, 0x05};

// string declarations, with init values
char score_value_str[] = "0000";
char level_num_str[] = "1  ";
char lives_num_str[] = "3";

// GAME VARS
unsigned char active_player;
bool round_is_over, another_player;

// VIA 6522 Timer Interrupts - Used for performance (execution time) measurements
unsigned VIA_irq_count;
unsigned char initial_vsync;
uint8_t irq_stack[256];
unsigned char values[50];
unsigned int max_duration_values[50];
unsigned char values_position;
unsigned int time_ticks[50];
volatile unsigned char *via_base = (volatile unsigned char *)VIA_BASE;
volatile unsigned char *via_count_l = (volatile unsigned char *)VIA_CNTR_1_LSB;
volatile unsigned char *via_count_h = (volatile unsigned char *)VIA_CNTR_1_MSB;
volatile unsigned char *via_acr = (volatile unsigned char *)VIA_ACR;
volatile unsigned char *via_ifr = (volatile unsigned char *)VIA_IFR;
volatile unsigned char *via_ier = (volatile unsigned char *)VIA_IER;

// SFX VARS
unsigned frequency, bullet_freq;
unsigned char loops, bullet_loops;
unsigned char wave, duty;
unsigned char atten_attack, attack_time, attack_volume_atten;
unsigned char atten_decay, decay_time, decay_volume_atten;
unsigned char wave_release, release_time;
unsigned char pan_gate, pan, gate;
bool ramp_up;
unsigned char toggle_tones;
unsigned char alien_march_note_sequ;
unsigned char alien_march_sfx_start;

// DEBUG VARS
unsigned loop_count_A, loop_count_A_snapshot, loop_count_B, loop_count_C, loop_count_D;
bool bypass_test_mode;

// ####################################################################################################
// ##################################### -= FUNCTIONS =- ##############################################
// ####################################################################################################

unsigned char new_delay(ticks)
{
    unsigned i, j, k;
    static uint8_t v;
    unsigned char tempz;
    RIA.addr1 = KEYBOARD_INPUT;
    RIA.step1 = 0;
    v = RIA.vsync;
    for (k = 0; k < ticks; k++)
    { // # of ticks loop
        for (j = 0; j < 1; j++)
        { // NOT USED -- 1/60th of a second loop
            for (i = 0; i < 2548; i++)
            { // watchdog timer ... loop until vsync is incremented
                if (v != RIA.vsync)
                {
                    tempz = RIA.rw1 & 1;
                    if (!tempz)
                    {
                        RIA.addr1 = KEYBOARD_INPUT + 3;
                        keystates[3] = RIA.rw1;
                        if ((keystates[3] & 64))
                        { // quit demo, start 1 player game
                            return 1;
                        }
                        else if ((keystates[3] & 128))
                        { // quit demo, start 2 player game
                            return 2;
                        }
                        else
                            return 3;
                        keystates[3] = 0;
                    }
                    v = RIA.vsync;
                    break;
                }
            }
        }
    }
}

static void delay(unsigned tenths_of_seconds)
{
    unsigned i, j, k;
    static uint8_t v;
    v = RIA.vsync;
    for (k = 0; k < tenths_of_seconds; k++)
    { // seconds loop
        for (j = 0; j < 1; j++)
        { // 1/60th of a second loop
            for (i = 0; i < 2548; i++)
            { // loop until vsync is incremented
                if (v != RIA.vsync)
                {
                    v = RIA.vsync;
                    break;
                }
            }
        }
    }
}

unsigned char new_wait()
{ // wait until key is pressed
    unsigned char tempz;
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;
    while (1)
    {
        tempz = RIA.rw0 & 1;
        if (!tempz)
        {
            return 1;
        }
    }
}

// static void wait()
// {
//     uint8_t discard;
//     discard = RIA.rx;
//     while (RIA.ready & RIA_READY_RX_BIT)
//         discard = RIA.rx;
//     while (!(RIA.ready & RIA_READY_RX_BIT))
//         ;
//     discard = RIA.rx;
// }

// clear entire character screen
// each char position has 3 bytes --> the character, fg color, bg color
// use the space character for initialization and bg color is black (transparent)
static void initialize_char_screen()
{
    unsigned i;
    RIA.addr0 = 0xB000;
    RIA.step0 = 1;
    for (i = 0; i < 40 * 30; i++)
    {
        RIA.rw0 = ' '; // char is blank
        // set colors for the various sections
        if (i >= 1175 && i < 1188)
            RIA.rw0 = 12; // blue "PLAYER <x>"
        else if (i >= 1080 && i < 1120)
            RIA.rw0 = 13; // purple "PLAYER <x>"
        else if (i >= 560 && i < 600)
            RIA.rw0 = 10; // yellow "SELECT NUMBER OF PLAYERS"
        else if (i >= 360 && i < 400)
            RIA.rw0 = 10; // yellow "PLAYER <x>"
        else if (i >= 240 && i < 280)
            RIA.rw0 = 13; // purple "PLAYER <x>"
        else if (i >= 200 && i < 240)
            RIA.rw0 = 9; // red "GAME OVER"
        else if (i >= 160 && i < 200)
            RIA.rw0 = 13; // purple "-Demo-"
        else if (i >= 97 && i < 101)
            RIA.rw0 = 11; // yellow "BONUS"
        else if (i >= 0 && i < 40)
            RIA.rw0 = 12; // blue "SCORE..." etc
        else
            RIA.rw0 = 15; // white
        RIA.rw0 = 0;      // bg color = black
    }
}

static void clear_char_screen(unsigned first_row, unsigned last_row)
{
    unsigned i;
    RIA.addr0 = 0xB000 + (first_row * 40 * 3);
    RIA.step0 = 3;
    for (i = 0; i < 40 * (last_row - first_row + 1); i++)
    {
        RIA.rw0 = ' ';
    }
}

// print a string, starting at character row/col, fast or slow print based on 'slow' flag
unsigned char print_string(unsigned row, unsigned col, char *str, bool slow)
{ // assumes no color change (text is white on black)
    bool skip = false;
    RIA.addr0 = 0xB000 + (((row * 40) + col) * 3);
    RIA.step0 = 3;
    while (*str)
    {
        RIA.rw0 = *str++;
        if (slow)
        {
            skip = new_delay(6);
            if (skip)
                return 1;
        }
    }
}

static void erase_xram_sprite_config(void)
{
    unsigned i;
    RIA.addr0 = 0xC000; // Base address of all volatile sprite config data
    RIA.step0 = 1;
    for (i = 0; i < 48; i++)
    { // clears 0x300 (48 * 16 = 768) bytes, actual sprite config data is just 0x250 bytes
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
        RIA.rw0 = 0;
    }
}

// BUNKER ERASE DUE TO ALIEN/BUNKER COLLISION - erase bunker as an alien overlaps one of the bunkers
void erase_top_of_bunkr2(unsigned mem_start, unsigned char num_col, unsigned char lower_half)
{
    unsigned char i = 0;
    RIA.addr1 = mem_start + (lower_half * 512);
    while (i < num_col)
    {
        if (i % 2 == 0)
            RIA.step1 = 64;
        else
            RIA.step1 = -64;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.rw1 = 0;
        RIA.step1 = 2;
        RIA.rw1 = 0;
        i++;
    }
}

// BLAST HOLE IN BUNKER at location of bullet
void erase_bunkr_bullet_explos(unsigned mem_start)
{
    unsigned char i = 0, temp1;
    RIA.addr1 = mem_start; // template image starting address
    RIA.addr0 = BULLET_EXPL_IMG_BASE;
    while (i < 8)
    {
        if (i % 2 == 0)
        {
            RIA.step1 = 64;
            RIA.step0 = 16;
        }
        else
        { // reverse vertical direction, every other column
            RIA.step1 = -64;
            RIA.step0 = -16;
        }
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        RIA.step0 = 2;
        RIA.step1 = 2; // move to next column in memory, on next write
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        i++;
    }
}

unsigned char bunkr_bullet_micro_collision(unsigned mem_start)
{ // find a pixel level collision
    unsigned char result = 0;
    RIA.addr0 = mem_start;
    RIA.step0 = 64;
    result |= RIA.rw0; // looking for any non-zero value in bunker image memory, in area that overlaps bullet
    result |= RIA.rw0;
    result |= RIA.rw0;
    result |= RIA.rw0;
    return result;
}

unsigned char bunkr_bomb_micro_collision(unsigned bunker_mem_start, unsigned bomb_mem_start)
{
    unsigned char result = 0, i = 0, temp1 = 0, temp2 = 0, tally = 0;
    RIA.addr0 = bomb_mem_start;
    RIA.addr1 = bunker_mem_start;
    while (i < 3)
    {
        if (i % 2 == 0)
        {
            RIA.step1 = 64;
            RIA.step0 = 16;
        }
        else
        {
            RIA.step1 = -64;
            RIA.step0 = -16;
        }
        result |= RIA.rw0 & RIA.rw1; // looking for any non-zero value in bunker image memory, in area that overlaps bullet
        result |= RIA.rw0 & RIA.rw1;
        result |= RIA.rw0 & RIA.rw1;
        result |= RIA.rw0 & RIA.rw1;
        result |= RIA.rw0 & RIA.rw1;
        result |= RIA.rw0 & RIA.rw1;
        result |= RIA.rw0 & RIA.rw1;

        RIA.step0 = 2;
        RIA.step1 = 2;
        result |= RIA.rw0 & RIA.rw1;
        i++;
    }
    return result;
}

// BLAST HOLE IN BUNKER at location of bomb
void erase_bunkr_bomb_explos(unsigned mem_start)
{
    unsigned char i = 0, temp1;
    RIA.addr1 = mem_start; // template image starting address
    RIA.addr0 = BOMB_EXPL_IMG_BASE;
    while (i < 6)
    { // BULLET IMAGE starts at col 0 and has 6 columns, columns 7 & 8 are blank
        if (i % 2 == 0)
        {
            RIA.step1 = 64;
            RIA.step0 = 16;
        }
        else
        {
            RIA.step1 = -64;
            RIA.step0 = -16;
        }
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        RIA.step0 = 2;
        RIA.step1 = 2;
        if (RIA.rw0 > 0)
            RIA.rw1 = 0;
        else
            temp1 = RIA.rw1;
        i++;
    }
}

// BULLET to BOMB Pixel level colliion detection
unsigned char bullet_bomb_micro_collision(unsigned int bomb_image_memory_base, int mem_top_row, int mem_column, unsigned char num_rows)
{
    unsigned char i, result, temp;
    result = 0;
    RIA.step0 = 16;
    RIA.addr0 = bomb_image_memory_base + (mem_column * 2) + (mem_top_row * 16);
    for (i = 0; i < num_rows; i++)
    { //
        temp = RIA.rw0;
        if (temp > 0)
        {
            result = 1;
            break;
        }
    }
    return result;
}

// Restore pristine bunkers
void restore_bunkers(unsigned char active_player)
{
    unsigned char j, temp;
    unsigned i;
    RIA.step0 = 1;
    RIA.step1 = 1;
    RIA.addr0 = BUNKR_0_PLYR1_IMG_BUF; // point to first bunker (0) image buffer for the current player
    if (active_player == 1)
        RIA.addr0 = BUNKR_0_PLYR2_IMG_BUF;
    for (j = 0; j < 4; j++)
    {
        RIA.addr1 = BUNKR_PRISTINE_IMG_BASE; // start of bunker template memory buffer
        for (i = 0; i < 2048; i++)
        {
            temp = RIA.rw1;
            RIA.rw0 = temp;
        }
    }
}

void update_numerical_lives(bool blink_lives, unsigned char num_of_lives_remaining)
{
    unsigned char blink_cycles, skip;
    strcpy(lives_num_str, " ");
    lives_num_str[0] = (char)num_of_lives_remaining + '0';
    if (blink_lives)
    {
        for (blink_cycles = 0; blink_cycles < 10; blink_cycles++)
        {
            if (active_player == 1)
            {
                print_string(29, 34, " ", false);
                skip = new_delay(10);
                print_string(29, 34, lives_num_str, false);
                skip = new_delay(10);
            }
            else
            {
                print_string(29, 5, " ", false);
                skip = new_delay(10);
                print_string(29, 5, lives_num_str, false);
                skip = new_delay(10);
            }
            if (skip)
                return;
        }
    }
    else
    {
        if (active_player == 1)
            print_string(29, 34, lives_num_str, false);
        else
            print_string(29, 5, lives_num_str, false);
    }
}

void update_wave_number(bool top_bottom)
{
    unsigned calc_1, calc_2;
    strcpy(level_num_str, "   ");
    calc_1 = Player[active_player].level;
    calc_2 = calc_1;
    if (calc_1 > 100 && calc_1 < 1000)
    {
        calc_1 /= 100;
        level_num_str[0] = (char)calc_1 + '0';
        calc_2 = calc_2 - (calc_1 * 100);
        calc_1 = calc_2 / 10;
        level_num_str[1] = (char)calc_1 + '0';
        calc_2 = calc_2 - (calc_1 * 10);
        level_num_str[2] = (char)calc_2 + '0';
    }
    else if (calc_1 > 10)
    {
        calc_1 = calc_2 / 10;
        level_num_str[0] = (char)calc_1 + '0';
        calc_2 = calc_2 - (calc_1 * 10);
        level_num_str[1] = (char)calc_2 + '0';
    }
    else
    {
        level_num_str[0] = (char)calc_2 + '0';
    }
    if (top_bottom)
        print_string(9, 15, level_num_str, false);
    else
        print_string(29, 20, level_num_str, false);
}

void update_score_string(unsigned int score)
{
    unsigned calc_1, calc_2;
    calc_1 = score;
    calc_2 = calc_1;
    calc_1 /= 100;                           // 1000's digit
    score_value_str[0] = (char)calc_1 + '0'; // convert to ascii char, built "score" string
    calc_2 = (calc_2 - (calc_1 * 100));
    calc_1 = calc_2 / 10; // 100's digit
    score_value_str[1] = (char)calc_1 + '0';
    calc_2 = (calc_2 - (calc_1 * 10));
    calc_1 = calc_2; // 10's digit
    score_value_str[2] = (char)calc_1 + '0';
}

void update_score_board()
{
    unsigned ptr;
    update_score_string(Player[active_player].score);
    // update player score character string
    if (active_player == 1)
        print_string(2, 27, score_value_str, false); // update 2nd player's score
    else
        print_string(2, 7, score_value_str, false); // update 1st player's score
    if (Player[active_player].score > Game.hi_score)
    {
        Game.hi_score = Player[active_player].score;
        print_string(2, 17, score_value_str, false);
    }
    if ((Player[active_player].bonus_achieved < 1) && (Player[active_player].score > 150))
    {
        Player[active_player].bonus_achieved = 1;
        Player[active_player].bonus_active = true;
        ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + ((Player[active_player].lives - 1) + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, LIVES_BONUS_IMG_BASE); // use magenta image for bonus ICONs
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, LIVES_Y_BASE);                // reappear this sprite
        Player[active_player].lives++;
        update_numerical_lives(false, Player[active_player].lives);
    }
}

// NOT USED... compile/link requires it based on include file structure in PSG, haven't gotten around to fixing it
void ezpsg_instruments(const uint8_t **data)
{
    switch ((int8_t)*(*data)++) // instrument
    {
    case -1:                  // hihat
        ezpsg_play_note(e5,   // note
                        2,    // duration
                        0,    // release
                        12,   // duty
                        0x61, // vol_attack
                        0xF7, // vol_decay
                        0x10, // wave_release
                        0);   // pan
        break;
    case -2:                        // kick
        ezpsg_play_note(d1,         // note
                        *(*data)++, // duration
                        0,          // release
                        32,         // duty
                        0x01,       // vol_attack
                        0xF9,       // vol_decay
                        0x40,       // wave_release
                        0);         // pan
        break;
    case -3:                  // snare
        ezpsg_play_note(cs3,  // note
                        4,    // duration
                        0,    // release
                        64,   // duty
                        0x01, // vol_attack
                        0xF8, // vol_decay
                        0x40, // wave_release
                        0);   // pan
        break;
    case -4:                        // bass
        ezpsg_play_note(*(*data)++, // note
                        *(*data)++, // duration
                        1,          // release
                        192,        // duty
                        0x70,       // vol_attack
                        0xF9,       // vol_decay
                        0x34,       // wave_release
                        0);         // pan
        break;
#ifndef NDEBUG
    default:
        // The instrumment you just added probably isn't
        // consuming the correct number of paramaters.
        puts("Unknown instrument.");
        exit(1);
#endif
    }
}

void load_SFX_base_parameters(unsigned int channel)
{
    // assemble bytes from nibbles
    atten_attack = (attack_volume_atten << 4) | attack_time;
    atten_decay = (decay_volume_atten << 4) | decay_time;
    wave_release = (wave << 4) | release_time;
    // load parameters for the sequence
    RIA.addr0 = channel;
    RIA.step0 = 1;
    RIA.rw0 = frequency & 0xFF;
    RIA.rw0 = (frequency >> 8) & 0xFF;
    RIA.rw0 = duty;
    RIA.rw0 = atten_attack;
    RIA.rw0 = atten_decay;
    RIA.rw0 = wave_release;
    RIA.rw0 = 0;
}

// IRQB INTERRUPT HANDLER (from OS VSYNC and from 6522VIA timer)
unsigned char irq_fn(void)
{
    // check to see if the IRQ was from the VIA (read Interrupt Flag Register, bit 6 = 1?)
    if ((*via_ifr & 0xC0) > 0)
    {                    // VIA interrupt occurred, bit 7 IRQB asserted, bit 6 Interrupt for Timer 1
        ++VIA_irq_count; // number of IRQ cycles triggered by timer
        *via_ifr = 0x40; // bit 7 = 0 to clear bits, bit 6 = 1 to clear bit 6 (Timer 1 IRQ)
    }
    return 1; // after ISR, return IRQ_HANDLED = 1
}

// GAME LOGIC VARS
bool inflight_complete;
bool level_completed;

// ALIEN HANDLER VARS
unsigned char alien_num;
bool Alien_update[55]; // updated (or not) state for each alien
bool alien_ref_update; // toggles on each pass to distinquich which aliens match the current vs prior reference
int alien_x, alien_y;
unsigned char alien_hit; // flag indicating collision
unsigned char alien_x_hit;
unsigned char alien_y_hit;
unsigned char num_of_alien_hit; // index # for alien that has been hit by bullet
unsigned char alien_explos_ticks;
bool alien_explosion_done;
unsigned char alien_roll_over;
unsigned char alien_drop;
bool alien_landed;
signed char alien_y_incr;      // +0 or +8 (used to drop the alien matrix 8 px
signed alien_ref_x;            // lower left refence pos of the alien matrix, uses byte arithm until sprite pos is updated
signed alien_ref_y;            // ditto
unsigned char Alien_rel_x[55]; // x/y position relative to ref x/y position
signed char Alien_rel_y[55];
unsigned char Alien_bbox_x0[55];
unsigned char Alien_bbox_x1[55];
unsigned char Alien_bbox_y0[55];
unsigned char Alien_bbox_y1[55];
unsigned char alien_row_num; // alien matrix vars
unsigned char alien_col_num;
unsigned char alien_col_bomb_0;                     // col # selected for dropping bomb 0 (targeted bmmb)
unsigned char alien_num_to_drop_bomb_from;          // col # selected for dropping bomb 1 and 2 (per column table)
unsigned char Alien_unoccupied_rows_per_col[2][11]; // for each of the 11 cols, # of unoccupied rows is between 0 and 5, 5 = not occupied
unsigned char Alien_unoccupied_cols_per_row[2][5];  // for each of 5 rows, # of unoccupied cols is between 0 and 11, 11 = not occupied
int alien_1st_col_abs_x;                            // absolute screen x/y position
int alien_last_col_abs_x;
unsigned char alien_anim; // animation sequ #
bool alien_explosion_sfx_enable;
bool alien_march_sfx_enable;
unsigned char alien_march_sfx_timer;

// ####################################################################################################
// ##################################### -= MAIN =- ###################################################
// ####################################################################################################
void main()
{
    // FOR LOOP VARS and such
    int i = 0, j = 0, k = 0, q = 0, r = 0, s = 0;
    int v = 0;

    // TEST vars
    unsigned char rounds_completed = 0;

    // GAME VARS
    FILE *fptr;
    bool demo_terminated;

    // LIVES
    unsigned lives_spr_ptr, lives_x_pos, lives_y_pos;

    // SPRITE VARS
    unsigned ptr;
    unsigned char sprite_number;

    // ALIENS
    bool skip_alien_sprite_update;

    // BUNKERS
    unsigned bunkr_x_pos, bunkr_img_ptr;
    unsigned char lower_half_bunkr;

    // GUNNER - active gunner x,y position, image ptr, # of lives indicators base x,y pos
    unsigned gunner_image_ptr;
    bool gunner_demo_direction_right; // Sprite starting direction

    // SAUCER - x,y position, image ptr, entry left/right, direction left/right, image/color,
    unsigned saucer_expl_score_image_ptr;

    // BULLET - x,y pos, base x,y pos, x path, image ptr
    int bullet_x, bullet_y, bullet_x0, bullet_y0, bullet_x1, bullet_y1;
    unsigned bullet_y_base, bullet_x_path, bullet_image_ptr;

    // BOMBS - x,y pos, base x,y, x path, image ptr, animation #, speed
    unsigned bomb_image_ptr;
    unsigned char bomb_type_counter, bomb_type_selector;
    unsigned char bomb_screw_skip;
    unsigned char bomb_reload_index;
    unsigned char bomb_speed;
    unsigned char bomb_reload_rate;

    // TIMING VARS
    unsigned char current_time, heartbeat_sfx_timer;

    // BULLET COLLISIONS - Bullet collision flags for bunker, bomb, alien, saucer, upper boundary
    unsigned char bullet_hit;
    unsigned char bullet_hit_subset1, bullet_hit_subset2;
    unsigned char bullet_bomb_hit;
    unsigned char bullet_macro_bunkr_hit, bullet_micro_bunkr_hit;
    unsigned char bullet_alien_hit;
    unsigned char bullet_saucer_hit;
    unsigned char bullet_boundary_hit;

    // Bullet with Bomb, Bomb with Bullet
    int ShotOverlapTop, ShotOverlapBottom, ShotColumn;
    int BombTopRow, BombRows;
    unsigned bomb_image_mem;

    // Bomb collisions
    bool bomb_micro_bunkr_hit;

    // Bullet with Bunker
    unsigned bunkr_img_base_addr;
    unsigned bunkr_start_addr, bunkr_start_addr1, bunkr_start_addr2;

    // Bomb with Bunker
    unsigned int bomb_img_start_addr, bomb_img_addr_offset1, bomb_img_addr_offset2;
    int BunkrBombOverlapLeft, BunkrBombOverlapRight;
    int BunkrBombOverlapTop, BunkrBombOverlapBottom;
    int BunkrExplosionOverlapLeft, BunkrExplosionOverlapRight;
    int BunkrExplosionOverlapTop, BunkrExplosionOverlapBottom;
    unsigned char temp_rw0, temp_rw1;

    // Bunker
    unsigned char bunkr_num;
    unsigned char bunkr_num_col;

    // Vars In Commmon
    int delta_x, delta_y;      // used to determine relative position of object relative to other object
    unsigned char delta_rel_x; // used to determine relative position of an object within another object's boundaries
    unsigned char delta_rel_y;

    // Keyboard input handler
    bool skip;
    bool paused, handled_key;

    // print to screen
    static const bool slow = true; // speed to print characters to screen
    static const bool blink = true;

    // Splash Screens
    unsigned char splash_screen_toggle = 0; // alternating between splash screens

    // Used for FAST OS read/write - dummy read to auto advance XRAM address
    unsigned char dummy_read;

    // PERFORMANCE VARS
    unsigned max_duration = 0;
    unsigned loop_count_at_max_duration = 0;

    // DEBUG
    unsigned int play_loop_time = 0, play_loop_time_max = 0; // for detection performance issues (not completing tasks in one tick)
    unsigned char bomb_num_just_updated;
    unsigned temp1, temp2;

    // set to zero in case this is the first ime the game is played, will be overwritten if hiscore file exists on USB drive
    Game.hi_score = 0;
    demo_terminated = false;

    // Load HIGH SCORE from 'hiscore' file on USB Drive,
    delay(120);
    // without this delay, fopen appears to be unreliable when code is INSTALLED in the pico's EPROM
    // ... seems to be ok when loading from USB drive or USB serial
    fptr = fopen("raiders.hiscore", "rb");
    /*
    // FILE OPEN - ERROR HANDLER
    if (file != NULL) {
        printf("USB DRIVE: Hi-score file is missing\n");
        delay(240);
    }
    */
    fread(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
    fclose(fptr);

    // **********************
    // NEW GAME INITIALIZATON
    // **********************

    // ##################################################################################################
    // #############################    BOOT (Game Initialization) LOOP    ##############################
    // ##################################################################################################
    // Do startup sequence, if no input, play demo, if input, get # players, wait for start button, hand-off to new level code

    // ###########    INITIALIZE GAME VARS    #############
    // This loop should perform all initialization necessary to restart the game from scratch
    while (1)
    { // Init Boot & Restart Game Loop
        // DEBUG VAR Initialization
        loop_count_A = 0; // count of iterations through PRIMARY PLAY (ROUND) loop
        loop_count_B = 0; // count of iterations through CONTROL loop
        loop_count_C = 0; // count of iterations through BOOT loop
        loop_count_A_snapshot = 0;

        // GAME VAR/ARRAY INITIALIZATION
        Game.cheat_code = 0;     // in case we have some, 1 enables
        Game.restart = false;    // pressing R restarts the game
        Game.pause = false;      // pressing P pauses the game, pressing again resumes
        level_completed = false; // current LEVEL/WAVE has been completed (all aliens terminated)
        srand(15);               // Initialize random # gen
        active_player = 0;       // 1st player (player 0) always goes first
        skip = false;            // flag to skip splash screens and go straight to demo/play

        // Initialization code that should be skipped when demo mode has been running and is then  terminated
        if (demo_terminated)
        {
            demo_terminated = false;
            print_string(2, 7, "0000", !slow);
            if (Game.num_players == 1)
                print_string(2, 27, "0000", !slow);
            // clear screen
            clear_char_screen(7, 22);
            for (i = 0; i < TOTAL_NUM_SPR; i++)
            {
                ptr = SPR_CFG_BASE + (i * sizeof(vga_mode4_sprite_t));
                if (i == 4 || i == 60)
                {
                    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
                }
                else
                {
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                }
            }
        }
        else
        {
            Game.play_mode = false; // press 1 or 2 (players, if no input is received, then demo mode is initiatied
            Game.num_players = 1;   // 0 = 1 player, 1 = 2 players0

            // #######################################################
            // ##############    INITIALIZE GRAPHICS    ##############
            // #######################################################
            // screen is 320 x 240 pixels screen OR 40 cols by 30 rows characters
            xreg_vga_canvas(1);

            // DEBUG - if needed to display console text for DEBUG
            // xreg_vga_mode(0, 0);    // setup console in plane 0 for debug purposes, remove when not in debug mode
            // for (i = 0; i<24; i++) puts("\n");      // clear console screen

            // Config character mode
            xram0_struct_set(0xFF00, vga_mode1_config_t, x_wrap, false);
            xram0_struct_set(0xFF00, vga_mode1_config_t, y_wrap, false);
            xram0_struct_set(0xFF00, vga_mode1_config_t, x_pos_px, 0);
            xram0_struct_set(0xFF00, vga_mode1_config_t, y_pos_px, 0);
            xram0_struct_set(0xFF00, vga_mode1_config_t, width_chars, 40);
            xram0_struct_set(0xFF00, vga_mode1_config_t, height_chars, 30);
            xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, 0xB000);    // address of 40x30 char array
            xram0_struct_set(0xFF00, vga_mode1_config_t, xram_palette_ptr, 0xFFFF); // using built-in color palette
            xram0_struct_set(0xFF00, vga_mode1_config_t, xram_font_ptr, 0x9980);    // address of old schoole 5x7 font array
            // Turn on CHARACTER graphics mode
            xreg_vga_mode(1, 3, CHAR_MODE_CFG, 1); // setup char mode in plane 1
            // Turn on SPRITE graphics mode
            erase_xram_sprite_config(); // but first... clear all XRAM SPACE used for sprite config arrays and screen char 40x30 array
            // config sprites necessary for splash screens
            // setup just one each of the alien and saucer sprites for the "SCORING TABLE", later will totally reconfig all sprites
            ptr = SPR_CFG_BASE + (5 * sizeof(vga_mode4_sprite_t)); // SAUCER FIRST
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, 96);
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, SAUCER_IMG_BASE);
            xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
            xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            for (i = 0; i < 3; i++)
            {
                ptr = SPR_CFG_BASE + ((6 + i) * sizeof(vga_mode4_sprite_t)); // THEN 3 ALIENS
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, 96);
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, INVR_IMG_GREENIE + (i * 2 * SPR_16X16_SIZE));
                xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
                xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            }
            // Define SPRITE config structure base address and total # of sprites (length of config structure)
            xreg_vga_mode(4, 0, SPR_CFG_BASE, TOTAL_NUM_SPR); // setup sprite mode in plane 0
            // first clear screen
            initialize_char_screen();
            delay(90); // this appears to be necessary to give the OS a chance to finish it's business before
                       // ... displaying anything

            // ######################################
            // ########  Keyboard operation  ########
            //      first, read 32 bytes of key state data from xram keyboard structure (currently at 0xFF10 to 0xFF2F)
            //      check first byte for LSBit = 0, indicating key has been pressed
            //      use this algorithm to find which bit of the 256 possible bits are set to '1'
            //          key(code) (keystates[code >> 3] & (1 << (code & 7)))
            //      by using the 'CODE' for the key of interest to find/check the bit in the array of 256 bits that represents the key
            //      to see if it's a '1'
            // KEY PRESSES
            // 1 = 1 player, 2 = 2 players, d = demo mode, < = gunner left, > = right, space or up or down arrow = fire, p = pause, ESC = quit, S = save game
            // Turn on USB keyboard I/O
            xreg_ria_keyboard(KEYBOARD_INPUT); // keyboard data is at 0xFF10
            paused = false, handled_key = false;

            // ######################################
            // #######   SPLASH SCREENS   ###########
            // load initial splash screen (scoring table), start with common/static elements used on (most) all screens
            print_string(0, 6, "SCORE<1> HI-SCORE SCORE<2>", !slow);
            print_string(2, 7, "0000", !slow);
            print_string(2, 27, "0000", !slow);
            update_score_string(Game.hi_score);
            print_string(2, 17, score_value_str, false);

            // used to toggle between splash screens (scoring talbe and) # players screens) after each demo run
            splash_screen_toggle = 1 - splash_screen_toggle;

            // NEXT load the 1st of 2 splash screens, pause for input, if none, transition to demo mode, then alternate splash 1, demo, splash 2,... until
            //      # players is selected and PlAY commences
            // SPLASH SCREEN LOOP
            while (1)
            { // using while(1)/break command to enable halting the screen print and moving directly to next bit of code
                if (splash_screen_toggle == 0)
                { // Load first screeen, then alternate after each demo run
                    if (print_string(7, 17, "PLAY", slow))
                        break; // use 'slow' to flag text that should be printed slowly
                    if (print_string(10, 13, "SPACE RAIDERS", slow))
                        break;
                    new_delay(70); // for dramatic effect
                    if (print_string(14, 12, "*SCORING TABLE*", !slow))
                        break;
                    new_delay(70);
                    // unlike orig display and text to the right of it, one row at a time
                    ptr = SPR_CFG_BASE + (5 * sizeof(vga_mode4_sprite_t));
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, 124);
                    if (print_string(16, 15, "= ??", !slow))
                        break;
                    if (print_string(16, 20, "MYSTERY", slow))
                        break;
                    for (i = 0; i < 3; i++)
                    {
                        ptr = SPR_CFG_BASE + ((6 + i) * sizeof(vga_mode4_sprite_t));
                        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, 140 + (i * 16));
                        switch (i)
                        {
                        case 0:
                            skip = print_string(18, 15, "= 30 POINTS", slow);
                            break;
                        case 1:
                            skip = print_string(20, 15, "= 20 POINTS", slow);
                            break;
                        case 2:
                            skip = print_string(22, 15, "= 10 POINTS", slow);
                            break;
                        }
                        if (skip)
                            break;
                    }
                }
                // load uniques for 2nd screen
                else
                {
                    if (print_string(14, 7, "*SELECT NUMBER OF PLAYERS*", !slow))
                        break; // use 'slow' to flag text that should be printed slowly
                    if (print_string(17, 12, "<1 OR 2 PLAYERS>", slow))
                        break;
                }
                break;
            } // END SPLASH SCREEN WHILE LOOP

            // pause for input for a couple seconds, if no input, move on with default = 'demo mode'
            new_delay(180); // pause for input for a couple seconds, if none move on with default = 'demo mode'

            // ############   GET KEYBOARD INPUT   ##############
            // ##################################################
            RIA.addr0 = KEYBOARD_INPUT;
            RIA.step0 = 1;
            while (!handled_key)
            {
                for (i = 0; i < KEYBOARD_BYTES; i++)
                    keystates[i] = RIA.rw0;
                if (!handled_key && (keystates[2] & 8))
                { // pause
                    paused = !paused;
                }
                else if (!handled_key && key(KEY_R))
                { // UNUSED restart game0
                }
                else if (!handled_key && key(KEY_ESC))
                { // exit game
                    fptr = fopen("raiders.hiscore", "wb+");
                    fwrite(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
                    fclose(fptr);
                    exit(0);
                }
                else if (!handled_key && key(KEY_D))
                { // demo mode
                    // Demo is the default this just allows us to start the demo on command instead of waiting for delay
                    Game.play_mode = false;
                    Game.num_players = 1;
                    break;
                }
                else if (!handled_key && key(KEY_X))
                { // eXtended demo mode
                    // not used
                    printf("switch to extended DEMO mode for DEBUG\n");
                    break;
                }
                else if (!handled_key && key(KEY_1))
                { // play mode - 1 player
                    Game.num_players = 0;
                    Game.play_mode = true;
                    break;
                }
                else if (!handled_key && key(KEY_2))
                {                         // play mode - 2 players
                    Game.num_players = 1; // "1" means there are 2 players
                    Game.play_mode = true;
                    break;
                }
                handled_key = true;
            }
        }
        // if 2 players, initialize score text for 2nd player and display Player <1> for first player to play
        if (Game.num_players == 0)
            print_string(2, 27, "    ", !slow);
        // PLAYER "STATE" ARRAY INITIALIZATION for both players
        //      this is updated at end of a round to allow continuation with next life (if available)
        for (i = 0; i < 2; i++)
        {
            Player[i].exists = 0;
            Player[i].lives = 3;
            Player[i].lives_icons_updated = false;
            Player[i].bonus_achieved = 0; // 0 = not achived, 1=achieve, 2=achieved but no longer active
            Player[i].bonus_active = false;
            Player[i].bonus_life_incr_done = false;
            Player[i].num_of_aliens = 55;
            Player[i].alien_anim = 1;
            Player[i].game_over = false;               // active player's game is over if true
            Player[i].level = 1;                       // current level being attempted range is 1 to 999
            Player[i].index_start_y_pos = 0;           // selects starting y pos of matrix upper left, rolls over at 8 to 1 (not zero)
            Player[i].alien_ref_x = INVR_MTRX_START_X; // matrix position & direction when gunner is terminated
            Player[i].alien_ref_y = INVR_MTRX_B_START_Y;
            Player[i].alien_x_incr = 2; // right = +2, left equal -2
            Player[i].alien_1st_col = 0;
            Player[i].alien_1st_col_rel_x = 0;
            Player[i].alien_last_col = 10;
            Player[i].alien_last_col_rel_x = 176;
            Player[i].col_index_spike = 0;    // index to select column for next bomb drop
            Player[i].col_index_sawtooth = 6; // index to select column for next bomb drop
            Player[i].bullets_fired = 0;      // saved at termination of round if triggered by loss of life
            Player[i].score = 0;
            Player[i].hi_score = 0;
            Player[i].saucer_spawn_time = 0; // time remaing before next spawn, saved at end of round
            Player[i].alien_march_index = 0;
        }
        // All aliens exist for both players
        for (j = 0; j < 2; j++)
        {
            for (i = 0; i < 55; i++)
            {
                Players_Alien_Exists[j][i] = 1;
            }
        }
        // Used to find empty colummns in the alien matrix
        for (i = 0; i < 2; i++)
        {
            for (j = 0; j < 11; j++)
            {
                Alien_unoccupied_rows_per_col[i][j] = 0; // when terminated bullet handler will add 1 to the occupied cols, when = 5, then col is empty
            }
            for (j = 0; j < 5; j++)
            {
                Alien_unoccupied_cols_per_row[i][j] = 0;
            }
        }

        // INIT ANIM #... LOAD 2 ALIEN IMAGE POINTERS FOR ALL ALIENS, ONE FOR EACH ANIMATION
        alien_anim = Player[0].alien_anim;
        for (i = 0; i < 2; i++)
        {
            for (j = 0; j < 55; j++)
            {
                if (i == 0)
                {
                    if (j < 22)
                        Alien_img_ptr[i][j] = INVR_IMG_MAGENTAIE_0;
                    else if (j < 44)
                        Alien_img_ptr[i][j] = INVR_IMG_BLUIE_0;
                    else
                        Alien_img_ptr[i][j] = INVR_IMG_GREENIE_0;
                }
                else
                {
                    if (j < 22)
                        Alien_img_ptr[i][j] = INVR_IMG_MAGENTAIE_1;
                    else if (j < 44)
                        Alien_img_ptr[i][j] = INVR_IMG_BLUIE_1;
                    else
                        Alien_img_ptr[i][j] = INVR_IMG_GREENIE_1;
                }
            }
        }

        // GAME VARS
        round_is_over = false;
        another_player = false;
        current_time = 0, heartbeat_sfx_timer = 0;
        // Use 'exists' flag to indicate whether or not each player is alive vs dead
        Player[0].exists = 1;
        Player[0].lives = 3; // range: 0-4 more, #1-#3 are active gunner + 2 more, #4 is a bonus life added after a certain score
        Player[1].exists = Game.num_players;
        Player[1].lives = Game.num_players * 3;
        if (Game.num_players == 1)
            another_player = true;

        clear_char_screen(7, 22); // clear the game title and scoring table (play area) of the screen
        for (i = 0; i < 4; i++)
        { // and clear sprites from SCORING TABLE
            ptr = SPR_CFG_BASE + ((5 + i) * sizeof(vga_mode4_sprite_t));
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
        }

        if (Game.num_players == 1)
        {
            print_string(6, 14, "PLAYER <1>", !slow);
        }
        else
        {
            print_string(6, 14, "  READY?  ", !slow);
        }
        temp1 = new_delay(120);
        if (temp1 == 1)
        {
            demo_terminated = true;
            Game.num_players = 0;
            Game.play_mode = true;
            continue;
        }
        else if (temp1 == 2)
        {
            demo_terminated = true;
            Game.num_players = 1;
            Game.play_mode = true;
            continue;
        }
        temp1 = 0;
        print_string(6, 14, "          ", !slow);

        // SPRITE VARS
        ptr = SPR_CFG_BASE;
        sprite_number = 0;

        // Restore all bunker images to pristine state by copying template bunker to 8 bunker image buffers, 4 for each player
        // ... do this at boot up and when a new level is started for a given player
        restore_bunkers(0); // plyr 1
        restore_bunkers(1); // plyr 2

        // ALIEN ARRAY VARS
        // calc and store RELATIVE POSITION CONSTANTS for each ALIEN MATRIX POSITION (relative to matrix reference)
        // alien_ref_x pos changes every tick, Alien_rel are fixed and relative to reference
        for (i = 0; i < 5; i++)
        {
            for (j = 0; j < 11; j++)
            {
                Alien_rel_x[(i * 11) + j] = j * 16;
                Alien_rel_y[(i * 11) + j] = i * -16;
            }
        }

        // CREATE ARRAY of CONSTANT Bounding Box (BBOX) VALUES and INITIALIZE "UPDATE" FLAG to TRUE
        for (i = 0; i < 55; i++)
        {
            Alien_update[i] = true;
            Alien_bbox_y0[i] = 4;
            Alien_bbox_y1[i] = 12;
            if (i < 22)
            {
                Alien_bbox_x0[i] = INVR_MAG_BBOX_X0; // for CD, remainder of (Bullet_x - Alien_ref_x)/16 s/b between x0 and x1, +/-2 based on update flag
                Alien_bbox_x1[i] = INVR_MAG_BBOX_X1;
                Alien_width[i] = 12;
            }
            else if (i < 44)
            {
                Alien_bbox_x0[i] = INVR_BLU_BBOX_X0;
                Alien_bbox_x1[i] = INVR_BLU_BBOX_X1;
                Alien_width[i] = 11;
            }
            else
            {
                Alien_bbox_x0[i] = INVR_GRN_BBOX_X0;
                Alien_bbox_x1[i] = INVR_GRN_BBOX_X1;
                Alien_width[i] = 8;
            }
        }

        // OTHER ALIEN VARS
        alien_x = INVR_MTRX_START_X;
        alien_y = INVR_MTRX_B_START_Y;
        alien_hit = 0;
        alien_x_hit = 0;
        alien_y_hit = 0;
        alien_explosion_sfx_enable = false;
        alien_march_sfx_enable = true;
        alien_march_sfx_timer = 100; // prevents premature start of SFX when timer = 0
        alien_explosion_done = false;
        alien_ref_x = INVR_MTRX_START_X;
        alien_ref_update = true;
        alien_ref_y = INVR_MTRX_B_START_Y; // initial value changes with each increase in level for several (8?) cycles then repeats
        alien_y_incr = 0;
        alien_1st_col_abs_x = INVR_MTRX_START_X + 0;
        alien_row_num = 5;
        alien_col_num = 11;
        num_of_alien_hit = 0;
        alien_last_col_abs_x = INVR_MTRX_START_X + 176;
        alien_col_bomb_0 = 11; // col from which bomb 0 is dropped, default is no columne ( = 11)
        alien_drop = 0;
        alien_landed = false;
        alien_roll_over = 0;
        skip_alien_sprite_update = true;

        // GUNNER VARS Init
        Gunner.exists = false; // gunner or it's explosion animation are in progress, 0 = fully terminated
        Gunner.hit = false;    // has been hit and appropriate explosion is in progress
        Gunner.explos_ticks = 0;
        Gunner.blown_up = false; // at start of new level, Gunner isn't alive/hasn't been spawned yet
        Gunner.sfx = false;
        Gunner.spawn = 0;
        Gunner.x = LIVES_P1_X_BASE;
        Gunner.direction_right = false; // game mode direction from keybd
        Gunner.direction_left = false;
        Gunner.loaded = 1; // 1 = gun is armed/loaded
        Gunner.shoot = false;
        gunner_demo_direction_right = false;

        // Bullet VAR init
        Bullet.exists = false;
        Bullet.hit = 0;
        Bullet.explos_ticks = BULLET_EXPL_TICKS;

        // Bullet.sfx = false;
        Bullet.toggle_fx = 0;
        Bullet.reload = 0;
        Bullet.spawn_enable = true;
        Bullet.anim_sequ_num = 0; // image # 0 = bullet image, 1 = explosion
        Bullet.x = Gunner.x + 4;
        Bullet.y = GUNNER_Y_BASE + 2;
        Bullet.x_base = GUNNER_P1_X_BASE + 4; // starting x,y position (
        Bullet.x_path = 0;                    // retains x position once bullet leaves the barrel
        Bullet.y_base = GUNNER_Y_BASE + 2;

        // BOMB INITIALIZAITON
        for (i = 0; i < NUM_BOMB_SPR; i++)
        { // i cycles thru bomb types, 0=screw/targeted, 1=spike, 2=sawtooth
            Bomb[i].exists = false;
            // HIT: 0=no collision, 1=alien, 2=bullet, 3=macro_bunkr, 4=micro_bunkr, 5=gunner, 6=ground
            Bomb[i].hit = 0;
            Bomb[i].bunkr_macro_hit = 0;
            Bomb[i].bunkr_mem_row_addr = 0; // bunker erosion row/col image memory address
            Bomb[i].bunkr_mem_col_addr = 0;
            Bomb[i].explos_started = false; // not started
            Bomb[i].numbr_steps_taken = 0;  // number of y-axis steps taken since spawning occurred
            Bomb[i].x = 0;                  // current pos
            Bomb[i].y = DISAPPEAR_Y;        // all bombs off screen
            Bomb[i].anim_sequ_num = 0;      // Index into animation images 0 - 3 for each bomb, 4 for explosion
            Bomb[i].x0 = BOMB_BBOX_X0;      // add x,y to these to get actual bbox
            Bomb[i].x1 = BOMB_BBOX_X1;
            Bomb[i].y1 = BOMB_BBOX_Y1;
        }
        Bomb[2].anim_sequ_num = 3;
        Bomb[0].y0 = BOMB_BBOX_SCREW_Y0; // add x,y to these to get actual bbox
        Bomb[1].y0 = BOMB_BBOX_SPIKE_Y0;
        Bomb[2].y0 = BOMB_BBOX_SAWTOOTH_Y0;

        // BOMB VARS
        // starting indices for next bomb drop 'column table'
        bomb_type_counter = 2, bomb_type_selector = 2;
        bomb_speed = 4;        // initial speed, changes to 5 when # aliens is less than 9
        bomb_reload_rate = 48; // yeah, I know it sounds wrong, but this is correct
        bomb_reload_index = 0; // index into bomb drop rate table, index is based on MSD of player's score and ranges from 0x30 down to 0x07,
        bomb_screw_skip = 0;   // skip one spawning turn if screw bomb was just terminated
        bomb_img_start_addr = 0, bomb_img_addr_offset1 = 0, bomb_img_addr_offset2 = 0;

        // SAUCER VARS
        Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME; // # of ticks b4 next spawn, saucers spawn every 25.6 s from last saucer termination
        Saucer.spawn_enable = false;
        Saucer.score_start_time = 0;
        Saucer.explosion_x = DISAPPEAR_X;
        Saucer.sfx = false;
        Saucer.toggle_fx = 0;
        saucer_expl_score_image_ptr = SAUCER_MAGENTA_EXPLOS_IMG_BASE;
        Saucer.left = 1;       // default start from right & move left
        Saucer.exists = false; // not until spawn occurs during game play

        // COLLISION VARS
        bullet_hit = 0, bullet_hit_subset1 = 0, bullet_hit_subset2 = 0;
        bullet_macro_bunkr_hit = 0, bullet_micro_bunkr_hit = 0;
        bullet_bomb_hit = 0;
        bullet_alien_hit = 0;
        bullet_saucer_hit = 0;
        bullet_boundary_hit = 0;

        // Bomb with Bullet and Bullet with Bomb Collisions
        ShotOverlapTop = 0;
        ShotOverlapBottom = 0, ShotColumn = 0;
        BombTopRow = 0, BombRows = 0;
        bomb_image_mem = BOMB_IMG_BASE;

        // bomb with bunker collisions
        bomb_micro_bunkr_hit = false;
        BunkrBombOverlapLeft = 0, BunkrBombOverlapRight = 0, BunkrBombOverlapTop = 0, BunkrBombOverlapBottom = 0;
        BunkrExplosionOverlapLeft = 0, BunkrExplosionOverlapRight = 0, BunkrExplosionOverlapTop = 0, BunkrExplosionOverlapBottom = 0;
        temp_rw0 = 0, temp_rw1 = 0;

        // bunker
        bunkr_num = 4; // default value used to indicate bunker # has not been identified

        // #########################################
        // #######  SoundFX initialization   #######
        // #########################################
        xreg(0, 1, 0x00, 0xFFFF); // turn off PSG
        delay(30);
        xreg(0, 1, 0x00, SFX_BASE_ADDR); // initialize PSG... set base address of xregs

        // Sound FX VARS
        dummy_read = 0;
        toggle_tones = 1; // put at SFX var initialization, may need unique one for each channel
        loops = 0;        // reinit in CONTROL LOOP or at bullet termiantion
        bullet_loops = 0;
        ramp_up = true;

        // turn off all 8 channels, gate = 0
        RIA.addr0 = SFX_BASE_ADDR + PAN_GATE;
        RIA.step0 = 8;
        for (i = 0; i < 8; i++)
            RIA.rw0 = 0x00;

        // Saucer Init ch 2
        wave = 3;                 // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
        frequency = 2235;         // Hz * 3 (745 Hz)
        duty = 128;               // % = duty/256
        attack_volume_atten = 13; // max = 15 for each nibble
        attack_time = 4;
        decay_volume_atten = 12;
        decay_time = 8;
        release_time = 8;
        load_SFX_base_parameters(SAUCER_SFX_BASE_ADDR);

        // Bullet Init ch 3
        wave = 4; // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
        bullet_freq = 5000;
        duty = 128;
        attack_volume_atten = 7; // max = 15
        attack_time = 2;
        decay_volume_atten = 11;
        decay_time = 8;
        release_time = 4;
        load_SFX_base_parameters(BULLET_SFX_BASE_ADDR);

        // Alien March Init - ch 4
        alien_march_note_sequ = 0;
        wave = 0;                // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
        frequency = 150;         // Hz * 3
        duty = 160;              // % = duty/256
        attack_volume_atten = 2; // max = 15 for each nibble
        attack_time = 3;
        decay_volume_atten = 6;
        decay_time = 5;
        release_time = 4;
        load_SFX_base_parameters(ALIEN_MARCH_SFX_BASE_ADDR);
        // END SFX INITIALIZATION

        // #########################################
        // ########  SPRITE INITIALIZATION  ########
        // #########################################

        // **** BUNKER SPRITE INIT **** //
        // Sprites 0 through 3, Load sprite config for each base
        for (i = 0; i < NUM_OF_BUNKR_SPR; i++)
        {
            ptr = SPR_CFG_BASE + ((BUNKR_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
            bunkr_x_pos = BUNKR_ZERO_X + (i * BUNKR_X_SPACING);
            bunkr_img_ptr = BUNKR_0_PLYR1_IMG_BUF + (SPR_32X32_SIZE * i);
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, bunkr_x_pos);
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, BUNKR_Y);
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bunkr_img_ptr);
            xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 5);
            xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            sprite_number++;
        }
        bunkr_img_ptr = BUNKR_0_PLYR1_IMG_BUF; // reset to first players images

        // **** SAUCER BIG EXPLOSION SPRITE INIT **** //
        ptr = SPR_CFG_BASE + (SAUCER_EXPLOS_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);   // offscreen
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, SAUCER_BASE_Y); // for now assume value as saucer
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, saucer_expl_score_image_ptr);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 5);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
        sprite_number++;

        // **** INVR SPRITE MATRIX INIT **** //
        // Sprites 5 through 60 -- initialize for starting player (player 1)
        // NOTE using ref y as upper left of matrix for this initialization, everywhere else it's the lower left
        alien_ref_y = INVR_MTRX_T_START_Y;
        alien_ref_x = INVR_MTRX_START_X;
        for (alien_row_num = 5; alien_row_num > 0; alien_row_num--)
        {
            alien_y = alien_ref_y + ((int16_t)INVR_Y_SPACING * ((int16_t)alien_row_num - 1)); // Calculate alien_y position for current row
            for (alien_col_num = 0; alien_col_num < 11; alien_col_num++)
            {
                alien_x = alien_ref_x + (alien_col_num * INVR_X_SPACING); // Calc alien_x position for current column
                // Each loop...load sprite config for one of the 55 aliens
                ptr = SPR_CFG_BASE + (sprite_number++ * sizeof(vga_mode4_sprite_t));
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, alien_y);
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, Alien_img_ptr[alien_anim][alien_num]);
                xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
                xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
                alien_num++;
            }
            alien_col_num = 0; // Reset base column # and alien_x base position
        }
        alien_num = 54;
        alien_x = alien_ref_x = INVR_MTRX_START_X;
        // reset to ref y to bottom of matrix (was top in sprite init, above)
        alien_y = alien_ref_y = Mtrx_Y_Start[Player[active_player].index_start_y_pos++];
        alien_row_num = 5;
        alien_col_num = 11;

        // **** SAUCER SPRITE INIT **** //
        // Load sprite config for the first saucer
        ptr = SPR_CFG_BASE + (sprite_number++ * sizeof(vga_mode4_sprite_t));
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, SAUCER_BASE_Y);
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, SAUCER_IMG_BASE);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);

        // **** GUNNER SPRITE INIT **** //
        // off screen until spawn
        Gunner.y = DISAPPEAR_Y;
        gunner_image_ptr = GUNNER_PLYR1_IMG_BASE;
        ptr = SPR_CFG_BASE + ((sprite_number++) * (sizeof(vga_mode4_sprite_t)));
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Gunner.x);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Gunner.y);
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, gunner_image_ptr);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);

        // #####  LIVES SPRITE INIT  #####
        for (i = 0; i < 2 + 1; i++)
        {
            for (j = 0; j < 4; j++)
            {
                lives_y_pos = DISAPPEAR_Y;
                if (j == 3)
                {
                    lives_spr_ptr = LIVES_BONUS_IMG_BASE;
                    lives_x_pos = LIVES_P1_X_BASE + (LIVES_X_SPACING * j);
                }
                else if (i == 0)
                {
                    lives_spr_ptr = GUNNER_PLYR1_IMG_BASE;
                    lives_x_pos = LIVES_P1_X_BASE + (LIVES_X_SPACING * j);
                }
                else
                {
                    lives_spr_ptr = GUNNER_PLYR2_IMG_BASE;
                    lives_x_pos = LIVES_P2_X_BASE + (LIVES_X_SPACING * j);
                }
                ptr = SPR_CFG_BASE + ((sprite_number++) * (sizeof(vga_mode4_sprite_t)));
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, lives_x_pos);
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, lives_y_pos);
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, lives_spr_ptr);
                xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
                xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            }
        }

        // **** BULLET SPRITE INIT **** //
        // ###############################
        bullet_y_base = GUNNER_Y_BASE + 2; // positions gunner near bottom
        bullet_x_path = Gunner.x + 4;      // bullet starting point should always be top-center of gunner sprite
        bullet_x = bullet_x_path;          // Once you fire a bullet it's x-axis postion should remain unchanged
        bullet_y = bullet_y_base;          // Needed for initialization and new bullets
        bullet_y = DISAPPEAR_Y;
        bullet_image_ptr = BULLET_IMG_BASE;
        ptr = SPR_CFG_BASE + (BULLET_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, bullet_x);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, bullet_y);
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bullet_image_ptr);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 3);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
        sprite_number++;

        // **** BOMB SPRITES INIT **** //
        for (i = 0; i < 3; i++)
        {
            ptr = SPR_CFG_BASE + ((BOMB_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
            switch (i)
            {
            case 0:
                bomb_image_ptr = BOMB_SCREW_IMG_BASE + (Bomb[i].anim_sequ_num * SPR_8X8_SIZE);
                break;
            case 1:
                bomb_image_ptr = BOMB_SPIKE_IMG_BASE + (Bomb[i].anim_sequ_num * SPR_8X8_SIZE);
                break;
            case 2:
                bomb_image_ptr = BOMB_SAWTOOTH_IMG_BASE + (Bomb[i].anim_sequ_num * SPR_8X8_SIZE);
                break;
            }
            // bomb_image_ptr = BOMB_IMG_BASE;
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Bomb[i].x);
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Bomb[i].y);
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bomb_image_ptr);
            xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 3);
            xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            sprite_number++;
        }

        // Time to first Gunner Spawn and to Bomb Drop Enabled  when "new level"
        Gunner.spawn_time = 64;
        Game.bomb_spawn_enable = false; // will be set to true when timer expires
        Game.bomb_spawn_time = 120;
        Game.new_round = false;

        update_numerical_lives(!blink, Player[active_player].lives);
        print_string(29, 15, "WAVE", !slow);
        Player[active_player].level = 1;
        update_wave_number(false); // false = print at bottom of screen

        // TEST vars (unneeded)
        r = 0;
        s = 0;
        bypass_test_mode = false;
        loop_count_A = 0;
        temp1 = temp2 = 0;

        // ########################################################################
        // #########################    CONTROL LOOP    ###########################
        // ########################################################################

        // CONTROL LOOP START - This where the game logic resides
        while (1)
        {
            // #### PROCESS RESULTS FROM LAST ROUND, UNLESS IT'S THE FIRST ROUND, IN WHICH CASE...SKIP ALL THIS
            if (round_is_over)
            { // either gunner was hit, alien landed or level was completed
                rounds_completed++;
                // turn off all 8 SFX channels
                RIA.addr0 = SFX_BASE_ADDR + PAN_GATE;
                RIA.step0 = 8;
                for (i = 0; i < 8; i++)
                    RIA.rw0 = 0x00;
                // reset PSG
                xreg(0, 1, 0x00, 0xFFFF); // turn off PSG
                delay(30);
                xreg(0, 1, 0x00, SFX_BASE_ADDR); // initialize PSG... set base address of xregs

                // check for another player
                another_player = (Game.num_players == 1) && (Player[1 - active_player].exists == 1);
                // ####  'GUNNER WAS HIT' HANDLER  ####
                // ####################################
                if (Gunner.blown_up)
                { // s/b true when gunner is hit or alien has lannded
                    // ##### CURRENT PLYR GAME OVER ####
                    if (Player[active_player].lives == 0)
                    {
                        Player[active_player].exists = 0;
                        Player[active_player].game_over = true;
                        if (Game.num_players == 1)
                        { // TWO PLAYER GAME AND OTHER PLAYER EXISTS
                            print_string(27, 9, "PLAYER <", !slow);
                            if (active_player == 0)
                                print_string(27, 17, "1> GAME OVER", !slow);
                            else
                                print_string(27, 17, "2> GAME OVER", !slow);
                            // new_delay (180);
                            temp1 = new_delay(180);
                            print_string(27, 8, "                      ", !slow);
                            if (temp1 == 1)
                            {
                                demo_terminated = true;
                                Game.num_players = 0;
                                Game.play_mode = true;
                                break;
                            }
                            else if (temp1 == 2)
                            {
                                demo_terminated = true;
                                Game.num_players = 1;
                                Game.play_mode = true;
                                break;
                            }
                            temp1 = 0;
                            print_string(27, 9, "                          ", !slow);
                        }
                        // ##### FULL GAME OVER #####
                        // execute GAME OVER procedure (both players)
                        if (!another_player)
                        {
                            clear_char_screen(5, 22);
                            print_string(27, 6, "                        ", !slow);
                            ptr = SPR_CFG_BASE + (SAUCER_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, SAUCER_BASE_X);
                            ptr = SPR_CFG_BASE + (SAUCER_EXPLOS_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, SAUCER_BASE_X);
                            print_string(5, 15, "GAME OVER", slow);
                            temp1 = new_delay(180);
                            print_string(5, 14, "          ", !slow);
                            if (temp1 == 1)
                            {
                                demo_terminated = true;
                                Game.num_players = 0;
                                Game.play_mode = true;
                            }
                            else if (temp1 == 2)
                            {
                                demo_terminated = true;
                                Game.num_players = 1;
                                Game.play_mode = true;
                            }
                            temp1 = 0;
                            // update hiscore file
                            fptr = fopen("raiders.hiscore", "wb+");
                            fwrite(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
                            fclose(fptr);
                            break;
                        }
                    }
                    // ##### CURRENT PLYR ALIVE AND WELL #####
                    else if (another_player)
                    {
                        // ##### THE OTHER PLAYER IS TOO #####
                        // so, save the active player's VARS that aren't in the Player[] STRUCT, then swap players below
                        Player[active_player].alien_ref_x = alien_ref_x;
                        Player[active_player].alien_ref_y = alien_ref_y;
                        Player[active_player].alien_anim = alien_anim;
                    }
                    // Else
                    // ##### OTHERWISE OTHER PLAYER IS DEAD OR NEVER EXISTED... SO, RESUME WITH CURRENT PLAYER
                }

                // ####  START NEW LEVEL HANDLER  ####
                // ###################################
                // update lives # to include bonus if it's there
                // draw std lives icons based on # of lives and drwan bonus icon in next open postiion based on bonus flag = true
                // save new state at new level
                // celebrate completion -- clear play area, print level/wave X for active player, pause
                // clear the game play area
                // REAPPEAR SPRITES in prep for start of next level
                // restore player's bunkers, update screen with new # of lives and lives sprites, draw aliens,
                // blink new player's score start next round, set timers for spawning gunner, bombs and saucer
                else if (level_completed)
                {
                    level_completed = false;
                    // CLEAR playing field (text and sprites)
                    clear_char_screen(5, 22);
                    for (i = 0; i < TOTAL_NUM_SPR; i++)
                    {
                        ptr = SPR_CFG_BASE + (i * sizeof(vga_mode4_sprite_t));
                        if (i == 4 || i == 60)
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
                        }
                        else
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                        }
                    }

                    // CELEBRATE Level Completion!!
                    print_string(6, 15, "PLAYER <", !slow);
                    if (active_player == 0)
                        print_string(6, 23, "1>", !slow);
                    else
                        print_string(6, 23, "2>", !slow);
                    print_string(9, 10, "WAVE     COMPLETED!", !slow);
                    update_wave_number(true); // true = print at top of screen, false bottom
                    update_wave_number(false);
                    delay(240);
                    print_string(6, 5, "                              ", !slow);
                    print_string(9, 5, "                              ", !slow);

                    // RELOAD PRISTINE BUNKER IMAGES
                    // restore aliens and bunkers to pristine state, but do not display... yet, use
                    // PLAY LOOP sprite update to load updated sprites to screen
                    restore_bunkers(active_player);

                    // NEW LEVEL SO RESTORE ALL ALIENS TO HEALTH
                    Player[active_player].num_of_aliens = 55;
                    for (i = 0; i < 55; i++)
                    {
                        Players_Alien_Exists[active_player][i] = 1;
                    }
                    alien_anim = 1;
                    // update key state save VARS and reinitialize to start the next level/wave
                    // increment index into 'starting y pos' table, roll-over after 8
                    if (Player[active_player].index_start_y_pos > 8)
                        Player[active_player].index_start_y_pos = 1;
                    Player[active_player].alien_ref_y = Mtrx_Y_Start[Player[active_player].index_start_y_pos];
                    alien_ref_y = Mtrx_Y_Start[Player[active_player].index_start_y_pos];
                    Player[active_player].index_start_y_pos++;
                    Player[active_player].level++;
                    Player[active_player].alien_ref_x = INVR_MTRX_START_X;
                    alien_ref_x = INVR_MTRX_START_X;
                    Player[active_player].alien_x_incr = 2; // right = +2, left equal -2
                    Player[active_player].alien_1st_col = 0;
                    Player[active_player].alien_1st_col_rel_x = 0;
                    Player[active_player].alien_last_col = 10;
                    Player[active_player].alien_last_col_rel_x = 176;

                    // Reset count of unoccupied alien columns and rows to zero
                    // When an alien is terminated the bullet handler will add 1 to the # of unoccupied cols, when = 5, then col is empty
                    for (j = 0; j < 11; j++)
                        Alien_unoccupied_rows_per_col[active_player][j] = 0;
                    for (j = 0; j < 5; j++)
                        Alien_unoccupied_cols_per_row[active_player][j] = 0;
                    alien_num = 54;
                    alien_x = alien_ref_x;
                    alien_y = alien_ref_y;
                    Player[active_player].bullets_fired = 0;
                    skip_alien_sprite_update = true;
                    // SFX reinit
                    Player[active_player].alien_march_index = 0;
                } // END NEW LEVEL HANDLER

                // #####  ANOTHER PLAYER??  #####
                // #############################
                // if another player exists then always swap players before reentering PLAY LOOP
                if (another_player)
                {
                    // #####  SWAP PLAYERS  #####
                    active_player = 1 - active_player;
                    // CLEAR PLAY FIELD TEXT & SPRITES
                    clear_char_screen(5, 22);
                    for (i = 0; i < TOTAL_NUM_SPR; i++)
                    {
                        ptr = SPR_CFG_BASE + (i * sizeof(vga_mode4_sprite_t));
                        if (i == 4 || i == 60)
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
                        }
                        else
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                        }
                    }
                    // show new player ID at top of screen
                    print_string(6, 14, "PLAYER <", slow);
                    if (active_player == 0)
                        print_string(6, 22, "1>", slow);
                    else
                        print_string(6, 22, "2>", slow);
                    update_wave_number(false);
                    // update lives ICONs to represent # lives of new player
                    for (i = 0; i < 4; i++)
                    {
                        ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + (i + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
                        if (i < Player[active_player].lives)
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, LIVES_Y_BASE);
                        }
                        else
                        {
                            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                        }
                    }
                    update_numerical_lives(blink, Player[active_player].lives);
                    // erase the text displayed above
                    print_string(6, 9, "                          ", !slow);

                    // change BUNKER SPRITE IMAGE pointers to OTHER PLAYER'S bunker images
                    bunkr_img_ptr = ((1 - active_player) * BUNKR_0_PLYR1_IMG_BUF) + (active_player * BUNKR_0_PLYR2_IMG_BUF);
                    for (i = 0; i < 4; i++)
                    { // switch to the other bank of 4 bunker images
                        ptr = SPR_CFG_BASE + ((BUNKR_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
                        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bunkr_img_ptr);
                        bunkr_img_ptr += SPR_32X32_SIZE;
                    }

                    // update working copy of ref positions using new player's data
                    // change ALIEN SPRITE X/Y POSITIONS to OTHER PLAYER"S prior positions
                    alien_ref_x = Player[active_player].alien_ref_x;
                    alien_ref_y = Player[active_player].alien_ref_y;
                    // ditto for state of animation
                    alien_anim = Player[active_player].alien_anim;

                    // reinit ALIEN NUM index for ALIEN MATRIX, reinit ALIEN# 0 X/Y POS = REFERENCE VALUES
                    alien_num = 54;
                    alien_x = alien_ref_x;
                    alien_y = alien_ref_y;
                    skip_alien_sprite_update = true;
                } // ELSE there is NO OTHER PLAYER, so keep current player

                // reappear fresh set of bunker sprites
                for (i = 0; i < NUM_OF_BUNKR_SPR; i++)
                {
                    ptr = SPR_CFG_BASE + ((BUNKR_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, BUNKR_Y);
                }
            } // END OF "ROUND OVER" HANDLER FOR GUNNER HIT and LEVEL COMPLETED "round is over"

            // ###############################################################
            // #######  STUFF TO INIT/RESET/REINIT PRIOR TO PLAY LOOP  #######
            // ###############################################################

            // LIVES UPDATES
            // in the CONTROL LOOP player's maybe swapped, so need to fully reinit the LIVES ICONS before starting PLAY LOOP
            // use # of lives remaining to turn on the right # of Lives ICONS and turn off the others
            //      put Lives ICON handler here and always initialize/reinitialize the Lives Array based on # of lives remaining, with the goal of
            // .... starting each round (pre-gunner-sapwn) with all lives shown as Lives ICONS lined up across the bottom, during SPAWN the last
            // .... ICON will be removed and a gunner ICON will be spawned in its "play" position above the Lives ICONS
            // .... DO the actual ICON sprite updates, appear/disapper (x-pos) and image (color) changes in the sprite update, buy when the update
            // .... flag is set
            // Lives Array - 1st index is player#, 2nd is 1 = existence, valid patterns 000, 100, 110, 111
            // .... start with 1st 3 ICONS = exists, update as appropriate using # of lives remaining to update existence flags

            for (i = 0; i < 4; i++)
            {
                ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + (i + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
                if (i < Player[active_player].lives)
                {
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, LIVES_Y_BASE);
                }
                else
                {
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                }
            }
            if (Player[active_player].bonus_active)
            { // Handle BONUS case
                ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + ((Player[active_player].lives - 1) + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, LIVES_BONUS_IMG_BASE);
            }

            // update # waves at bottom of screen
            update_wave_number(false); // false = print at bottom of screen

            // (Re)initialize KEY VARS
            round_is_over = false;
            Gunner.blown_up = false;
            inflight_complete = false;
            alien_landed = false;
            Gunner.exists = false;
            Gunner.spawn_time = 64;
            Game.bomb_spawn_enable = false;
            Game.bomb_spawn_time = 120;
            Saucer.exists = false;
            Saucer.spawn_enable = false;
            Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME;
            alien_roll_over = 0;
            alien_hit = 0;
            Gunner.hit = false;

            // SFX VARS
            dummy_read = 0;
            toggle_tones = 1; // put at SFX var initialization, may need unique one for each channel
            loops = 0;
            bullet_loops = 0;
            ramp_up = true;
            alien_march_sfx_enable = true;
            alien_march_sfx_timer = Alien_March_SFX_Rate[Player[active_player].alien_march_index];
            alien_march_sfx_timer = 100;
            alien_march_sfx_start = 0;

            // #######  GETTING READY TO TRANSITION TO PLAY LOOP  #######
            // grab current value of vsync, wait for VSYNC to increment to v + 1, sync play loop to that event
            v = RIA.vsync;
            // END CONTROL LOOP

            // SETUP 6522 VIA Timer for performance measurement
            // ######  VIA TIMER/INTERRUPT  ######
            // ###################################
            // 6522 VIA interface initialization
            VIA_irq_count = 0;
            // disable VIA timer 1 interrupts by writing a 1 to bit 6 & a 0 to bit 7 of Interrupt Enable Register
            // pointer_LSB = (VIA_pointer + 0x0E);
            // *pointer_LSB = 0x3F;   // This disables all interrupts, bit 7 means clear the bits selected, bits 0-6 when 1 it means reset/disable
            *via_ier = 0x3F;
            // setup VIA timer 1 to rollover continuously using Aux Control Reg
            // pointer_LSB = (VIA_pointer + 0x0B);
            // *pointer_LSB = 0xC0;
            *via_acr = 0xC0;
            // load counters in VIA timer 1 with 0xFFFF, equivalent to 8.192 ms at PHI2 = 8 MHz (125ns)
            *via_count_l = 0xFF;
            *via_count_h = 0xFF;

            // Synchronize start of IRQ HANDLER with next VSYNC transition
            v = RIA.vsync;
            while (v == RIA.vsync)
                continue;
            v = RIA.vsync;
            initial_vsync = v;
            // initialize IRQB interrupt handler
            set_irq(irq_fn, &irq_stack, sizeof(irq_stack));
            // Enable VIA interupt requests for Timer 1
            // Interrupt Enable Register (0x0E), bit 6 is Timer 1, write 1 to bit 6 and to bit 7 to enable interrupts
            // ... write 0 to bit 7 and 1 to bit 6 to disable interupts
            // pointer_LSB = (VIA_pointer + 0x0E);
            // *pointer_LSB = 0xC0;  // set timer 1 interrupt to enabled, others unchanged
            // (volatile unsigned char *)0xff0e = 0xC0;
            *via_ier = 0xC0;

            // Arrays to hold timing results for performance measurement
            for (i = 0; i < 50; i++)
                max_duration_values[i] = 0;
            for (i = 0; i < 50; i += 2)
            {
                values[i] = 0;
                values[i + 1] = 255;
            }

            // ###################################################################################
            // #########################    PRIMARY LOOP - PLAY Loop    ##########################
            // ###################################################################################

            while (1)
            {
                // Execute PLAY loop until break command
                // ... which is issued when the level is completed or gunner has been  hit
                // ##########   WAIT FOR VSYNC/START OF BLANKING   #########
                // Synchronzie graphics updates and PLAY loop to start of video blanking period
                if (RIA.vsync == v)
                    continue;  // wait for blanking period (VSYNC increment)
                v = RIA.vsync; // update v to be ready for start of next 'play' vsync loop

                current_time++;
                // ##########   BOMB SELECTOR/SYNCHRONIZER   #########
                // counter/selector determine the counter rotating sequence of bomb drop opportunities
                bomb_type_selector = bomb_type_counter;

                // PERF
                // reset index of array that holds time stamps
                values_position = 0;
                // Reset timer/IRQ counter to "zero", note timer is a count down timer, IRQ counter OTH counts up
                VIA_irq_count = 0;
                *via_count_l = 0xFF;
                *via_count_h = 0xFF;
                // log starting time for loop
                // values[values_position++] = VIA_irq_count;  // Pos ZERO BEGINNING OF THE LOOP
                // values[values_position++] = *via_count_h;

                // ###########################################################################################
                // ##########################    UPDATE SPRITE CONFIG DATA    ################################
                // ###########################################################################################

                // ALIEN SPRITE UPDATE
                // *******************
                // IMAGE POINTER HANDLER - alternates between EXPLOSION and ALIEN IMAGE
                // image pointer needs to point to the alien that has been hit
                if (skip_alien_sprite_update == false)
                {
                    ptr = SPR_CFG_BASE + ((num_of_alien_hit + INVR_FIRST_SPR_NUM) * sizeof(vga_mode4_sprite_t));
                    // if collision has occurred and explosion is done, disappear alien
                    if (alien_explosion_done)
                    {                                                                                                            // indicates termination process has been completed/is done
                        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);                                        // disappear dead alien
                        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, Alien_img_ptr[alien_anim][num_of_alien_hit]); // restore alien image for next round
                        alien_explosion_done = false;                                                                            // reset flag for next time
                    }
                    // if hit, do explosion image
                    if (alien_hit == 1)
                    {
                        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, INVR_EXPL_IMG_BASE);
                    }
                    // otherwise, just do regular (once per tick) update of sprite pos
                    else
                    {
                        ptr = SPR_CFG_BASE + ((alien_num + INVR_FIRST_SPR_NUM) * sizeof(vga_mode4_sprite_t));
                        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, alien_x);
                        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, alien_y);
                        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, Alien_img_ptr[alien_anim][alien_num]);
                    } // END ALIEN SPRITE UPDATE
                }
                skip_alien_sprite_update = false;

                // BULLET SPRITE UPDATE - presence, pos
                // ********************
                ptr = SPR_CFG_BASE + (BULLET_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, bullet_x);                // tracks gunner x until fired, then fixed
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, bullet_y);                // fixed y until fired
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bullet_image_ptr); // bullet or explosion

                // BOMB SPRITE UPDATE - presence, pos, animation image for BOMB
                // ******************
                // first check for explosion that have been completed, if so, disappear the bomb/explosion
                for (i = 0; i < 3; i++)
                {
                    if (Bomb[i].y == DISAPPEAR_Y)
                    {
                        ptr = SPR_CFG_BASE + ((BOMB_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
                        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Bomb[i].y);
                        Bomb[i].y = 242; // to run this once per explosion
                    }
                }
                // if explosion is in progress, no position updates, image = explosion, otherwise new position and next animation image
                ptr = SPR_CFG_BASE + ((BOMB_FIRST_SPR_NUM + bomb_num_just_updated) * sizeof(vga_mode4_sprite_t));
                // only update explos image right after explosion starts
                if (bomb_num_just_updated < 3)
                {
                    if (Bomb[bomb_num_just_updated].hit > 0)
                    {
                        bomb_image_ptr = BOMB_EXPL_IMG_BASE;
                    }
                    else
                    {
                        bomb_image_ptr = BOMB_IMG_BASE + (((bomb_num_just_updated * 4) + Bomb[bomb_num_just_updated].anim_sequ_num) * SPR_8X8_SIZE);
                    }
                    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Bomb[bomb_num_just_updated].x);
                    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Bomb[bomb_num_just_updated].y);
                    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bomb_image_ptr);
                }

                // GUNNER SPRITE UPDATE - pos, color (player 1 or 2) or explosion animation
                // ********************
                ptr = SPR_CFG_BASE + (GUNNER_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Gunner.x);
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Gunner.y);
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, gunner_image_ptr);

                // SAUCER (UNHARMED) SPRITE UPDATE - pos, presence,
                // *******************************
                ptr = SPR_CFG_BASE + (SAUCER_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                if (Saucer.exists == true)
                {
                    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Saucer.x);
                }
                else
                { // disappear Saucer
                    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
                }
                // SAUCER EXPLOSION/SCORE SPRITE UPDATE - pos, presence
                // ******************************
                ptr = SPR_CFG_BASE + (SAUCER_EXPLOS_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
                // displays explosion with offset from SAUCER_X, switches from explos to score at 1/2 way point
                xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Saucer.explosion_x);
                xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, saucer_expl_score_image_ptr);

                // ########  ALIEN MARCH SFX HANDLER  ########
                // do this in each loop (doesn't matter where, but I think after SPRITE updates feels right)
                if (alien_march_sfx_start == 2)
                {
                    alien_march_sfx_timer = 0;
                    alien_march_sfx_start = 3;
                }
                if (alien_march_sfx_timer > 0)
                    alien_march_sfx_timer--;
                // made it thru one pass of all alien sprites (all 55 are now visible)
                else if (alien_march_sfx_enable && true)
                {
                    // this modifies the tones 4 times, one on each VSYNC tick
                    switch (alien_march_note_sequ)
                    {
                    case 0:
                        frequency = 150;
                        break;
                    case 1:
                        frequency = 171;
                        break;
                    case 2:
                        frequency = 192;
                        break;
                    case 3:
                        frequency = 213;
                        break;
                    }
                    if (alien_march_note_sequ > 3)
                    {
                        RIA.addr0 = ALIEN_MARCH_SFX_BASE_ADDR + PAN_GATE;
                        RIA.rw0 = 0; // push pause
                        toggle_tones = 1 - toggle_tones;
                        alien_march_note_sequ = 0;
                        // reload timer using current index, same value if # of aliens hasn't crossed the next threshold
                        alien_march_sfx_timer = Alien_March_SFX_Rate[Player[active_player].alien_march_index];
                    }
                    else
                    {
                        if (toggle_tones == 1)
                            frequency += 50;
                        alien_march_note_sequ++;
                        // load frequ
                        RIA.addr0 = ALIEN_MARCH_SFX_BASE_ADDR;
                        RIA.step0 = 1;
                        RIA.rw0 = frequency & 0xFF;
                        RIA.rw0 = (frequency >> 8) & 0xFF;
                        // push play, play the next note in the sequence
                        RIA.addr0 = ALIEN_MARCH_SFX_BASE_ADDR + PAN_GATE;
                        RIA.rw0 = 1 & 1;
                        // sequence done, push pause and wait for the trigger to restart sequence
                    }
                }

                // PERF
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // ##############################################################################################
                // ##############  HANDLER TO PROCESS GUNNER TERMINATION or COMPLETION OF A WAVE  ###############
                // ##############################################################################################

                // spawning is disabled when gunner is hit or #aliens = 0
                // when gunner is terminated and ALL inflight stuff has been terminated, exit the PLAY LOOP

                inflight_complete = !Bullet.exists && !Bomb[0].exists && !Bomb[1].exists && !Bomb[2].exists && !alien_hit && !Gunner.hit;
                if (Gunner.blown_up && inflight_complete && !Saucer.exists && (bullet_saucer_hit == 0))
                {
                    // GUNNER IS TERMINATED - exit to CONTROL LOOP
                    round_is_over = true;
                    break; // exit to CONTROL loop
                }
                // when #aliens is zero, wait for inflight bullets/bombs to terminate, then exist PLAY LOOP
                if (level_completed && inflight_complete)
                {
                    // LEVEL HAS BEEN SUCCESSFULLY COMPLETED - exit to CONTROL LOOP
                    Gunner.y = DISAPPEAR_Y; // disappear gunner/explosion
                    round_is_over = true;
                    break;
                }

                // ############################################
                // ############  KEYBOARD INPUT  ##############
                // ############################################
                // get keybd input for shoot, right, left, pause, restart, quit
                // every tick read specific bytes from keyboard data structure @ 0xFF10
                do
                {
                    if (current_time % 1 == 0)
                    {
                        RIA.addr0 = KEYBOARD_INPUT;
                        RIA.step0 = 2;
                        keystates[0] = RIA.rw0;
                        RIA.step0 = 1;
                        keystates[2] = RIA.rw0;
                        RIA.step0 = 2;
                        keystates[3] = RIA.rw0;
                        RIA.step0 = 4;
                        keystates[5] = RIA.rw0;
                        RIA.step0 = 0;
                        keystates[9] = RIA.rw0;
                        // don't knpw why but have to reset address or add delay to make it (reading 10) work
                        RIA.addr0 = KEYBOARD_INPUT + 10;
                        keystates[10] = RIA.rw0;
                    }
                    // amy key pressed? (determined by LSBit of first byte = 0)
                    if (!(keystates[0] & 1))
                    { // which one?
                        // direction and firing are set here, then reset in Gunner MOVE and Bullet SPAWN sections
                        if (!paused && (keystates[9] & 128))
                        { // move ship right
                            Gunner.direction_right = true;
                            Gunner.direction_left = false;
                        }
                        if (!paused && (keystates[10] & 1))
                        { // move ship left
                            Gunner.direction_left = true;
                            Gunner.direction_right = false;
                        }
                        // 3 keys can be used to fire bullets
                        if (!Gunner.shoot)
                        {
                            if (!paused && (keystates[10] & 4) || (keystates[10] & 2) || (keystates[5] & 16))
                            { // shoot
                                Gunner.shoot = true;
                            }
                        } // else Gunner.shoot = false;
                        if (!handled_key)
                        {
                            if ((keystates[2] & 8))
                            { // pause
                                paused = !paused;
                                RIA.addr0 = KEYBOARD_INPUT;
                                RIA.step0 = 0;
                                while (!(RIA.rw0 & 1))
                                {
                                }
                            }
                            else if ((keystates[2] & 32))
                            { // restart game
                                Game.restart = true;
                            }
                            else if ((keystates[5] & 2))
                            { // exit game
                                fptr = fopen("raiders.hiscore", "wb+");
                                fwrite(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
                                fclose(fptr);
                                exit(0);
                            }
                            else if ((keystates[3] & 8))
                            { // do demo mode immediately
                                printf("switch to extended DEMO mode for DEBUG\n");
                                break;
                            }
                            else if ((keystates[3] & 64))
                            { // quit demo, start 1 player game
                                demo_terminated = true;
                                Game.num_players = 0;
                                Game.play_mode = true;
                                Game.restart = true;
                                break;
                            }
                            else if ((keystates[3] & 128))
                            { // quit demo, start 2 player game
                                demo_terminated = true;
                                Game.num_players = 1;
                                Game.play_mode = true;
                                Game.restart = true;
                                break;
                            }
                            handled_key = true;
                            keystates[2] = 0;
                            keystates[5] = 0;
                            keystates[3] = 0;
                        }
                        else
                        { // no keys down
                            handled_key = false;
                        }
                    }
                } while (paused); // hang out here if 'pause" (P) is pressed, until it's pressed again

                if (Game.restart)
                {
                    xreg(0, 1, 0x00, 0xFFFF); // turn off PSG
                    fptr = fopen("raiders.hiscore", "wb+");
                    fwrite(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
                    fclose(fptr);
                    break;
                }

                // PERF - First Position - POST SPRITE UPDATES AND GAME PLAY KEYBD INPUT
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // #############################################
                // ############   BULLET MOVE/SPAWN   ##########
                // #############################################

                // ####  BULLET SPAWN/RELOAD  ####
                if (Bullet.reload == 1 && Gunner.exists && !Gunner.hit)
                { // if so, load bullet into gunner
                    bullet_x_path = Gunner.x + 4;
                    bullet_x = bullet_x_path; // Once bullet is fired, x-axis postion is unchanged, otherwise it track gunner pos
                    bullet_image_ptr = BULLET_IMG_BASE;
                    if ((Gunner.shoot && Game.play_mode) || !Game.play_mode)
                    {
                        Bullet.exists = true;
                        bullet_y_base = GUNNER_Y_BASE + 4; // bullet loaded position
                        bullet_y = bullet_y_base;          // Needed for initialization and new bullets
                        Player[active_player].bullets_fired++;
                        if (Player[active_player].bullets_fired > 14)
                            Player[active_player].bullets_fired = 0;
                        Bullet.reload = 0;
                        // SFX
                        loops = 0;
                        bullet_loops = 0;
                        // Bullet (RE)Init ch 3
                        wave = 4;                // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
                        bullet_freq = 5000;      // Hz * 3
                        duty = 128;              // % = duty/256
                        attack_volume_atten = 5; // max = 15
                        attack_time = 2;
                        decay_volume_atten = 11;
                        decay_time = 8;
                        release_time = 4;
                        load_SFX_base_parameters(BULLET_SFX_BASE_ADDR);
                    }
                } // END BULLET SPAWN

                // ###  MOVE BULLET  ###  - only move if no explosion is in progress and Bullet has been spawned
                if (bullet_hit == 0)
                {
                    if (Bullet.exists)
                    { // inflight and not exploding, so continue to move it until it collides and is terminated
                        bullet_y -= BULLET_SPEED;
                        bullet_x = bullet_x_path;
                        // do shot SFX
                        // play SFX continously until terminated, if alien is hit, play final explosion sound in CD section
                        // sweep frequency for initial firing sound
                        if ((bullet_loops < 5) && true)
                        {
                            bullet_freq = 5000 - (bullet_loops * 200);
                            bullet_loops++;
                            // load frequ
                            RIA.addr0 = BULLET_SFX_BASE_ADDR;
                            RIA.step0 = 1;
                            RIA.rw0 = bullet_freq & 0xFF;
                            RIA.rw0 = (bullet_freq >> 8) & 0xFF;
                            // PUSH PLAY
                            RIA.addr0 = BULLET_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 1 & 0;
                        }
                        else
                        {
                            RIA.addr0 = BULLET_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 1 & 1;
                        }

                        // alternate between frequencies while inflight
                        if ((bullet_loops > 4) && true)
                        {
                            if (bullet_loops == 10)
                                bullet_freq = 2000;
                            if (bullet_loops == 5)
                                bullet_freq = 1000;
                            bullet_loops++;
                            if (bullet_loops == 15)
                                bullet_loops = 5;
                            // load frequ
                            RIA.addr0 = BULLET_SFX_BASE_ADDR;
                            RIA.step0 = 1;
                            RIA.rw0 = bullet_freq & 0xFF;
                            RIA.rw0 = (bullet_freq >> 8) & 0xFF;
                        }
                    }
                    else
                        bullet_loops = 0;
                }
                // END BULLET MOVE/SPAWN

                // ###########################################################
                // ###########     BULLET COLLISION DETECTION     ############
                // ###########################################################

                // PERF
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // actions for this section
                //      # after explosion, reset "exists" flag for impacted alien, bullet, bomb, saucer, etc.
                //      # set [object name]collision flag "hit"
                //      # start timer, change sprite to explosion for one or both of the objects colliding (both = bullet/alien, just gunner, just bunker)
                //      # pause movement
                //      # timer ends - reset collision flag, set image y to offscreen, restart movement

                // For BOMBS, BUNKERS, ALIENS, SAUCERS, TOP BOUNDARY
                //      DETECT & FLAG COLLISIONS, INITIATE EXPLOSIONS, START EXPLOSIION DURATION TIMERS, PERFORM BUNKER EROSION

                // These bbox values are used for all collision detection, but y1 varies depending on
                // ... object bullet is colliding with
                bullet_x0 = bullet_x + 3;
                bullet_x1 = bullet_x + 4;
                bullet_y0 = bullet_y;
                // see below for bullet_y1

                // COLLISION BULLET hits BOMB (not BOMB to BULLET)
                // ###############################################
                if (bullet_hit == 0)
                {
                    for (i = 0; i < 3; i++)
                    { // loop through all bombs
                        if (Bomb[i].exists && (Bomb[i].hit == 0))
                        {
                            if (bullet_x0 >= Bomb[i].x0 && bullet_x0 < Bomb[i].x1)
                            {
                                // This bbox for y1 is used for BULLET/BOMB COLLISION DETECTION and BOMB/BULLET CD ONLY
                                bullet_y1 = bullet_y + 4;
                                ShotOverlapTop = bullet_y1 - Bomb[i].y;
                                ShotOverlapBottom = Bomb[i].y1 - bullet_y0;
                                ShotColumn = 2 + bullet_x0 - Bomb[i].x0;
                                if (ShotOverlapBottom > 0 && ShotOverlapTop > 0)
                                { // then bullet overlaps bomb in y axis
                                    if (ShotOverlapTop < 4)
                                    {
                                        BombTopRow = 0;
                                        BombRows = ShotOverlapTop;
                                    }
                                    else if (ShotOverlapBottom < 4)
                                    {
                                        BombTopRow = 8 - ShotOverlapBottom;
                                        BombRows = ShotOverlapBottom;
                                    }
                                    else
                                    {
                                        BombTopRow = bullet_y0 - Bomb[i].y;
                                        BombRows = 4;
                                    }

                                    bomb_image_mem = BOMB_IMG_BASE + (((i * 4) + Bomb[i].anim_sequ_num) * SPR_8X8_SIZE);
                                    bullet_bomb_hit = bullet_bomb_micro_collision(bomb_image_mem, BombTopRow, ShotColumn, BombRows);
                                }
                                if (bullet_bomb_hit == 1)
                                {
                                    Bullet.explos_ticks = BULLET_EXPL_TICKS;
                                    bullet_image_ptr = BULLET_EXPL_IMG_BASE;
                                }
                            }
                        }
                    }
                } // END BULLET HITS BOMB COLLISION DETECT

                // RESTORE - This bbox y1 is used for all but BOMB COLLISION DETECTION
                bullet_y1 = bullet_y + 1; // bounding box is just one pixel at the tip of the spear

                // PERF
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // COLLISION BULLET to BUNKER
                // ##########################
                // Macro CD y-axis
                if ((bullet_hit == 0) && !bypass_test_mode)
                {
                    delta_y = BUNKR_MACRO_BBOX_Y1 - bullet_y0;
                    if ((delta_y > 0) && (delta_y <= 16))
                    { // Macro check for y-axis
                        // Macro test x-axis where do we have overlap, if any?
                        delta_x = bullet_x0 - BUNKR_ZERO_X0;
                        // Macro CD x-axis -- at least partially inside left edge of 1st bunker and right edge of last bunker
                        // Last Bunker x1 + alien width, 3*45 + 22 + Alien_width[alien_num] (45 = width bunker+gap)
                        if ((delta_x >= 0) && (delta_x < 45 + 45 + 45 + 22))
                        {
                            // x-axis binary search for overlap with bunker, if any
                            bunkr_num = 4; // default value indicates no alignment with any bunker
                            if (delta_x >= 45 + 45)
                            { // left or right side of (approx) midpoint of the 4 bunkers?
                                if (delta_x >= 45 + 45 + 45)
                                {
                                    bunkr_num = 3;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_3_PLYR1_IMG_BUF + (active_player * BUNKR_3_PLYR2_IMG_BUF);
                                    delta_x -= 45 + 45 + 45;
                                }
                                else if (delta_x < 45 + 45 + 22)
                                {
                                    bunkr_num = 2;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_2_PLYR1_IMG_BUF + (active_player * BUNKR_2_PLYR2_IMG_BUF);
                                    delta_x -= 45 + 45;
                                }
                            }
                            else
                            {
                                if (delta_x >= 45 && delta_x < 45 + 22)
                                {
                                    bunkr_num = 1;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_1_PLYR1_IMG_BUF + (active_player * BUNKR_1_PLYR2_IMG_BUF);
                                    delta_x -= 45;
                                }
                                else if (delta_x < 22)
                                {
                                    bunkr_num = 0;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_0_PLYR1_IMG_BUF + (active_player * BUNKR_0_PLYR2_IMG_BUF);
                                }
                            }
                            // common code -- NOTE delta_x is updated to reflect position of colliding bunker
                            if (bunkr_num < 4)
                            {
                                bunkr_start_addr = bunkr_img_base_addr + ((24 - delta_y) * 64) + (2 * (delta_x + 5)); // image offset is 5 px, 2 bytes/px, clear 2nd byte of 2 bytes/px
                                if (bunkr_bullet_micro_collision(bunkr_start_addr) > 0)
                                {
                                    bullet_micro_bunkr_hit = 1;
                                    Bullet.explos_ticks = BULLET_EXPL_TICKS;
                                    bullet_y -= 2; // this is to move explosion up to match the hole being made in bunker (the hole is bullet y - 2)
                                    bullet_image_ptr = BULLET_EXPL_IMG_BASE;
                                    erase_bunkr_bullet_explos(bunkr_start_addr - (64 * 2) - (2 * 3));
                                }
                            }
                        }
                    }
                } // END BULLET BUNKER CD

                // PERF - perf measurement
                // values[2] = VIA_irq_count;
                // values[3] = *via_count_h;

                // BULLET to ALIEN COLLISION
                // ###########################
                // NEW Bullet/alien colliion detect
                // alien_binary_search_index array holds the alien #'s corresponding to the columns used in the decision tree
                //      after each row is prcessed the array values are increment by +11 (e.g. starting at 5, then 16,27,38,49)
                //      The indices into the Index_array are constant and based on the columns defined by the decision tree logic
                delta_x = bullet_x0 - alien_ref_x;        // based on x increasing left to right, ref is left side of matrix
                delta_y = bullet_y0 - (alien_ref_y - 64); // +??? for sprite offset, based on y increasing from top to bottom, ref is top of matrix
                if ((bullet_hit == 0) && (delta_y >= 4) && (delta_y <= (64 + 12)))
                { // 8 to account for alien x1
                    delta_rel_y = (uint8_t)delta_y & 0x0F;
                    alien_row_num = 4 - ((uint8_t)delta_y >> 4);
                    if (delta_x >= 0 && delta_x < INVR_MTRX_WIDTH + 16)
                    {
                        delta_rel_x = (uint8_t)delta_x & 0x0F; // get the bullet position relative to alien x (which is the remainder after divide by 16)
                        alien_col_num = (uint8_t)delta_x >> 4; // divide by 16 to get column number
                        num_of_alien_hit = (alien_row_num * 11) + alien_col_num;
                        if (Players_Alien_Exists[active_player][num_of_alien_hit] == 1)
                        {
                            if (delta_rel_y < Alien_bbox_y1[num_of_alien_hit] && (delta_rel_y >= Alien_bbox_y0[num_of_alien_hit]))
                                alien_y_hit = 1;
                            if (Alien_update[num_of_alien_hit] != alien_ref_update)
                            {
                                // hit alien hasn't been updated, if moving right subtract 2, left add 2 to bbox
                                if (Player[active_player].alien_x_incr == +2)
                                { // moving aliens to the right, offset bbox to the left by 2
                                    if ((delta_rel_x >= Alien_bbox_x0[num_of_alien_hit] - 2) && (delta_rel_x < Alien_bbox_x1[num_of_alien_hit] - 2))
                                        alien_x_hit = 1;
                                }
                                else
                                {
                                    if ((delta_rel_x >= Alien_bbox_x0[num_of_alien_hit] + 2) && (delta_rel_x < Alien_bbox_x1[num_of_alien_hit] + 2))
                                        alien_x_hit = 1;
                                }
                            }
                            else
                            { // hit alien has already been updated to match reference pos
                                if ((delta_rel_x >= Alien_bbox_x0[num_of_alien_hit]) && (delta_rel_x < Alien_bbox_x1[num_of_alien_hit]))
                                {
                                    alien_x_hit = 1;
                                }
                            }
                            alien_hit = alien_x_hit && alien_y_hit;
                            alien_x_hit = 0;
                            alien_y_hit = 0;
                            if (alien_hit == 1)
                            {
                                Alien_unoccupied_cols_per_row[active_player][alien_row_num]++; // count number of terminated aliens in each row & column
                                Alien_unoccupied_rows_per_col[active_player][alien_col_num]++;
                                bullet_y = DISAPPEAR_Y; // disapper bullet
                                // use explosion done time to delay spawning next bullet until alien explos is over
                                Bullet.explos_ticks = INVR_EXPL_TICKS;
                                alien_explos_ticks = INVR_EXPL_TICKS;
                                // keep score
                                // NOTE - score values are divided by 10 (LSD always zero)
                                if (num_of_alien_hit < 21)
                                    Player[active_player].score += 1;
                                else if (num_of_alien_hit < 43)
                                    Player[active_player].score += 2;
                                else
                                    Player[active_player].score += 3;
                                if (true)
                                {
                                    // enable SFX and reset SFX loop counter
                                    alien_explosion_sfx_enable = true;
                                    bullet_loops = 0;
                                    // load explosion parameters
                                    wave = 4;                // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
                                    bullet_freq = 50;        // Hz * 3
                                    duty = 90;               // % = duty/256
                                    attack_volume_atten = 7; // max = 15 for each nibble
                                    attack_time = 2;
                                    decay_volume_atten = 11;
                                    decay_time = 1;
                                    release_time = 0;
                                    load_SFX_base_parameters(BULLET_SFX_BASE_ADDR);
                                }
                            }
                        }
                    }
                } // END BULLET TO ALIEN  COLLISION DETECT

                // PERF - perf measurement
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // COLLISION BULLET to SAUCER
                // ##########################
                if (Saucer.score_start_time > 0)
                    Saucer.score_start_time--;
                if (Saucer.score_start_time == 1)
                { // since it's a one time event, need to use value = 1 (not 0)
                    saucer_expl_score_image_ptr = Saucer_Score.image_ptr;
                }
                if (bullet_hit == 0 && Saucer.exists)
                {
                    if (bullet_y < (SAUCER_BASE_Y + SAUCER_BBOX_Y1))
                    {
                        if (bullet_y1 >= (SAUCER_BASE_Y + SAUCER_BBOX_Y0))
                        {
                            if (bullet_x0 >= (Saucer.x + SAUCER_BBOX_X0))
                            { // -3 to account for bullet sprite offset from x by +3 pixels
                                if (bullet_x1 <= (Saucer.x + SAUCER_BBOX_X1))
                                {
                                    bullet_saucer_hit = 1;
                                    Saucer.sfx = true;
                                    Bullet.explos_ticks = SAUCER_EXPL_TICKS;
                                    Saucer.score_start_time = SAUCER_EXPL_TICKS / 2;
                                    // take bullet and Saucer off screen and replace with SAUCER EXPLO followed by SAUCER SCORE
                                    Saucer.explosion_x = Saucer.x - 8;
                                    Saucer.exists = false;
                                    bullet_y = DISAPPEAR_Y; // disappear bullet and saucer, add (appear) saucer explos/score sprite
                                    Saucer.exists = false;
                                    // clear spawn timer to make it inactive and immediately disasble spawning
                                    Saucer.next_spawn_time = 0;
                                    Saucer.spawn_enable = false;
                                    // do scoring
                                    Player[active_player].score += Saucer_Score_Table[Player[active_player].bullets_fired];
                                    switch (Saucer_Score_Table[Player[active_player].bullets_fired])
                                    {
                                    case 5:
                                        Saucer_Score.image_ptr = SAUCER_SCORE50_IMG_BASE;
                                        break;
                                    case 10:
                                        Saucer_Score.image_ptr = SAUCER_SCORE100_IMG_BASE;
                                        break;
                                    case 15:
                                        Saucer_Score.image_ptr = SAUCER_SCORE150_IMG_BASE;
                                        break;
                                    case 30:
                                        Saucer_Score.image_ptr = SAUCER_SCORE300_IMG_BASE;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } // END BULLET TO SAUCER COLLSION DETECT

                // COLLISION BULLET to UPPER BNDRY
                // ###############################
                if ((bullet_y < TOP_BOUNDARY) && (bullet_boundary_hit == 0))
                {
                    bullet_boundary_hit = 1;
                    Bullet.explos_ticks = BULLET_EXPL_TICKS;
                    bullet_image_ptr = BULLET_EXPL_IMG_BASE; // Boundary explosion address
                }
                // END BULLET COLLISION DETECT/HANDLER

                // PERF - Second Position - POST BULLET MOVE/SPAWN/COLLISION DETECTION
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // ##############################################
                // #############   BOMB MOVE/SPAWN   ############
                // ##############################################

                if (Game.bomb_spawn_time > 0)
                    Game.bomb_spawn_time -= 1; // counts down, stops at zero
                if (Game.bomb_spawn_time == 1)
                {
                    Game.bomb_spawn_enable = true;
                }
                // initializing var to track which bomb just moved/spawned, default is 3 (none), otherwise 0-2
                // 3 indicates no bomb selected which triggers a do-over after continuing to the next loop,
                bomb_num_just_updated = 3; // default, indicates none have been updated
                if (Player[active_player].num_of_aliens < 9)
                    bomb_speed = 5;

                // #############
                // SCREW BOMB 0
                // #############
                if (bomb_type_counter == 0)
                {                          // run screw handler, if count = zero and "skip" if false
                    bomb_type_counter = 2; // since reached zero, reset bomb type counter to starting count = 2
                    // ###  MOVE  ###
                    if (Bomb[0].exists == true)
                    {
                        if (Bomb[0].hit == 0)
                        { // if not exploding, move & animate it
                            Bomb[0].y += bomb_speed;
                            Bomb[0].y0 = Bomb[0].y + BOMB_BBOX_SCREW_Y0;
                            Bomb[0].y1 = Bomb[0].y + BOMB_BBOX_Y1;
                            Bomb[0].numbr_steps_taken++;
                            Bomb[0].anim_sequ_num += 1;
                            if (Bomb[0].anim_sequ_num > 3)
                                Bomb[0].anim_sequ_num = 0;
                            bomb_num_just_updated = 0;
                        }
                    }
                    else
                    { // bomb doesn't exist so SPAWN one
                        // ###  SPAWN  ###
                        // Define screw sprite pos, image/anim sequ, update steps, inidcate existence
                        if (bomb_screw_skip == 0)
                        { // don't skip the first cycle, but every other cycle after that
                            if ((Bomb[1].numbr_steps_taken == 0) || (Bomb[1].numbr_steps_taken > bomb_reload_rate))
                            {
                                if ((Bomb[2].numbr_steps_taken == 0) || (Bomb[2].numbr_steps_taken > bomb_reload_rate))
                                {
                                    if (Game.bomb_spawn_enable && (Player[active_player].num_of_aliens > 0) && (Gunner.exists) && !Gunner.blown_up)
                                    {
                                        // spawn it the gunner is in range, otherwise don't
                                        alien_col_num = 11;                         // default is no column (which is flagged by col = 11)
                                        delta_rel_x = (Gunner.x + 8) - alien_ref_x; // center of gunner minus left edge of matrix
                                        if (delta_rel_x > 0)
                                        {
                                            alien_col_num = delta_rel_x >> 4;
                                        }
                                        if (alien_col_num < 11)
                                        {
                                            if (Alien_unoccupied_rows_per_col[active_player][alien_col_num] < 5)
                                            { // valid col # (>0n & <11) and col is occupied
                                                // if column isn't empty, find the row # of the first alien from the bottom and it's relative y position
                                                alien_num_to_drop_bomb_from = alien_col_num;
                                                Bomb[0].drop_rel_y = alien_ref_y + 21; // +17 to start drop 5 px below bottom edge of lowest alien
                                                // find first alien from the bottom or skip if column is empty
                                                for (i = 0; i < 5; i++)
                                                {
                                                    if ((Players_Alien_Exists[active_player][alien_num_to_drop_bomb_from] == 0) ||
                                                        ((alien_hit > 0) && (num_of_alien_hit == alien_num_to_drop_bomb_from)))
                                                    {
                                                        alien_num_to_drop_bomb_from += 11;
                                                        Bomb[0].drop_rel_y -= 16;
                                                    }
                                                    else
                                                    {
                                                        // left edge x is upper 4 bits of delta x, +5 to move center of bomb to center of alien
                                                        // ... launch bomb from
                                                        Bomb[0].x = alien_ref_x + (delta_rel_x & 0xF0) + 5;
                                                        Bomb[0].y = Bomb[0].drop_rel_y;
                                                        Bomb[0].x0 = Bomb[0].x + BOMB_BBOX_X0;
                                                        Bomb[0].x1 = Bomb[0].x + BOMB_BBOX_X1;
                                                        Bomb[0].y0 = Bomb[0].y + BOMB_BBOX_SCREW_Y0;
                                                        Bomb[0].y1 = Bomb[0].y + BOMB_BBOX_Y1;
                                                        Bomb[0].anim_sequ_num = 0;
                                                        Bomb[0].exists = true;
                                                        bomb_num_just_updated = 0;
                                                        Bomb[0].numbr_steps_taken = 1; // now that it exists, increment # of steps to 1
                                                        // if row > 0 and alien in row - 1 is exploding, then spawing bomb is colliding
                                                        //      with exploding alien just below
                                                        if (alien_num_to_drop_bomb_from > 10)
                                                        {
                                                            if ((alien_hit > 0) && ((alien_num_to_drop_bomb_from - 11) == num_of_alien_hit))
                                                            {
                                                                Bomb[0].hit = 1;
                                                            }
                                                        }
                                                        break; // found first alien in column, so stop looking
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        else
                            bomb_screw_skip = 0; // this is set to 1 when screw bomb terminates to prevent immediately respawn
                    }
                }
                else
                    bomb_type_counter -= 1; // decrement if not zero
                // END SCREW BOMB HANDLER

                // ##############
                // SPIKE BOMB
                // ##############
                // BOMB 1 MOVE/SPAWN
                if (bomb_type_selector == 1)
                { // run spike handler
                    // ### MOVE ###
                    if (Bomb[1].exists == true)
                    {
                        if (Bomb[1].hit == 0)
                        {
                            Bomb[1].y += bomb_speed;
                            Bomb[1].y0 = Bomb[1].y + BOMB_BBOX_SPIKE_Y0;
                            Bomb[1].y1 = Bomb[1].y + BOMB_BBOX_Y1;
                            Bomb[1].numbr_steps_taken++;
                            Bomb[1].anim_sequ_num += 1;
                            if (Bomb[1].anim_sequ_num > 3)
                                Bomb[1].anim_sequ_num = 0;
                            bomb_num_just_updated = 1;
                        }
                    }
                    // ### SPAWN ###
                    else
                    {
                        if (Bomb[0].numbr_steps_taken == 0 || (Bomb[0].numbr_steps_taken > bomb_reload_rate))
                        {
                            if (Bomb[2].numbr_steps_taken == 0 || (Bomb[2].numbr_steps_taken > bomb_reload_rate))
                            {
                                // bomb 1 drops are disabled when one alien remains
                                if (Game.bomb_spawn_enable && (Player[active_player].num_of_aliens > 1) && (Gunner.exists) && !Gunner.blown_up)
                                {
                                    // select column from table, if empty, go to next column, repeat until column not empty
                                    alien_col_num = Bomb_Column_Sequ[Player[active_player].col_index_spike++];
                                    alien_num_to_drop_bomb_from = alien_col_num;
                                    if (Player[active_player].col_index_spike > 14)
                                    {
                                        Player[active_player].col_index_spike = 0;
                                    }
                                    // starts 1 px higher than others, since it's 1 px shorter
                                    Bomb[1].drop_rel_y = alien_ref_y + 20; // +16 to start drop 4 px below bottom edge of lowest alien
                                    if (Alien_unoccupied_rows_per_col[active_player][alien_col_num] < 5)
                                    { // valid col # (>0n & <11) and col is occupied
                                        for (i = 0; i < 5; i++)
                                        { // check if column is occupied, starting at bottom and working up
                                            if ((Players_Alien_Exists[active_player][alien_num_to_drop_bomb_from] == 0) ||
                                                ((alien_hit > 0) && (num_of_alien_hit == alien_num_to_drop_bomb_from)))
                                            {
                                                alien_num_to_drop_bomb_from += 11;
                                                Bomb[1].drop_rel_y -= 16;
                                            }
                                            else
                                            {
                                                Bomb[1].x = alien_ref_x + (alien_col_num * 16) + 5;
                                                Bomb[1].y = Bomb[1].drop_rel_y;
                                                Bomb[1].x0 = Bomb[1].x + BOMB_BBOX_X0;
                                                Bomb[1].x1 = Bomb[1].x + BOMB_BBOX_X1;
                                                Bomb[1].y0 = Bomb[1].y + BOMB_BBOX_SPIKE_Y0;
                                                Bomb[1].y1 = Bomb[1].y + BOMB_BBOX_Y1;
                                                Bomb[1].anim_sequ_num = 0;
                                                Bomb[1].exists = true;
                                                bomb_num_just_updated = 1;
                                                Bomb[1].numbr_steps_taken = 1; // now that it exists, increment # of steps to 1
                                                if (alien_num_to_drop_bomb_from > 10)
                                                {
                                                    if ((alien_hit > 0) && ((alien_num_to_drop_bomb_from - 11) == num_of_alien_hit))
                                                    {
                                                        Bomb[1].hit = 1;
                                                    }
                                                }
                                                break; // found lowest alien in col, so quit looking
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } // END SPIKE BOMB 1 MOVE SPAWN

                // #################
                // SAWTOOTH BOMB
                // #################
                if (bomb_type_selector == 2)
                { // run sawtooth handler
                    // ### MOVE ###
                    if (Bomb[2].exists == true)
                    {
                        if (Bomb[2].hit == 0)
                        {
                            Bomb[2].y += bomb_speed;
                            Bomb[2].y0 = Bomb[2].y + BOMB_BBOX_SAWTOOTH_Y0;
                            Bomb[2].y1 = Bomb[2].y + BOMB_BBOX_Y1;
                            Bomb[2].numbr_steps_taken++;
                            Bomb[2].anim_sequ_num += 1;
                            if (Bomb[2].anim_sequ_num > 3)
                                Bomb[2].anim_sequ_num = 0;
                            bomb_num_just_updated = 2;
                        }
                    }
                    else
                    {
                        if (Bomb[0].numbr_steps_taken == 0 || (Bomb[0].numbr_steps_taken > bomb_reload_rate))
                        {
                            if (Bomb[1].numbr_steps_taken == 0 || (Bomb[1].numbr_steps_taken > bomb_reload_rate))
                            {
                                if (Game.bomb_spawn_enable && (Gunner.exists) && !Gunner.blown_up)
                                {
                                    // select column from table, if empty, go to next column, repeat until column not empty
                                    alien_col_num = Bomb_Column_Sequ[Player[active_player].col_index_sawtooth];
                                    alien_num_to_drop_bomb_from = alien_col_num;
                                    if (++Player[active_player].col_index_sawtooth > 15)
                                        Player[active_player].col_index_sawtooth = 0;
                                    // starts 1 px higher than others, since it's 1 px shorter
                                    Bomb[2].drop_rel_y = alien_ref_y + 21; // +17 to start drop 5 px below bottom edge of lowest alien
                                    if (Alien_unoccupied_rows_per_col[active_player][alien_num_to_drop_bomb_from] < 5)
                                    { // valid col # (>0n & <11) and col is occupied
                                        for (i = 0; i < 5; i++)
                                        { // check if column is occupied, starting at bottom and working up
                                            if ((Players_Alien_Exists[active_player][alien_num_to_drop_bomb_from] == 0) ||
                                                ((alien_hit > 0) && (num_of_alien_hit == alien_num_to_drop_bomb_from)))
                                            {
                                                alien_num_to_drop_bomb_from += 11;
                                                Bomb[2].drop_rel_y -= 16;
                                            }
                                            else
                                            {
                                                Bomb[2].x = alien_ref_x + (alien_col_num * 16) + 5;
                                                Bomb[2].y = Bomb[2].drop_rel_y;
                                                Bomb[2].x0 = Bomb[2].x + BOMB_BBOX_X0;
                                                Bomb[2].x1 = Bomb[2].x + BOMB_BBOX_X1;
                                                Bomb[2].y0 = Bomb[2].y + BOMB_BBOX_SAWTOOTH_Y0;
                                                Bomb[2].y1 = Bomb[2].y + BOMB_BBOX_Y1;
                                                Bomb[2].anim_sequ_num = 3;
                                                Bomb[2].exists = true;
                                                bomb_num_just_updated = 2;
                                                Bomb[2].numbr_steps_taken = 1; // now that it exists, increment # of steps to 1
                                                if (alien_num_to_drop_bomb_from > 10)
                                                {
                                                    if ((alien_hit > 0) && ((alien_num_to_drop_bomb_from - 11) == num_of_alien_hit))
                                                    {
                                                        Bomb[2].hit = 1;
                                                    }
                                                }
                                                break; // found lowest alien in col, so quit looking
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } // END SAWTOOTH BOMB 2 MOVE SPAWN

                // #########################################################
                // ###########     BOMB COLLISION DETECTION     ############
                // #########################################################
                // NOTE; need to know if Gunner is hit b4 executing handlers, so we can shutdown alien movement, but let inflight objects terminate naturually
                // 0 = no collision, 1 = alien, 2 = bullet, 3 = macro_bunkr, 4 = micro_bunkr, 5 = ground, 6 = gunner
                //      all but gunner collisions trigger a bomb explosion image
                //      the explosion duration for actual explosions is the same all that have one
                //      but the duration for a gunner collisions matches the gunner explosion duration

                // ########################################
                // BOMB hits BULLET (NOT>>> bullet to bomb)
                // ########################################
                // hit = 2 for contact with bullet (but not the other way around)
                if (bullet_x0 >= Bomb[bomb_num_just_updated].x0 && bullet_x0 < Bomb[bomb_num_just_updated].x1)
                {
                    bullet_y1 = bullet_y + 4;
                    ShotOverlapTop = bullet_y1 - Bomb[bomb_num_just_updated].y;
                    ShotOverlapBottom = Bomb[bomb_num_just_updated].y1 - bullet_y0;
                    ShotColumn = 2 + bullet_x0 - Bomb[bomb_num_just_updated].x0;
                    if (ShotOverlapBottom > 0 && ShotOverlapBottom < 13 && ShotOverlapTop > 0 && ShotOverlapTop < 13)
                    {
                        if (ShotOverlapTop < 4)
                        {
                            BombTopRow = 0;
                            BombRows = ShotOverlapTop;
                        }
                        else if (ShotOverlapBottom < 4)
                        {
                            BombTopRow = 8 - ShotOverlapBottom;
                            BombRows = ShotOverlapBottom;
                        }
                        else
                        {
                            BombTopRow = bullet_y0 - Bomb[bomb_num_just_updated].y;
                            BombRows = 4;
                        }
                        bomb_image_mem = BOMB_IMG_BASE + (((bomb_num_just_updated * 4) + Bomb[bomb_num_just_updated].anim_sequ_num) * SPR_8X8_SIZE);
                        Bomb[bomb_num_just_updated].hit = 2 * bullet_bomb_micro_collision(bomb_image_mem, BombTopRow, ShotColumn, BombRows);
                    }
                    if (Bomb[bomb_num_just_updated].hit == 2)
                    {
                        Bomb[bomb_num_just_updated].explos_ticks = BOMB_EXPL_TICKS;
                    }
                }

                // RESTORE this var so it can be used later in another COLLISION DETECTION algo
                bullet_y1 = bullet_y + 1;

                // ###################################
                // COLLISION BOMB to EXPLODING ALIEN
                // ###################################
                // hit = 1 for contact with "alien explosion" in progress
                //      The detection occurs on prior tick in the move/spawn section, but acted on here (1 tick later)
                if (Bomb[bomb_num_just_updated].hit == 1)
                {
                    Bomb[bomb_num_just_updated].explos_ticks = BOMB_EXPL_TICKS;
                }

                // ##########################
                // COLLISION BOMB to BUNKER
                // ##########################
                // Macro CD y-axis
                if ((Bomb[bomb_num_just_updated].hit != 4) && !bypass_test_mode)
                {
                    delta_y = Bomb[bomb_num_just_updated].y - BUNKR_Y;
                    if ((delta_y > 0) && (delta_y <= 16 + 4))
                    { // Macro check for y-axis, using +4 to allow hit when 1/2 of bomb is non-overlapping at bottom of bunker
                        // Macro test x-axis where do we have overlap, if any?
                        delta_x = Bomb[bomb_num_just_updated].x0 - BUNKR_ZERO_X;
                        // Macro CD x-axis -- need at least 1 px overlap of BOMB on left and right edges of bunker
                        // Less than last Bunker x1 and greater than num px's to Bomb X1 (3)...45 = width bunker+gap, 27 = distance from bunker x to bunker x1
                        if ((delta_x > 2) && (delta_x < 45 + 45 + 45 + 27))
                        {
                            // x-axis binary search for overlap with bunker, if any
                            bunkr_num = 4; // default value indicates no alignment with any bunker
                            if (delta_x > 45 + 45 + 2)
                            { // is bomb on the left or right side of (approx) midpoint of the 4 bunkers?
                                if (delta_x >= 45 + 45 + 45 + 2)
                                {
                                    bunkr_num = 3;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_3_PLYR1_IMG_BUF + (active_player * BUNKR_3_PLYR2_IMG_BUF);
                                    delta_x -= 45 + 45 + 45;
                                }
                                else if (delta_x < 45 + 45 + 27)
                                {
                                    bunkr_num = 2;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_2_PLYR1_IMG_BUF + (active_player * BUNKR_2_PLYR2_IMG_BUF);
                                    delta_x -= 45 + 45;
                                }
                            }
                            else
                            {
                                if (delta_x > 45 && delta_x < 45 + 27)
                                {
                                    bunkr_num = 1;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_1_PLYR1_IMG_BUF + (active_player * BUNKR_1_PLYR2_IMG_BUF);
                                    delta_x -= 45;
                                }
                                else if (delta_x < 27)
                                {
                                    bunkr_num = 0;
                                    bunkr_img_base_addr = (1 - active_player) * BUNKR_0_PLYR1_IMG_BUF + (active_player * BUNKR_0_PLYR2_IMG_BUF);
                                }
                            }
                            // common code -- NOTE delta_x is updated to reflect position of colliding bunker
                            if (bunkr_num < 4)
                            {
                                // fast math using "switch", get bomb image offsets based on bomb # and animation sequence #, add them to image base address
                                switch (bomb_num_just_updated)
                                {
                                case 0:
                                    bomb_img_addr_offset1 = 0;
                                    break;
                                case 1:
                                    bomb_img_addr_offset1 = 512;
                                    break;
                                case 2:
                                    bomb_img_addr_offset1 = 1024;
                                    break;
                                }
                                // adding 2 for bomb image starting address relative to base address, x2 since 2 bytes/px
                                switch (Bomb[bomb_num_just_updated].anim_sequ_num)
                                {
                                case 0:
                                    bomb_img_addr_offset2 = 0 + (2 * 2);
                                    break;
                                case 1:
                                    bomb_img_addr_offset2 = 128 + (2 * 2);
                                    break;
                                case 2:
                                    bomb_img_addr_offset2 = 256 + (2 * 2);
                                    break;
                                case 3:
                                    bomb_img_addr_offset2 = 384 + (2 * 2);
                                    break;
                                }
                                // starting address of bomb template for collision detection
                                bomb_img_start_addr = BOMB_IMG_BASE + bomb_img_addr_offset1 + bomb_img_addr_offset2;
                                // define position of explosion and hole that will be made in bunker image
                                bunkr_start_addr1 = bunkr_img_base_addr + (delta_y * 64) + (2 * delta_x);
                                if (bunkr_bomb_micro_collision(bunkr_start_addr1, bomb_img_start_addr) > 0)
                                {
                                    if (delta_y < 4)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 4;
                                        bunkr_start_addr2 = 4 * 64;
                                    }
                                    else if (delta_y < 8)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 8;
                                        bunkr_start_addr2 = 8 * 64;
                                    }
                                    else if (delta_y < 12)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 12;
                                        bunkr_start_addr2 = 12 * 64;
                                    }
                                    else if (delta_y < 16)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 16;
                                        bunkr_start_addr2 = 16 * 64;
                                    }
                                    else if (delta_y < 20)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 20;
                                        bunkr_start_addr2 = 20 * 64;
                                    }
                                    else if (delta_y < 24)
                                    {
                                        Bomb[bomb_num_just_updated].y = BUNKR_Y + 24;
                                        bunkr_start_addr2 = 24 * 64;
                                    }
                                    bomb_micro_bunkr_hit = true;
                                    Bomb[bomb_num_just_updated].hit = 4;
                                    Bomb[bomb_num_just_updated].explos_ticks = BOMB_EXPL_TICKS;
                                    // image pointer is handled in SPRITE UPDATE code
                                    // DON"T TOUCH
                                    bunkr_start_addr2 = bunkr_img_base_addr + bunkr_start_addr2 + (2 * delta_x) - 4;
                                    erase_bunkr_bomb_explos(bunkr_start_addr2);
                                }
                            }
                        }
                    }
                } // END BOMB BUNKER CD

                // ##################
                // BOMB hits GROUND
                // ##################
                // hit = 5 for ground, show bomb explosion
                if (Bomb[bomb_num_just_updated].y >= 224)
                {
                    Bomb[bomb_num_just_updated].hit = 5;
                    Bomb[bomb_num_just_updated].y = 224;
                    Bomb[bomb_num_just_updated].explos_ticks = BOMB_EXPL_TICKS;
                }

                // ####################################################
                // BOMB hits GUNNER  OR  ALIEN LANDS on the ground
                // ####################################################
                // hit = 6 for gunner hit, alien_landed = true for alien landed, DON'T SHOW bomb explosion for either
                // action taken are the same except, if collision wiht BOMB, BOMB is disappeared
                if (!Gunner.hit)
                {
                    if (((Bomb[bomb_num_just_updated].y1) > (GUNNER_Y_BASE + GUNNER_BBOX_Y0)) &&
                        ((Bomb[bomb_num_just_updated].y0) < (GUNNER_Y_BASE + GUNNER_BBOX_Y1)))
                    {
                        if (Bomb[bomb_num_just_updated].x1 > (Gunner.x + GUNNER_BBOX_X0) &&
                            Bomb[bomb_num_just_updated].x0 < (Gunner.x + GUNNER_BBOX_X1))
                        {
                            Bomb[bomb_num_just_updated].hit = 6;
                            Bomb[bomb_num_just_updated].y = DISAPPEAR_Y; // disappear bomb, but don't reset "exists"/start spawn until Gunner Explos is done
                        }
                    }
                    if ((Bomb[bomb_num_just_updated].hit == 6) || alien_landed)
                    { // round has ended, do final "end of round" clean up
                        Gunner.hit = true;
                        if (Player[active_player].lives > 0)
                            Player[active_player].lives--;
                        update_numerical_lives(!blink, Player[active_player].lives);
                        Gunner.explos_ticks = GUNNER_EXPL_TICKS;
                        Bomb[bomb_num_just_updated].exists = false;
                        Bomb[bomb_num_just_updated].hit = 0;
                        Game.bomb_spawn_enable = false;
                        Game.bomb_spawn_time = 0;
                        Saucer.spawn_enable = false;
                        // clear spawn timer to make it inactive and immediately disasble spawning
                        Saucer.next_spawn_time = 0;
                        Bullet.spawn_enable = false;
                        // initialize SFX
                        RIA.addr0 = GUNNER_SFX_BASE_ADDR + PAN_GATE; // channel 0 = gunner, address for ch0 = 0xFF00
                        RIA.rw0 = 1 & 1;                             // Turn on FX
                        // turn off alien march SFX
                        alien_march_sfx_enable = false;
                    }
                } // END BOMB to GUNNER and ALIEN to GROUND COLLISION DETECTION

                // PERF - Third Position POST BOMB MOVE/SPAWN/COLLISION DETECTION
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // ####################################################################################################
                // ################################    OBJECT TERMINAtION HANDLER     #################################
                // ####################################################################################################
                //  Handle explosions/timers, anim/image changes, flags, terminations

                // BULLET HANDLER - TERMINATION
                // ############################
                bullet_hit_subset1 = bullet_boundary_hit + bullet_micro_bunkr_hit + bullet_bomb_hit;
                bullet_hit_subset2 = bullet_saucer_hit + alien_hit;
                bullet_hit = bullet_hit_subset1 + bullet_hit_subset2; // flag any collision with a bullet
                if (Bullet.explos_ticks > 0)
                    Bullet.explos_ticks--;
                if (bullet_hit > 0)
                {
                    if (bullet_hit_subset2 > 0)
                    {
                        if (Player[active_player].score > 1229)
                            bomb_reload_rate = 7;
                        else if (Player[active_player].score > 819)
                            bomb_reload_rate = 8;
                        else if (Player[active_player].score > 409)
                            bomb_reload_rate = 11;
                        else if (Player[active_player].score > 51)
                            bomb_reload_rate = 16;
                        else
                            bomb_reload_rate = 48;
                    }
                    if (Bullet.explos_ticks == 0)
                    {
                        if (bullet_hit_subset2 > 0)
                            update_score_board();
                        // turn off SFX
                        if (alien_hit == 1)
                        {
                            RIA.addr0 = BULLET_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 0;
                            bullet_loops = 0;
                        }
                        // turn off SFX, disappear Saucer Explosion, update img ptr to Saucer image
                        if (bullet_saucer_hit == 1)
                        {
                            Saucer.explosion_x = DISAPPEAR_X;
                            // FIX - if saucer is hit, everything seems normal except in single player mode, if a bullet passes over the space
                            // ... where the saucer was when it was hit, then another explosion/score occur, as though the saucer was being hit
                            // ... even though it is not there (doesn't exist)
                            Saucer.x = DISAPPEAR_X;
                            Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME;
                            saucer_expl_score_image_ptr = SAUCER_MAGENTA_EXPLOS_IMG_BASE;
                            RIA.addr0 = SAUCER_SFX_BASE_ADDR + PAN_GATE; // byte 6 = pan/gate
                            RIA.rw0 = 0;
                        }
                        // clear flags
                        bullet_hit = 0;
                        bullet_bomb_hit = 0;
                        bullet_micro_bunkr_hit = 0;
                        bullet_saucer_hit = 0;
                        bullet_boundary_hit = 0;
                        bullet_y = DISAPPEAR_Y; // disappear bullet
                        Bullet.exists = false;
                        Gunner.shoot = false;
                        // spawn bullet if gunner is not hit, otherwise wait until Gunner spawns
                        Bullet.reload = 1;
                    }
                    else
                    {
                        // do explosion SFX
                        if (alien_explosion_sfx_enable && true)
                        {
                            // transition to explosion SFX - load new bullet SFX parameters for explosion
                            // PUSH PLAY
                            RIA.addr0 = BULLET_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 1;
                            bullet_freq = 1500 - (bullet_loops * 50);
                            RIA.addr0 = BULLET_SFX_BASE_ADDR;
                            RIA.step0 = 1;
                            RIA.rw0 = bullet_freq & 0xFF;
                            RIA.rw0 = (bullet_freq >> 8) & 0xFF;
                            bullet_loops++;
                        }
                        else
                        {
                            RIA.addr0 = BULLET_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 0;
                        }
                        if (bullet_saucer_hit == 1 && 1)
                        {
                            if (frequency <= 500)
                                ramp_up = true;
                            if (frequency > 2500)
                                ramp_up = false;
                            if (ramp_up)
                                frequency += 1564;
                            else
                                frequency -= 1564;
                            // load frequ
                            RIA.addr0 = SAUCER_SFX_BASE_ADDR;
                            RIA.step0 = 1;
                            RIA.rw0 = frequency & 0xFF;
                            RIA.rw0 = (frequency >> 8) & 0xFF;
                        }
                        else
                        {
                            RIA.addr0 = SAUCER_SFX_BASE_ADDR + PAN_GATE;
                            RIA.rw0 = 0;
                        }
                    }
                }
                // PERF
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // ALIEN TERMINATION HANDLER
                // #########################
                if (alien_explos_ticks > 0)
                    alien_explos_ticks--;
                if (alien_hit == 1)
                {
                    // IF time expired, alien explosion done, terminate alien, adjust # of aliens, set/reset flags
                    if (alien_explos_ticks == 0)
                    {
                        alien_explosion_done = true;                               // flag transition to completion
                        alien_hit = 0;                                             // clear collision flag to terminate explosion cycle
                        Players_Alien_Exists[active_player][num_of_alien_hit] = 0; // terminate alien
                        Player[active_player].num_of_aliens--;
                        // turn off SFX
                        alien_explosion_sfx_enable = false;
                        bullet_loops = 0;
                        // level complete???
                        if (Player[active_player].num_of_aliens == 0)
                        {
                            level_completed = true;
                            Game.bomb_spawn_enable = false;
                            Game.bomb_spawn_time = 0;
                            Saucer.spawn_enable = false;
                            Saucer.next_spawn_time = 0;
                            Bullet.spawn_enable = false;
                            alien_march_sfx_enable = false;
                            RIA.addr0 = SFX_BASE_ADDR + PAN_GATE;
                            RIA.step0 = 8;
                            for (i = 0; i < 8; i++)
                                RIA.rw0 = 0x00;
                        }
                        // update ALIEN MARCH SFX rate based on # aliens remaining
                        else if (Alien_March_SFX_Threshold[Player[active_player].alien_march_index] > Player[active_player].num_of_aliens)
                        {
                            // move index to next faster pulse rate, note... actual rate is not updated until current cycle is over
                            Player[active_player].alien_march_index++;
                        }
                    }
                    else
                    {
                        alien_explosion_sfx_enable = true;
                    }
                } // END ALIEN TERM

                // PERF
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // BOMB HANDLER - TERMINATION
                // ##########################
                for (i = 0; i < 3; i++)
                {
                    if (Bomb[i].explos_ticks > 0)
                        Bomb[i].explos_ticks--;
                    if ((Bomb[i].hit > 0) && (Bomb[i].explos_ticks == 0))
                    { // bomb explosion is done, terminate
                        // must wait one cycle after screw bomb terminates before enabling respawn
                        if (i == 0)
                            bomb_screw_skip = 1;
                        // clean up/reset flags/vars for termination
                        Bomb[i].hit = 0;
                        Bomb[i].exists = false;
                        Bomb[i].numbr_steps_taken = 0;
                        Bomb[i].y = DISAPPEAR_Y;
                    }
                }

                // PERF
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // GUNNER HANDLER - TERMINATION
                // ############################
                if (Gunner.explos_ticks > 0)
                    Gunner.explos_ticks--;
                if (Gunner.hit)
                {
                    if (Gunner.explos_ticks != 0)
                    {
                        // do explosion animation for active player's gunner, alternate between images
                        gunner_image_ptr = GUNNER_PLYR1_IMG_BASE + SPR_16X16_SIZE;
                        if (active_player == 1)
                            gunner_image_ptr = GUNNER_PLYR2_IMG_BASE + SPR_16X16_SIZE;
                        if (current_time % 16 > 8)
                        {
                            gunner_image_ptr += SPR_16X16_SIZE;
                        }
                        // do explosion SFX
                        RIA.addr0 = GUNNER_SFX_BASE_ADDR; // channel 0 = gunner, address for ch0 = 0xFF00
                        RIA.step0 = 1;
                        Sfx[GUNNER_CHAN].freq = ((rand() % (0x2A0 - 0x40 + 1)) + 0x40); // max 2A0 min 40
                        RIA.rw0 = Sfx[GUNNER_CHAN].freq & 0xFF;                         // byte 0 @ 0xFF00 = freq_lsb
                        RIA.rw0 = (Sfx[GUNNER_CHAN].freq >> 8) & 0xFF;                  // byte 1 = freq_msb
                        if (Gunner.explos_ticks > GUNNER_EXPL_TICKS - 5)
                            RIA.rw0 = ((uint8_t)((rand() % (0xF0 - 0x08 + 1)) + 0x08) & 0xFF); // byte 2 = duty cycle
                        else
                            dummy_read = RIA.rw0; // to increment addr either way
                        dummy_read = RIA.rw0;     // skip byte 3
                        // reduce volume using explosion timer, based on done_time = 45 ticks
                        RIA.rw0 = (GUNNER_EXPL_TICKS - Gunner.explos_ticks) * 2; // byte 4 = attenuation
                    }
                    else
                    { // gunner explosion is complete, time to process end end of this round
                        // Reset SFX, hit, exists, on screen presence, Set blown_up flag
                        RIA.addr0 = GUNNER_SFX_BASE_ADDR + 6; // byte 6 = pan/gate
                        RIA.rw0 = 0;                          // terminate SFX
                        Gunner.hit = false;
                        Gunner.exists = false;
                        Gunner.blown_up = true;
                        Gunner.y = DISAPPEAR_Y; // disappear gunner/explosion
                        gunner_image_ptr = GUNNER_PLYR1_IMG_BASE;
                        if (active_player == 1)
                            gunner_image_ptr = GUNNER_PLYR2_IMG_BASE;
                    }
                }

                // PERF - Fourth Position POST OBJECT (BULLET, BOMB, ALIEN, GUNNER) TERMINATION HANDLER
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // PERF
                // Fifth Position POST GAME LOGIC
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // #######################################################################################################
                // ####################################    MOVE/SPAWN HANDLER     ########################################
                // ####################################  GUNNER, SAUCER, ALIEN    ########################################
                // #######################################################################################################

                // ############   GUNNER MOVE/SPAWN   ############
                // ###############################################

                // ####  MOVE  ####
                // ################
                if (Gunner.exists)
                {
                    if (!Gunner.hit)
                    {
                        // #####   DEMO MODE AI   #####
                        Gunner.y = GUNNER_Y_BASE;
                        // this is the movement/firing algo for demo mode
                        if (!Game.play_mode)
                        {
                            if (rand() % 96 == 0)
                                gunner_demo_direction_right = !gunner_demo_direction_right;
                            // if GUNNER is out of bounds, put back in bounds and change direction
                            if (Gunner.x > 320 - 96)
                            {
                                gunner_demo_direction_right = false;
                                Gunner.x = 320 - 96;
                            }
                            if (Gunner.x < 55)
                            {
                                gunner_demo_direction_right = true;
                                Gunner.x = 55;
                            }
                            // try to keep gunner under alien matrix
                            if (Gunner.x < alien_1st_col_abs_x - 16)
                            {
                                gunner_demo_direction_right = true;
                            }
                            else if (Gunner.x > alien_last_col_abs_x + 32)
                            {
                                gunner_demo_direction_right = false;
                            }
                            // once direction is set, move the gunner
                            if (gunner_demo_direction_right == true)
                                Gunner.x += GUNNER_SPEED;
                            else
                                Gunner.x -= GUNNER_SPEED;
                        }
                        // #####   PLAY MODE   #####
                        // based on player input
                        else if (Game.play_mode)
                        {
                            if (Gunner.direction_right && Gunner.x <= 249)
                                Gunner.x += GUNNER_SPEED;
                            if (Gunner.x > 249)
                            {
                                Gunner.x = 249;
                            }
                            if (Gunner.direction_left && Gunner.x >= 55)
                                Gunner.x -= GUNNER_SPEED;
                            if (Gunner.x < 55)
                            {
                                Gunner.x = 55;
                            }
                            Gunner.direction_right = false;
                            Gunner.direction_left = false;
                        }
                    }
                }
                else
                {
                    // ####  SPAWN  ####
                    // #################
                    // got lives, but no Gunner, so spawn a new one, after spawn time is up, then reload gunner with bullet
                    if (Gunner.spawn_time == 1)
                    {
                        // update Lives ICON to remove last ICON from the tray and add simultaneously add gunner to the play area
                        Player[active_player].bonus_active = false;
                        ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + ((Player[active_player].lives - 1) + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
                        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
                        Gunner.x = GUNNER_P1_X_BASE;
                        gunner_image_ptr = GUNNER_PLYR1_IMG_BASE;
                        if (active_player == 1)
                            gunner_image_ptr = GUNNER_PLYR2_IMG_BASE;
                        Bullet.reload = 1;
                        Gunner.exists = 1;
                        Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // s/b SAUCER_SPAWN_TIME;
                        Saucer.spawn_enable = false;
                    }
                } // END GUNNER MOVE/SPAWN HANDLER

                // ####  GUNNER TIMER UPDATE  ####
                if (Gunner.spawn_time > 0)
                    Gunner.spawn_time -= 1; // decrement if timer has been started (not zero)

                // PERF - Sixth Position POST GUNNER MOVE/SPAWN
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // ########################################################
                // #############   SAUCER TERMINATE/MOVE/SPAWN   ##########
                // ########################################################

                // SAUCER - UPDATE EXISTING SAUCER... POSITION, EXPLOSIION, TERMINATION, RESPAWN TIMER
                //      if offscreen, SCHEDULE RESPAWN when terminated
                //      apply default method of termination if saucer travels off screen (not hit by bullet)
                // #########################################################################################

                // ####  UPDATE SPAWN TIMER  ####
                if (Saucer.next_spawn_time > 0)
                    Saucer.next_spawn_time--;
                if (Saucer.next_spawn_time == 1 && Player[active_player].num_of_aliens > 7)
                {
                    Saucer.spawn_enable = true; // (= 1) to create a one-shot enable
                }
                // ####  MOVE/TERMINATE  ####
                if (Saucer.exists && !level_completed && true)
                {
                    if (Saucer.left == 1)
                        Saucer.x -= SAUCER_SPEED; // move saucer one px left or right depending on direction flag
                    else
                        Saucer.x += SAUCER_SPEED;
                    // do SFX
                    RIA.addr0 = SAUCER_SFX_BASE_ADDR + PAN_GATE; // PUSH PLAY
                    RIA.rw0 = 1 & 1;
                    // do frequency ramp up then down, one frequency step per tick
                    if (frequency <= 1235)
                        ramp_up = true;
                    if (frequency > (1235 + (6 * 2127)))
                        ramp_up = false;
                    if (ramp_up)
                        frequency += 1127;
                    else
                        frequency -= 1127;
                    // load frequ
                    RIA.addr0 = SAUCER_SFX_BASE_ADDR;
                    RIA.step0 = 1;
                    RIA.rw0 = frequency & 0xFF;
                    RIA.rw0 = (frequency >> 8) & 0xFF;
                    // has the saucer moved off screen? if so, terminate
                    if (((Saucer.x < 48 - 16) && (Saucer.left == 1)) || ((Saucer.left == 0) && (Saucer.x > 319 - 48)))
                    {
                        // TERMINATE SAUCER AND SET TIMER FOR AUTO RESPAWN
                        Saucer.exists = false;                      // reset "exists" flag
                        Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME;
                        Saucer.spawn_enable = false;
                        // pause SFX
                        RIA.addr0 = SAUCER_SFX_BASE_ADDR + PAN_GATE;
                        RIA.rw0 = 0;
                    }
                }
                // ####  SPAWN NEW SAUCER  ####
                if (!Saucer.exists && Saucer.spawn_enable)
                {
                    Saucer.exists = true;
                    Saucer.spawn_enable = false;
                    Saucer.left = Player[active_player].bullets_fired & 0x01;
                    if (Saucer.left == 1)
                        Saucer.x = 319 - 48; // start off screen on the selected side
                    else
                        Saucer.x = 47 - 16;
                } // END SAUCER TERMINATE/MOVE/SPAWN

                // PERF - Seventh Position POST SAUCER MOVE/SPAWN/TERMINATE
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // #########################################################
                // ######   ALIEN  MOVE/ANIMATION & MATRIX UPDATES   #######
                // #########################################################
                if (!((alien_hit == 1) || Gunner.hit || Gunner.blown_up || level_completed))
                {
                    alien_num++;
                    if (alien_num > 54)
                    {
                        alien_num = 0;
                        alien_roll_over = 1;
                    }
                    // loop while alien does NOT exist, exit loop with alien_num = # of next existing alien and handle roll-over
                    while (Players_Alien_Exists[active_player][alien_num] == 0)
                    {
                        alien_num++;
                        // roll-over alien num, find unoccupied 1st/last row/col, check x boundary collision and set drop flag,
                        // .. continue search for next existing alien
                        if (alien_num > 54)
                        { // handle 'end/start of matrix" calculations
                            alien_num = 0;
                            alien_roll_over = 1;
                        } // END OF ROLLOVER PROCESSING
                    } // END OF WHILE LOOP TO FIND NEXT EXISTING ALIEN - alien_num is now valid

                    // Bullet Collision Handler will count the number of terminations in each col (up to 5),
                    //      so when count = 5 it means column is UNOCCUPIED
                    // This is used for boundary crossing detection and bullet/alien macro CD bounding box
                    // each pass thru here, uses the results of the last pass (i.e. no redundant rechecking)
                    if (alien_roll_over == 1)
                    {
                        s++;
                        if (alien_march_sfx_start < 3)
                            alien_march_sfx_start++;
                        // reset flags
                        alien_roll_over = 0;
                        alien_drop = 0;
                        // find left and right edge (x) of matrix
                        // check to see if 1st column is empty, i.e. 5 terminated aliens in the column
                        while (Alien_unoccupied_rows_per_col[active_player][Player[active_player].alien_1st_col] == 5)
                        {
                            Player[active_player].alien_1st_col++; // if so, update 1st column #
                            Player[active_player].alien_1st_col_rel_x += 16;
                        } // ditto for last col
                        while (Alien_unoccupied_rows_per_col[active_player][Player[active_player].alien_last_col] == 5)
                        {
                            Player[active_player].alien_last_col--;
                            Player[active_player].alien_last_col_rel_x -= 16;
                        }
                        alien_1st_col_abs_x = alien_ref_x + Player[active_player].alien_1st_col_rel_x;
                        alien_last_col_abs_x = alien_ref_x + Player[active_player].alien_last_col_rel_x;

                        // Check for left/right edge boundary crossing
                        if (alien_1st_col_abs_x < INVR_MTRX_LIMIT_LX || alien_last_col_abs_x > INVR_MTRX_LIMIT_RX)
                        {
                            alien_drop = 1;                                                             // if either, drop the matrix by 8px and reverse direction
                            Player[active_player].alien_x_incr = -(Player[active_player].alien_x_incr); // reverse direction
                            alien_ref_y += 8;                                                           // drop alien 8 px
                        }
                        alien_anim = 1 - alien_anim; // alternate animation
                        alien_ref_x += Player[active_player].alien_x_incr;
                        if (Player[active_player].num_of_aliens == 1)
                        {
                            if (Player[active_player].alien_x_incr > 0)
                            {
                                alien_ref_x += 1; // speed is +3 to right, -2 to the left, when there's one alien left
                            }
                            else if (alien_drop == 1)
                            {                     // should be heading left
                                alien_ref_x -= 1; // the first step to the left must get alien back to the other side of the boundary, so -3 (once)
                            }
                        }
                        alien_ref_update = !alien_ref_update; // toggle flag to track which aliens are up to date compared to the reference x/y
                    }
                    // Now that we have the next (existing) alien #, calc its position and image
                    alien_x = alien_ref_x + (int16_t)Alien_rel_x[alien_num];
                    alien_y = alien_ref_y + Alien_rel_y[alien_num];
                    Alien_update[alien_num] = alien_ref_update;

                    // ALIEN COLLSION WITH GROUND -- "ALIEN HAS LANDED - GAME OVER"
                    // ##########################################
                    // If an alien touches the ground, game over, explode gunner, but let bombs/bullets/saucers/collisions/scoring continue until normal termination
                    // pseudo code
                    //      check for any alien with y > XYZ
                    //      set flag indicating collision that will be used to trigger an orderly shutdown
                    //      including stopping motion, animations, etc., clearing display/printing game over screen
                    //      cylcing back to new game section
                    if (alien_y > 200)
                        alien_landed = true;
                } // END ALIEN CODE

                // PERF - Eighth Position POST ALIEN HANDLER MOVE/SPAWN/ANIMATE/MANAGE MATRIX
                // values[values_position++] = VIA_irq_count;
                // values[values_position++] = *via_count_h;
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // ######################################################################
                // ###########     ALIEN COLLISION WITH BUNKER DETECTION     ############
                // ######################################################################
                // When an alien to bunker collision occurs, erase top y lines of bunkr where y is the
                // ... overlap between alien and bunkr top

                // PERF - perf measurement
                // VIA_irq_count = 0;
                //*via_count_l = 0xFF;
                //*via_count_h = 0xFF;

                // Macro CD y-axis
                if ((Alien_rel_y[alien_num] + alien_ref_y + 4) >= (BUNKR_Y + 8) &&
                    (Alien_rel_y[alien_num] + alien_ref_y + 4) <= (BUNKR_Y + 16) && !bypass_test_mode)
                {
                    // Macro test x-axis where do we have overlap, if any?
                    delta_x = (Alien_rel_x[alien_num] + alien_ref_x + Alien_bbox_x1[alien_num]) - BUNKR_ZERO_X0;
                    // Macro CD x-axis -- at least partially inside left edge of 1st bunker and right edge of last bunker
                    // Last Bunker x1 + alien width, 3*45 + 22 + Alien_width[alien_num] (45 = width bunker+gap)
                    if ((delta_x > 0) && (delta_x < 45 + 45 + 45 + 22 + Alien_width[alien_num]))
                    {
                        // x-axis binary search for overlap with bunker, if any
                        bunkr_num = 4; // default value indicates no alignment with any bunker
                        if (delta_x > 45 + 45)
                        { // left or right side of (approx) midpoint of the 4 bunkers?
                            if (delta_x > 45 + 45 + 45)
                            {
                                bunkr_num = 3;
                                bunkr_img_base_addr = (1 - active_player) * BUNKR_3_PLYR1_IMG_BUF + (active_player * BUNKR_3_PLYR2_IMG_BUF);
                                delta_x -= 45 + 45 + 45;
                            }
                            else if (delta_x < 45 + 45 + 22 + Alien_width[alien_num])
                            {
                                bunkr_num = 2;
                                bunkr_img_base_addr = (1 - active_player) * BUNKR_2_PLYR1_IMG_BUF + (active_player * BUNKR_2_PLYR2_IMG_BUF);
                                delta_x -= 45 + 45;
                            }
                        }
                        else
                        {
                            if (delta_x > 45 && delta_x < 45 + 22 + Alien_width[alien_num])
                            {
                                bunkr_num = 1;
                                bunkr_img_base_addr = (1 - active_player) * BUNKR_1_PLYR1_IMG_BUF + (active_player * BUNKR_1_PLYR2_IMG_BUF);
                                delta_x -= 45;
                            }
                            else if (delta_x < 22 + Alien_width[alien_num])
                            {
                                bunkr_num = 0;
                                bunkr_img_base_addr = (1 - active_player) * BUNKR_0_PLYR1_IMG_BUF + (active_player * BUNKR_0_PLYR2_IMG_BUF);
                            }
                        }
                        // common code -- NOTE delta_x is updated to reflect position of colliding bunker
                        if (bunkr_num < 4)
                        {
                            if (delta_x <= Alien_width[alien_num])
                            {
                                bunkr_start_addr = bunkr_img_base_addr + (8 * 64) + 10; // image offset is 5 px, 2 bytes/px, clear 2nd byte of 2 bytes/px
                                bunkr_num_col = delta_x;                                // # col = overlap
                            }
                            else if (delta_x > 22)
                            {
                                bunkr_start_addr = bunkr_img_base_addr + (8 * 64) + (2 * (delta_x - Alien_width[alien_num] + 5)); // -Alien_width[alien_num] to get left edge of alien, +5 for memory offset
                                bunkr_num_col = Alien_width[alien_num] - (delta_x - 22);
                            }
                            else
                            {
                                bunkr_start_addr = bunkr_img_base_addr + (8 * 64) + (2 * (delta_x - Alien_width[alien_num] + 5)); // -Alien_width[alien_num] to get left edge of alien, +5 for memory offset
                                bunkr_num_col = Alien_width[alien_num];
                            }
                            lower_half_bunkr = 0;
                            if ((Alien_rel_y[alien_num] + alien_ref_y + 4) >= BUNKR_Y + 16)
                                lower_half_bunkr = 1;
                            erase_top_of_bunkr2(bunkr_start_addr, bunkr_num_col, lower_half_bunkr);
                        }
                    }
                }

                // PERF - perf measurement of erase function
                // Ninth (LAST) Position POST ALIEN BUNKER COLLISION DETECT/HANDLER
                values[values_position++] = VIA_irq_count;
                values[values_position++] = *via_count_h;
                VIA_irq_count = 0;
                *via_count_l = 0xFF;
                *via_count_h = 0xFF;
                // values[0] = VIA_irq_count;
                // values[1] = *via_count_h;

                // ###############################################################################
                // ####### LAST BIT OF CODE IN PLAY LOOP for debug/performance measurement  ######
                // ###############################################################################

                // #################
                // ###   DEBUG   ###
                // #################

                // DEBUG
                loop_count_A++; // count # of iterations of code inside PLAY loop
                play_loop_time = 0;

                // ####################
                // ###  PEFORMANCE  ###
                // ####################

                /*
                // PERF - display results
                // Perf Measurement
                r = 0;
                for (q = 0; q < 2; q += 2) {
                    time_ticks[r] = values[q]*256 + (255-values[q+1]);
                    play_loop_time += time_ticks[r];
                    if (play_loop_time > play_loop_time_max) {
                        play_loop_time_max = play_loop_time;
                    }
                    //play_loop_average += play_loop_average
                    //if (time_ticks[r] > 5){
                    //}
                    //printf("#%d #%d i=%d, i+1=%d\n", loop_count_A, q, values[q+10], values[q+11]);
                    printf("#%d p#%d  total = %d max = %d\n", loop_count_A, q, play_loop_time, play_loop_time_max);
                    //if (play_loop_time_max > 210) wait();
                    //if (time_ticks[r] > 55) {
                    //wait();
                    // printf("%#d #p%d ticks%d tot%d max%d\n", loop_count_A, q, time_ticks[r], play_loop_time, play_loop_time_max);
                    //if (round_is_over) wait();
                    if (loop_count_A > 0 && loop_count_A < 65000) {
                        if (time_ticks[1] > 25) {
                            wait();
                        }
                    }
                    //if (loop_count_A > 1140)
                    //if (time_ticks > 10)
                    values[q] = 0;
                    values[q+1] = 255;
                    r++;
                //wait();
                */

                // PERF - MORE
                /* printf("max duration=%d @ loop#=%d\n", max_duration, loop_count_at_max_duration);
                for (values_position = 0; values_position < 8; values_position++) {
                    printf("section#%d max duration = %d\n", values_position, max_duration_values[values_position]);
                */

            } // END OF PLAY LOOP
            loop_count_B++;
            if (Game.restart)
            {
                break;
            }
        } // END OF CONTROL LOOP
        loop_count_C++;
    } // END OF BOOT LOOP
    loop_count_D++;
} // END OF MAIN

// #####################################################
// ##############    END OF CODE     ###################
// #####################################################
