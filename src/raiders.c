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
//  0x0000 - 0x01FF -- Blank (512)
//  0x0200 - 0x997F -- Sprite IMAGES (37K)
//  0x9980 - 0xA17F -- Font TABLE (2K)
//  0xB000 - 0xBE0F -- On screen 40x30 CHAR ARRAY (40x30 x3 bytes per char = 3600)
//  0xBE10 - 0xBFFF -- Blank (496)
//  0xC000 - 0xC24F -- Sprite Config Array (74 sprites x 8 bytes per sprite = 592)
//  0xFF00 - 0xFF0F -- Character mode config array
//  0xFF10 - 0xFF3F -- Direct keyboard key state array
//  0xFF40 - 0xFF7F -- Sound FX 8 ch by 8 byte data array
//  0xFF80 = 0xFFA8 -- Direct gamepads

// ###########   KNOWN ISSUES LIST   ###########
// final alien is not exploding
// occasionally a raider will be hit, but remain on the screen, in a stationary position, until the next round of play

// ####################################################################################################
// #################################    ARRAY DECLARATIONS    #########################################
// ####################################################################################################

// GAME vars
struct
{
    bool new_game;                 // triggers start of a new game
    bool new_round;                // triggers start of new round of play
    bool level_done;               // successful completion of current level
    unsigned char level_num;       // level # starting at 1
    unsigned char num_players;     // 0 = 1 player, 1 = 2 players
    unsigned hi_score;             // highest score achieved to date (need to store on media to survive power cycle)
    bool over;                     // GAME OVER flag to trigger end of game actions
    bool play_mode;                // Indicates normal game play
    bool restart;                  // reboot the game, probably using the reset command after acknowledging on screen receipt of the command
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
    unsigned char lives;          // # lives remaining, initially 3, current lives +1 if bonus, 0 when active player's game is over
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

unsigned char players_alien_exists[2][NUM_ALIEN_SPR];
unsigned char alien_width[NUM_ALIEN_SPR];     // Array to hold Width Constants for each specific alien
unsigned int alien_img_ptr[2][NUM_ALIEN_SPR]; // Array to hold 2 image pointers for all 55 aliens

struct
{
    GunnerState state;
    unsigned char explosion_ticks;
    bool sfx; // do gunner explosion sound fx
    unsigned char spawn_time;
    unsigned x;
    unsigned y;
    unsigned char anim_frame; // Gunner = 0, explosion anim = 1 or 2 (they alternate)
    bool direction_right;     // for game play (not demo)
    bool direction_left;
    unsigned loaded; // gun is armed/loaded
    bool shoot;
} Gunner;

struct
{
    unsigned char exists;
    unsigned char explosion_ticks;
    bool sfx;                  // one shot sound fx for bullet
    unsigned char reload_time; // after gunner (re)spawn, time to (re)spawn bullet
    unsigned char reload;      // signal to spawn new bullet
    bool spawn_enable;
    unsigned x;
    unsigned y;
    unsigned anim_frame; // image # 0 = bullet image, 1 = bullet explosion image
    unsigned x_base;     // starting x,y position (could probably define a constant)
    unsigned y_base;
    unsigned x_path; // retains x position once bullet leaves the barrel
} Bullet;

struct
{
    unsigned char type; // 0 = screw type, 1 = nail/spike, 2 = sawtooth
    bool exists;        // spawned, inflight or exploding
    unsigned char hit;  // 0=no collision, 1=alien, 2=bullet, 3=macro_bunker, 4=micro_bunker, 5=gunner, 6=ground
    unsigned char x_aligned_bunker;
    unsigned char bunker_macro_hit;
    unsigned bunker_mem_row_addr; // bunker erosion row/col image memory address
    unsigned bunker_mem_col_addr;
    bool explosion_started;        // false = not started, true = started
    unsigned char explosion_ticks; // starting timer value, which decrements each tick
    unsigned char spawn_time;      // time after round starts before bombs can start dropping
    unsigned steps_taken;          // min time since last bomb, before a new bomb can spawn
    unsigned char anim_frame;
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
    unsigned char score_start_time;
    bool moving_left;
    bool exists;
    bool spawn_enable; // disable spawn while gunner is exploding and until next round
    unsigned next_spawn_time;
} Saucer;

struct
{
    int x;
    unsigned image_ptr;
} saucer_score;

// SOUND FX
// 8 channels, 1 structure per channel, 8 bytes per structure, last byte of structure is unused
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

// Matrix Reference Alien's y-axis starting position table (index increments at level completion)
static const unsigned char mtrx_y_start[9] = {116, 140, 156, 164, 164, 164, 172, 172, 172};

// SAUCER SCORING TABLE - index into this table is number of shots fired by gunner
// these are all divided by 10, LSD is always zero
static const unsigned saucer_score_table[15] = {10, 5, 5, 10, 15, 10, 10, 5, 30, 10, 10, 10, 5, 15, 10};

// COLUMN SEQUENCE TABLE for BOMB DROP
// This is the "random" table of column #s, used to sequence columns of sawtooth and spike bombs, screw bombs drop
// ... from the column the player is underneath
// table index is 0 to 20, sawtooth bombs index starts 6 and goes to 20, spike starts at 0 and goes to 15
static const unsigned char bomb_column_sequ[21] = {0, 6, 0, 0, 0, 3, 10, 0, 5, 2, 0, 0, 10, 8, 1, 7, 1, 10, 3, 6, 9};

// ALIEN MARCH SFX RATE TABLES
// 2 tables, 1 defines the threshold of the # of aliens remaining that is used to change MARCH SFX pulse rate
//      the other table defines the pulse rate when the # of aliens is greater than the current threshold
static const unsigned char alien_march_sfx_threshold[16] =
    {0x32, 0x2B, 0x24, 0x1C, 0x16, 0x11, 0x0D, 0x0A, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
static const unsigned char alien_march_sfx_rate[16] =
    {0x34, 0x2E, 0x27, 0x22, 0x1C, 0x18, 0x15, 0x13, 0x10, 0x0E, 0x0D, 0x0C, 0x0B, 0x09, 0x07, 0x05};

// string declarations, with init values
char score_value_str[] = "0000";
char level_num_str[] = "1  ";
char lives_num_str[] = "3";

// GAME VARS
unsigned char active_player;
bool round_is_over, another_player;

// SFX VARS
unsigned frequency, bullet_freq;
unsigned char loops, bullet_loops;
unsigned char wave, duty;
unsigned char atten_attack, attack_time, attack_volume_atten;
unsigned char atten_decay, decay_time, decay_volume_atten;
unsigned char wave_release, release_time;
bool ramp_up;
unsigned char toggle_tones;
unsigned char alien_march_note_sequ;
unsigned char alien_march_sfx_start;

// ####################################################################################################
// ###############    SHARED VARS (used by multiple extracted functions)    ############################
// ####################################################################################################

// Vsync tracking
int v;
FILE *fptr;
bool demo_terminated;
unsigned ptr;
bool skip_alien_sprite_update;
unsigned bunker_img_ptr;

// Gunner/Saucer display
unsigned gunner_image_ptr;
bool gunner_demo_direction_right;
unsigned saucer_expl_score_image_ptr;

// Bullet state (shared by bullet_move_spawn, bullet_collision_detect, update_sprites, etc.)
int bullet_x, bullet_y, bullet_x0, bullet_y0, bullet_y1;
unsigned bullet_y_base, bullet_x_path, bullet_image_ptr;

// Bullet collision flags (shared by collision detect + termination)
unsigned char bullet_hit;
unsigned char bullet_hit_subset1, bullet_hit_subset2;
unsigned char bullet_bomb_hit;
unsigned char bullet_micro_bunker_hit;
unsigned char bullet_saucer_hit;
unsigned char bullet_boundary_hit;

// Bullet/Bomb overlap (shared by bullet_collision_detect + bomb_collision_detect)
int shot_overlap_top, shot_overlap_bottom, shot_column;
int bomb_top_row, bomb_rows;
unsigned bomb_image_mem;

// Bomb state (shared by bomb_move, bomb_move_spawn_all, bomb_collision_detect, etc.)
unsigned bomb_image_ptr;
unsigned char bomb_type_counter, bomb_type_selector;
unsigned char screw_bomb_cooldown;
unsigned char bomb_speed;
unsigned char bomb_reload_rate;
unsigned char active_bomb_idx;
bool bomb_micro_bunker_hit;
unsigned int bomb_img_start_addr;

// Bunker collision (shared by find_bunker_for_x, bullet/bomb/alien collision)
unsigned bunker_img_base_addr;
unsigned bunker_start_addr;
unsigned char bunker_num;

// Common collision vars (shared by multiple collision functions)
int delta_x, delta_y;
unsigned char delta_rel_x;

// Timing (shared by play_loop, handle_keyboard, object_termination)
unsigned char current_time;

// Unified input state (written by poll_input, read by handle_keyboard, splash_and_input, delay_with_input)
struct
{
    bool move_left;
    bool move_right;
    bool shoot;
    bool pause_toggle;  // true for one frame on rising edge of P
    bool restart;       // true for one frame on rising edge of R
    unsigned char coin; // 0=none, 1=1P, 2=2P
} Input;

bool skip;
bool paused;

// Misc shared
unsigned char dummy_read;
unsigned temp1, temp2;

// Print speed constants
static const bool slow = true;
static const bool blink = true;

// ####################################################################################################
// ##################################### -= FUNCTIONS =- ##############################################
// ####################################################################################################

// Gamepad XRAM layout: 10 bytes per pad, 4 pads max (40 bytes total)
// Byte 0: DPAD (bits 0-3 = up/down/left/right, bit 7 = connected)
// Byte 1: STICKS (bits 0-3 = lstick u/d/l/r, bits 4-7 = rstick u/d/l/r)
// Byte 2: BTN0 (bit 0=A, 1=B, 2=C, 3=X, 4=Y, 5=Z, 6=L1, 7=R1)
// Byte 3: BTN1 (bit 0=L2, 1=R2, 2=Select, 3=Start, 4=Home, 5=L3, 6=R3)
// Bytes 4-9: analog sticks and triggers
#define GAMEPAD_SIZE 10         // bytes per gamepad in XRAM
#define GAMEPAD_CONNECTED 0x80  // byte 0 bit 7
#define GAMEPAD_BTN_START 0x08  // byte 3 bit 3
#define GAMEPAD_BTN_SELECT 0x04 // byte 3 bit 2

// Single per-frame input polling function. Reads all keyboard and gamepad state,
// sets Input struct flags for consumers. Edge-detects pause and restart keys.
void poll_input(void)
{
    static bool prev_p, prev_r;
    bool cur_p, cur_r;
    unsigned char gp_btn1;
    int bits;
    int i;

    Input.move_left = false;
    Input.move_right = false;
    Input.shoot = false;
    Input.pause_toggle = false;
    Input.restart = false;
    Input.coin = 0;

    // Read keyboard state (channel 1 to avoid clobbering channel 0)
    RIA.addr1 = KEYBOARD_INPUT;
    RIA.step1 = 1;
    for (i = 0; i < KEYBOARD_BYTES; i++)
        keystates[i] = RIA.rw1;

    // Read active player's gamepad (channel 1 — must not touch channel 0,
    // which may be mid-write during print_string slow mode)
    {
        unsigned gp_base = GAMEPAD_INPUT + (active_player * GAMEPAD_SIZE);
        RIA.step1 = 1;
        RIA.addr1 = gp_base;
        // byte 0 = dpad, byte 1 = sticks: merge for direction
        bits = RIA.rw1 | RIA.rw1;
        if ((bits & 0x4) && !(bits & 0x8))
            Input.move_left = true;
        if ((bits & 0x8) && !(bits & 0x4))
            Input.move_right = true;
        // byte 2 = BTN0: A(0), B(1), X(3), Y(4) for fire
        if (RIA.rw1 & 0x1B)
            Input.shoot = true;
    }

    // Keyboard directional and fire
    if (!(keystates[0] & 1)) // any key pressed?
    {
        if (key(KEY_LEFT) && !key(KEY_RIGHT))
            Input.move_left = true;
        if (key(KEY_RIGHT) && !key(KEY_LEFT))
            Input.move_right = true;
        if (key(KEY_UP) || key(KEY_DOWN) || key(KEY_SPACE))
            Input.shoot = true;
    }

    // Edge-detect pause (P) and restart (R)
    cur_p = key(KEY_P) ? true : false;
    cur_r = key(KEY_R) ? true : false;
    if (cur_p && !prev_p)
        Input.pause_toggle = true;
    if (cur_r && !prev_r)
        Input.restart = true;
    prev_p = cur_p;
    prev_r = cur_r;

    // Coin detection (only when not in active play mode)
    if (!Game.play_mode)
    {
        // Gamepad 0 start/select → 1P
        RIA.addr1 = GAMEPAD_INPUT + 3;
        RIA.step1 = 0;
        gp_btn1 = RIA.rw1;
        if (gp_btn1 & (GAMEPAD_BTN_START | GAMEPAD_BTN_SELECT))
        {
            Input.coin = 1;
            return;
        }
        // Gamepad 1 start/select → 2P
        RIA.addr1 = GAMEPAD_INPUT + GAMEPAD_SIZE + 3;
        gp_btn1 = RIA.rw1;
        if (gp_btn1 & (GAMEPAD_BTN_START | GAMEPAD_BTN_SELECT))
        {
            Input.coin = 2;
            return;
        }
        // Keyboard 1/2
        if (key(KEY_1))
            Input.coin = 1;
        else if (key(KEY_2))
            Input.coin = 2;
    }
}

// Act on a coin insert: set demo_terminated and game config.
// Only responds to 1 (1P) or 2 (2P), ignores 0 (timeout) and 3 (other key).
bool act_on_coin(unsigned char coin)
{
    if (coin != 1 && coin != 2)
        return false;
    demo_terminated = true;
    Game.num_players = (coin == 2) ? 1 : 0;
    Game.play_mode = true;
    return true;
}

// Wait for N vsync ticks, polling input each tick.
// Returns: 0=timeout, 1=1P coin, 2=2P coin
unsigned char delay_with_input(unsigned ticks)
{
    unsigned di;
    static uint8_t dv;
    dv = RIA.vsync;
    for (di = 0; di < ticks; di++)
    {
        while (dv == RIA.vsync)
            ;
        dv = RIA.vsync;
        poll_input();
        if (Input.coin)
            return Input.coin;
    }
    return 0;
}

// Delay that acts on coin-insert by setting demo_terminated.
// Returns: true if coin was inserted (only outside active gameplay)
bool coin_delay(unsigned ticks)
{
    return act_on_coin(delay_with_input(ticks));
}

// clear entire character screen
// each char position has 3 bytes --> the character, fg color, bg color
// use the space character for initialization and bg color is black (transparent)
static void initialize_char_screen()
{
    int i;
    RIA.addr0 = CHAR_SCREEN_BASE;
    RIA.step0 = 1;
    for (i = 0; i < CHARS_PER_ROW * CHAR_ROWS; i++)
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
        else if (i < 40)
            RIA.rw0 = 12; // blue "SCORE..." etc
        else
            RIA.rw0 = 15; // white
        RIA.rw0 = 0;      // bg color = black
    }
}

static void clear_char_screen(unsigned first_row, unsigned last_row)
{
    unsigned i;
    RIA.addr0 = CHAR_SCREEN_BASE + (first_row * CHARS_PER_ROW * BYTES_PER_CHAR);
    RIA.step0 = BYTES_PER_CHAR;
    for (i = 0; i < CHARS_PER_ROW * (last_row - first_row + 1); i++)
    {
        RIA.rw0 = ' ';
    }
}

// print a string, starting at character row/col, fast or slow print based on 'slow' flag
unsigned char print_string(unsigned row, unsigned col, char *str, bool slow)
{ // assumes no color change (text is white on black)
    bool skip = false;
    RIA.addr0 = CHAR_SCREEN_BASE + (((row * CHARS_PER_ROW) + col) * BYTES_PER_CHAR);
    RIA.step0 = BYTES_PER_CHAR;
    while (*str)
    {
        RIA.rw0 = *str++;
        if (slow)
        {
            skip = delay_with_input(6);
            if (skip)
                return 1;
        }
    }
    return 0;
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
void erase_top_of_bunker(unsigned mem_start, unsigned char num_col, unsigned char lower_half)
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

// BLAST HOLE IN BUNKER at location of bullet or bomb explosion
void erase_bunker_explos(unsigned mem_start, unsigned explos_img_base, unsigned char num_cols)
{
    unsigned char i = 0, temp1;
    RIA.addr1 = mem_start; // template image starting address
    RIA.addr0 = explos_img_base;
    while (i < num_cols)
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

unsigned char bunker_bullet_micro_collision(unsigned mem_start)
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

unsigned char bunker_bomb_micro_collision(unsigned bunker_mem_start, unsigned bomb_mem_start)
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

// BULLET to BOMB Pixel level collision detection
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
    RIA.addr0 = BUNKER_0_PLYR1_IMG_BUF; // point to first bunker (0) image buffer for the current player
    if (active_player == 1)
        RIA.addr0 = BUNKER_0_PLYR2_IMG_BUF;
    for (j = 0; j < 4; j++)
    {
        RIA.addr1 = BUNKER_PRISTINE_IMG_BASE; // start of bunker template memory buffer
        for (i = 0; i < 2048; i++)
        {
            temp = RIA.rw1;
            RIA.rw0 = temp;
        }
    }
}

void update_numerical_lives(bool blink_lives, unsigned char num_of_lives_remaining)
{
    unsigned char blink_cycles;
    unsigned char col = (active_player == 1) ? 34 : 5;
    strcpy(lives_num_str, " ");
    lives_num_str[0] = (char)num_of_lives_remaining + '0';
    if (blink_lives)
    {
        for (blink_cycles = 0; blink_cycles < 10; blink_cycles++)
        {
            print_string(29, col, " ", false);
            if (delay_with_input(10))
                return;
            print_string(29, col, lives_num_str, false);
            if (delay_with_input(10))
                return;
        }
    }
    else
    {
        print_string(29, col, lives_num_str, false);
    }
}

void update_wave_number(bool print_at_top)
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
    if (print_at_top)
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
    if ((Player[active_player].bonus_achieved < 1) && (Player[active_player].score > BONUS_SCORE_THRESHOLD))
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

// Required by ezpsg library but song playback is unused
void ezpsg_instruments(const uint8_t **data)
{
    (void)data;
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

// GAME LOGIC VARS
bool inflight_complete;
bool level_completed;

// ALIEN HANDLER VARS
unsigned char alien_num;
bool alien_update[NUM_ALIEN_SPR]; // updated (or not) state for each alien
bool alien_ref_update;            // toggles on each pass to distinguish which aliens match the current vs prior reference
int alien_x, alien_y;
unsigned char alien_hit; // flag indicating collision
unsigned char alien_x_hit;
unsigned char alien_y_hit;
unsigned char hit_alien_idx; // index # for alien that has been hit by bullet
unsigned char alien_explosion_ticks;
bool alien_explosion_done;
unsigned char alien_index_wrapped;
unsigned char alien_drop;
bool alien_landed;
signed char alien_y_incr;                 // +0 or +8 (used to drop the alien matrix 8 px
signed alien_ref_x;                       // lower left reference pos of the alien matrix, uses byte arithm until sprite pos is updated
signed alien_ref_y;                       // ditto
unsigned char alien_rel_x[NUM_ALIEN_SPR]; // x/y position relative to ref x/y position
signed char alien_rel_y[NUM_ALIEN_SPR];
unsigned char alien_bbox_x0[NUM_ALIEN_SPR];
unsigned char alien_bbox_x1[NUM_ALIEN_SPR];
unsigned char alien_bbox_y0[NUM_ALIEN_SPR];
unsigned char alien_bbox_y1[NUM_ALIEN_SPR];
unsigned char alien_row_num; // alien matrix vars
unsigned char alien_col_num;
unsigned char alien_col_bomb_0;                                   // col # selected for dropping bomb 0 (targeted bmmb)
unsigned char alien_num_to_drop_bomb_from;                        // col # selected for dropping bomb 1 and 2 (per column table)
unsigned char alien_unoccupied_rows_per_col[2][NUM_INVADER_COLS]; // for each of the 11 cols, # of unoccupied rows is between 0 and 5, 5 = not occupied
unsigned char alien_unoccupied_cols_per_row[2][NUM_INVADER_ROWS]; // for each of 5 rows, # of unoccupied cols is between 0 and 11, 11 = not occupied
int alien_1st_col_abs_x;                                          // absolute screen x/y position
int alien_last_col_abs_x;
unsigned char alien_anim; // animation sequ #
bool alien_explosion_sfx_enable;
bool alien_march_sfx_enable;
unsigned char alien_march_sfx_timer;

// ####################################################################################################
// ########################    DRY HELPER FUNCTIONS    ################################################
// ####################################################################################################

// Bunker image base address lookup tables
static const unsigned bunker_plyr1_img[4] = {
    BUNKER_0_PLYR1_IMG_BUF, BUNKER_1_PLYR1_IMG_BUF,
    BUNKER_2_PLYR1_IMG_BUF, BUNKER_3_PLYR1_IMG_BUF};
static const unsigned bunker_plyr2_img[4] = {
    BUNKER_0_PLYR2_IMG_BUF, BUNKER_1_PLYR2_IMG_BUF,
    BUNKER_2_PLYR2_IMG_BUF, BUNKER_3_PLYR2_IMG_BUF};

// Binary search through 4 bunkers to find which one (if any) delta_x falls within.
// Sets bunker_img_base_addr and adjusts delta_x to be relative to the matched bunker.
// right_margin: width of the object (22 for bullet/bomb, 22+alien_width for alien)
// Returns: 0-3 = bunker found, 4 = no bunker
unsigned char find_bunker_for_x(unsigned char right_margin)
{
    unsigned char bn = 4;
    if (delta_x >= 90)
    { // 45+45
        if (delta_x >= 135)
        { // 45+45+45
            bn = 3;
            delta_x -= 135;
        }
        else if (delta_x < 90 + right_margin)
        {
            bn = 2;
            delta_x -= 90;
        }
    }
    else
    {
        if (delta_x >= 45 && delta_x < 45 + right_margin)
        {
            bn = 1;
            delta_x -= 45;
        }
        else if (delta_x < right_margin)
        {
            bn = 0;
        }
    }
    if (bn < 4)
    {
        bunker_img_base_addr = (1 - active_player) * bunker_plyr1_img[bn] + active_player * bunker_plyr2_img[bn];
    }
    return bn;
}

// Hide all sprites off-screen (sprites 4 and 60 use x-axis, others use y-axis)
void hide_all_sprites(void)
{
    unsigned char si;
    for (si = 0; si < TOTAL_NUM_SPR; si++)
    {
        ptr = SPR_CFG_BASE + (si * sizeof(vga_mode4_sprite_t));
        if (si == 4 || si == 60)
        {
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
        }
        else
        {
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
        }
    }
}

// Update lives icons for the active player based on remaining lives count
void update_lives_icons(void)
{
    unsigned char li;
    for (li = 0; li < 4; li++)
    {
        ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + (li + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
        if (li < Player[active_player].lives)
        {
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, LIVES_Y_BASE);
        }
        else
        {
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);
        }
    }
}

// Silence all 8 SFX channels
void silence_all_sfx(void)
{
    unsigned char si;
    RIA.addr0 = SFX_BASE_ADDR + PAN_GATE;
    RIA.step0 = 8;
    for (si = 0; si < 8; si++)
        RIA.rw0 = 0x00;
}

// Load a frequency value to a RIA SFX channel address
void load_freq_to_ria(unsigned addr, unsigned freq)
{
    RIA.addr0 = addr;
    RIA.step0 = 1;
    RIA.rw0 = freq & 0xFF;
    RIA.rw0 = (freq >> 8) & 0xFF;
}

// Set SFX channel gate on (1) or off (0)
void sfx_gate(unsigned channel_addr, unsigned char state)
{
    RIA.addr0 = channel_addr + PAN_GATE;
    RIA.rw0 = state;
}

// Move a bomb downward: update position, bbox, animation, step count
void bomb_move(unsigned char bomb_idx, unsigned char bbox_y0)
{
    Bomb[bomb_idx].y += bomb_speed;
    Bomb[bomb_idx].y0 = Bomb[bomb_idx].y + bbox_y0;
    Bomb[bomb_idx].y1 = Bomb[bomb_idx].y + BOMB_BBOX_Y1;
    Bomb[bomb_idx].steps_taken++;
    Bomb[bomb_idx].anim_frame++;
    if (Bomb[bomb_idx].anim_frame > 3)
        Bomb[bomb_idx].anim_frame = 0;
    active_bomb_idx = bomb_idx;
}

// Find lowest alien in a column and spawn a bomb from it.
// Returns true if bomb was successfully spawned.
bool bomb_spawn_from_column(unsigned char bomb_idx, unsigned char col, int spawn_x, unsigned char bbox_y0)
{
    int i;
    alien_num_to_drop_bomb_from = col;
    if (alien_unoccupied_rows_per_col[active_player][col] >= NUM_INVADER_ROWS)
        return false;
    for (i = 0; i < NUM_INVADER_ROWS; i++)
    {
        if ((players_alien_exists[active_player][alien_num_to_drop_bomb_from] == 0) ||
            ((alien_hit > 0) && (hit_alien_idx == alien_num_to_drop_bomb_from)))
        {
            alien_num_to_drop_bomb_from += NUM_INVADER_COLS;
            Bomb[bomb_idx].drop_rel_y -= 16;
        }
        else
        {
            Bomb[bomb_idx].x = spawn_x;
            Bomb[bomb_idx].y = Bomb[bomb_idx].drop_rel_y;
            Bomb[bomb_idx].x0 = Bomb[bomb_idx].x + BOMB_BBOX_X0;
            Bomb[bomb_idx].x1 = Bomb[bomb_idx].x + BOMB_BBOX_X1;
            Bomb[bomb_idx].y0 = Bomb[bomb_idx].y + bbox_y0;
            Bomb[bomb_idx].y1 = Bomb[bomb_idx].y + BOMB_BBOX_Y1;
            Bomb[bomb_idx].anim_frame = (bomb_idx == 2) ? 3 : 0;
            Bomb[bomb_idx].exists = true;
            active_bomb_idx = bomb_idx;
            Bomb[bomb_idx].steps_taken = 1;
            if (alien_num_to_drop_bomb_from > NUM_INVADER_COLS - 1)
            {
                if ((alien_hit > 0) && ((alien_num_to_drop_bomb_from - NUM_INVADER_COLS) == hit_alien_idx))
                    Bomb[bomb_idx].hit = 1;
            }
            return true;
        }
    }
    return false;
}

// Check if other bombs allow spawning (both must be past reload rate or not started)
bool bombs_allow_spawn(unsigned char exclude1, unsigned char exclude2)
{
    return (Bomb[exclude1].steps_taken == 0 || Bomb[exclude1].steps_taken > bomb_reload_rate) &&
           (Bomb[exclude2].steps_taken == 0 || Bomb[exclude2].steps_taken > bomb_reload_rate);
}

// ####################################################################################################
// #################    PLAY LOOP SUB-FUNCTIONS    ####################################################
// ####################################################################################################

// Forward declarations for play loop functions
static void update_sprites(void);
static void alien_march_sfx(void);
static void handle_keyboard(void);
static void bullet_move_spawn(void);
static void bullet_collision_detect(void);
static void bomb_move_spawn_all(void);
static void bomb_collision_detect(void);
static void terminate_bullet(void);
static void terminate_alien(void);
static void terminate_bombs(void);
static void terminate_gunner(void);
static void object_termination(void);
static void gunner_move_spawn(void);
static void saucer_move_spawn(void);
static void alien_move_animate(void);
static void alien_bunker_collision(void);

// Forward declarations for game phase functions
static void boot_once(void);
static void boot_init(void);
static void splash_and_input(void);
static void game_init(void);
static void play_loop(void);
static void control_loop(void);

// ####################################################################################################
// ##################################### -= MAIN =- ###################################################
// ####################################################################################################
void main()
{
    boot_once();
    while (1)
    {
        boot_init();
        if (!demo_terminated)
            splash_and_input();
        demo_terminated = false;
        game_init();
        if (demo_terminated)
            continue;
        control_loop();
    }
}

// ####################################################################################################
// ###########################    GAME PHASE FUNCTIONS    #############################################
// ####################################################################################################

// One-time initialization: load hiscore from USB drive
static void boot_once(void)
{
    Game.hi_score = 0;
    demo_terminated = false;

    // Load HIGH SCORE from 'hiscore' file on USB Drive
    fptr = fopen("raiders.hiscore", "rb");
    if (fptr)
    {
        fread(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
        fclose(fptr);
    }
}

// Per-restart game variable initialization
static void boot_init(void)
{
    // GAME VAR/ARRAY INITIALIZATION
    Game.restart = false;    // pressing R restarts the game
    level_completed = false; // current LEVEL/WAVE has been completed (all aliens terminated)
    srand(15);               // Initialize random # gen
    active_player = 0;       // 1st player (player 0) always goes first
    skip = false;            // flag to skip splash screens and go straight to demo/play

    // Handle demo termination: cleanup sprites/scores for fresh game start
    // NOTE: do NOT clear demo_terminated here - main() needs it to skip splash_and_input
    if (demo_terminated)
    {
        print_string(2, 7, "0000", !slow);
        if (Game.num_players == 1)
            print_string(2, 27, "0000", !slow);
        clear_char_screen(7, 22);
        hide_all_sprites();
    }
}

// Initialize graphics, show splash screens, get player selection
static void splash_and_input(void)
{
    int i;
    static unsigned char splash_screen_toggle = 0;
    Game.play_mode = false; // press 1 or 2 (players, if no input is received, then demo mode is initiated
    Game.num_players = 1;   // 0 = 1 player, 1 = 2 players

    // #######################################################
    // ##############    INITIALIZE GRAPHICS    ##############
    // #######################################################
    // screen is 320 x 240 pixels screen OR 40 cols by 30 rows characters
    xreg_vga_canvas(1);

    // Config character mode
    xram0_struct_set(0xFF00, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, width_chars, 40);
    xram0_struct_set(0xFF00, vga_mode1_config_t, height_chars, 30);
    xram0_struct_set(CHAR_MODE_CFG, vga_mode1_config_t, xram_data_ptr, CHAR_SCREEN_BASE);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_palette_ptr, 0xFFFF); // using built-in color palette
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_font_ptr, 0x9980);    // address of old school 5x7 font array
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
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, INVADER_IMG_ALIEN_GREEN + (i * 2 * SPR_16X16_SIZE));
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
    }
    // Define SPRITE config structure base address and total # of sprites (length of config structure)
    xreg_vga_mode(4, 0, SPR_CFG_BASE, TOTAL_NUM_SPR); // setup sprite mode in plane 0
    // first clear screen
    initialize_char_screen();
    // Turn on USB keyboard and gamepad I/O (must be before any delay_with_input calls)
    xreg_ria_keyboard(KEYBOARD_INPUT);
    xreg(0, 0, 2, 0xFF80U);
    if (coin_delay(90)) // give the OS a chance to finish before displaying
        return;

    paused = false;

    // ######################################
    // #######   SPLASH SCREENS   ###########
    // load initial splash screen (scoring table), start with common/static elements used on (most) all screens
    print_string(0, 6, "SCORE<1> HI-SCORE SCORE<2>", !slow);
    print_string(2, 7, "0000", !slow);
    print_string(2, 27, "0000", !slow);
    update_score_string(Game.hi_score);
    print_string(2, 17, score_value_str, false);

    // used to toggle between splash screens (scoring table and # players screens) after each demo run
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
            if (coin_delay(70)) // for dramatic effect
                break;
            if (print_string(14, 12, "*SCORING TABLE*", !slow))
                break;
            if (coin_delay(70))
                break;
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

    // If a coin was already inserted during the splash loop, skip the rest
    if (Game.play_mode)
        return;

    // pause for input for a couple seconds, if no input, move on with default = 'demo mode'
    if (coin_delay(180))
        return;

    // Get player selection from keyboard or gamepad
    poll_input();
    if (Input.coin)
        act_on_coin(Input.coin);
    else if (key(KEY_D))
    { // demo mode (default if no input during delay)
        Game.play_mode = false;
        Game.num_players = 1;
    }
}

// Initialize player state, alien arrays, sprites, and all pre-play game state
// Initialize player state arrays, alien existence, and alien position constants
static void init_player_state(void)
{
    int i, j;
    // PLAYER "STATE" ARRAY INITIALIZATION for both players
    //      this is updated at end of a round to allow continuation with next life (if available)
    for (i = 0; i < 2; i++)
    {
        Player[i].exists = 0;
        Player[i].lives = 3;
        Player[i].lives_icons_updated = false;
        Player[i].bonus_achieved = 0; // 0 = not achieved, 1=achieved, 2=achieved but no longer active
        Player[i].bonus_active = false;
        Player[i].bonus_life_incr_done = false;
        Player[i].num_of_aliens = NUM_ALIEN_SPR;
        Player[i].alien_anim = 1;
        Player[i].game_over = false;                  // active player's game is over if true
        Player[i].level = 1;                          // current level being attempted range is 1 to 999
        Player[i].index_start_y_pos = 0;              // selects starting y pos of matrix upper left, rolls over at 8 to 1 (not zero)
        Player[i].alien_ref_x = INVADER_MTRX_START_X; // matrix position & direction when gunner is terminated
        Player[i].alien_ref_y = INVADER_MTRX_B_START_Y;
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
        for (i = 0; i < NUM_ALIEN_SPR; i++)
        {
            players_alien_exists[j][i] = 1;
        }
    }
    // Used to find empty columns in the alien matrix
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < NUM_INVADER_COLS; j++)
        {
            alien_unoccupied_rows_per_col[i][j] = 0; // when terminated bullet handler will add 1 to the occupied cols, when = 5, then col is empty
        }
        for (j = 0; j < NUM_INVADER_ROWS; j++)
        {
            alien_unoccupied_cols_per_row[i][j] = 0;
        }
    }

    // INIT ANIM #... LOAD 2 ALIEN IMAGE POINTERS FOR ALL ALIENS, ONE FOR EACH ANIMATION
    // Lookup: [anim_frame][alien_type] -> image base address
    {
        static const unsigned invr_anim_img[2][3] = {
            {INVADER_IMG_ALIEN_MAGENTA_0, INVADER_IMG_ALIEN_BLUE_0, INVADER_IMG_ALIEN_GREEN_0},
            {INVADER_IMG_ALIEN_MAGENTA_1, INVADER_IMG_ALIEN_BLUE_1, INVADER_IMG_ALIEN_GREEN_1}};
        alien_anim = Player[0].alien_anim;
        for (i = 0; i < 2; i++)
            for (j = 0; j < NUM_ALIEN_SPR; j++)
                alien_img_ptr[i][j] = invr_anim_img[i][(j < 22) ? 0 : (j < 44) ? 1
                                                                               : 2];
    }
}

// Initialize SFX channels and load base parameters
static void init_sfx(void)
{
    xreg(0, 1, 0x00, 0xFFFF); // turn off PSG
    coin_delay(30);
    xreg(0, 1, 0x00, SFX_BASE_ADDR); // initialize PSG... set base address of xregs

    dummy_read = 0;
    toggle_tones = 1;
    loops = 0;
    bullet_loops = 0;
    ramp_up = true;

    silence_all_sfx();

    // Saucer Init ch 2
    wave = 3;         // 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise
    frequency = 2235; // Hz * 3 (745 Hz)
    duty = 128;
    attack_volume_atten = 13;
    attack_time = 4;
    decay_volume_atten = 12;
    decay_time = 8;
    release_time = 8;
    load_SFX_base_parameters(SAUCER_SFX_BASE_ADDR);

    // Bullet Init ch 3
    wave = 4;
    bullet_freq = 5000;
    duty = 128;
    attack_volume_atten = 7;
    attack_time = 2;
    decay_volume_atten = 11;
    decay_time = 8;
    release_time = 4;
    load_SFX_base_parameters(BULLET_SFX_BASE_ADDR);

    // Alien March Init - ch 4
    alien_march_note_sequ = 0;
    wave = 0;
    frequency = 150;
    duty = 160;
    attack_volume_atten = 2;
    attack_time = 3;
    decay_volume_atten = 6;
    decay_time = 5;
    release_time = 4;
    load_SFX_base_parameters(ALIEN_MARCH_SFX_BASE_ADDR);
}

// Initialize all sprite configurations in XRAM
static void init_sprites(void)
{
    int i, j;
    unsigned bunker_x_pos;
    unsigned lives_spr_ptr, lives_x_pos, lives_y_pos;
    unsigned char sprite_number = 0;

    // **** BUNKER SPRITE INIT ****
    for (i = 0; i < NUM_OF_BUNKER_SPR; i++)
    {
        ptr = SPR_CFG_BASE + ((BUNKER_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
        bunker_x_pos = BUNKER_ZERO_X + (i * BUNKER_X_SPACING);
        bunker_img_ptr = BUNKER_0_PLYR1_IMG_BUF + (SPR_32X32_SIZE * i);
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, bunker_x_pos);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, BUNKER_Y);
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bunker_img_ptr);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 5);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
        sprite_number++;
    }
    bunker_img_ptr = BUNKER_0_PLYR1_IMG_BUF;

    // **** SAUCER BIG EXPLOSION SPRITE INIT ****
    ptr = SPR_CFG_BASE + (SAUCER_EXPLOS_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, SAUCER_BASE_Y);
    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, saucer_expl_score_image_ptr);
    xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 5);
    xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
    sprite_number++;

    // **** INVADER SPRITE MATRIX INIT ****
    alien_ref_y = INVADER_MTRX_T_START_Y;
    alien_ref_x = INVADER_MTRX_START_X;
    for (alien_row_num = 5; alien_row_num > 0; alien_row_num--)
    {
        alien_y = alien_ref_y + ((int16_t)INVADER_Y_SPACING * ((int16_t)alien_row_num - 1));
        for (alien_col_num = 0; alien_col_num < NUM_INVADER_COLS; alien_col_num++)
        {
            alien_x = alien_ref_x + (alien_col_num * INVADER_X_SPACING);
            ptr = SPR_CFG_BASE + (sprite_number++ * sizeof(vga_mode4_sprite_t));
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, alien_y);
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, alien_img_ptr[alien_anim][alien_num]);
            xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
            xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
            alien_num++;
        }
        alien_col_num = 0;
    }
    alien_num = NUM_ALIEN_SPR - 1;
    alien_x = alien_ref_x = INVADER_MTRX_START_X;
    alien_y = alien_ref_y = mtrx_y_start[Player[active_player].index_start_y_pos++];
    alien_row_num = 5;
    alien_col_num = NUM_INVADER_COLS;

    // **** SAUCER SPRITE INIT ****
    ptr = SPR_CFG_BASE + (sprite_number++ * sizeof(vga_mode4_sprite_t));
    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, DISAPPEAR_X);
    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, SAUCER_BASE_Y);
    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, SAUCER_IMG_BASE);
    xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
    xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);

    // **** GUNNER SPRITE INIT ****
    Gunner.y = DISAPPEAR_Y;
    gunner_image_ptr = GUNNER_PLYR1_IMG_BASE;
    ptr = SPR_CFG_BASE + ((sprite_number++) * (sizeof(vga_mode4_sprite_t)));
    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Gunner.x);
    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Gunner.y);
    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, gunner_image_ptr);
    xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 4);
    xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);

    // **** LIVES SPRITE INIT ****
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

    // **** BULLET SPRITE INIT ****
    bullet_y_base = GUNNER_Y_BASE + 2;
    bullet_x_path = Gunner.x + 4;
    bullet_x = bullet_x_path;
    bullet_y = bullet_y_base;
    bullet_y = DISAPPEAR_Y;
    bullet_image_ptr = BULLET_IMG_BASE;
    ptr = SPR_CFG_BASE + (BULLET_FIRST_SPR_NUM * sizeof(vga_mode4_sprite_t));
    xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, bullet_x);
    xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, bullet_y);
    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bullet_image_ptr);
    xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 3);
    xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
    sprite_number++;

    // **** BOMB SPRITES INIT ****
    for (i = 0; i < 3; i++)
    {
        ptr = SPR_CFG_BASE + ((BOMB_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
        switch (i)
        {
        case 0:
            bomb_image_ptr = BOMB_SCREW_IMG_BASE + (Bomb[i].anim_frame * SPR_8X8_SIZE);
            break;
        case 1:
            bomb_image_ptr = BOMB_SPIKE_IMG_BASE + (Bomb[i].anim_frame * SPR_8X8_SIZE);
            break;
        case 2:
            bomb_image_ptr = BOMB_SAWTOOTH_IMG_BASE + (Bomb[i].anim_frame * SPR_8X8_SIZE);
            break;
        }
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Bomb[i].x);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Bomb[i].y);
        xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bomb_image_ptr);
        xram0_struct_set(ptr, vga_mode4_sprite_t, log_size, 3);
        xram0_struct_set(ptr, vga_mode4_sprite_t, has_opacity_metadata, false);
        sprite_number++;
    }
}

static void game_init(void)
{
    int i, j;

    if (Game.num_players == 0)
        print_string(2, 27, "    ", !slow);

    init_player_state();

    // GAME VARS
    round_is_over = false;
    another_player = false;
    current_time = 0;
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
    if (coin_delay(120))
        return;
    print_string(6, 14, "          ", !slow);

    // Restore all bunker images to pristine state by copying template bunker to 8 bunker image buffers, 4 for each player
    // ... do this at boot up and when a new level is started for a given player
    restore_bunkers(0); // plyr 1
    restore_bunkers(1); // plyr 2

    // ALIEN ARRAY VARS
    // calc and store RELATIVE POSITION CONSTANTS for each ALIEN MATRIX POSITION (relative to matrix reference)
    // alien_ref_x pos changes every tick, Alien_rel are fixed and relative to reference
    for (i = 0; i < NUM_INVADER_ROWS; i++)
    {
        for (j = 0; j < NUM_INVADER_COLS; j++)
        {
            alien_rel_x[(i * NUM_INVADER_COLS) + j] = j * 16;
            alien_rel_y[(i * NUM_INVADER_COLS) + j] = i * -16;
        }
    }

    // CREATE ARRAY of CONSTANT Bounding Box (BBOX) VALUES and INITIALIZE "UPDATE" FLAG to TRUE
    for (i = 0; i < NUM_ALIEN_SPR; i++)
    {
        alien_update[i] = true;
        alien_bbox_y0[i] = 4;
        alien_bbox_y1[i] = 12;
        if (i < 22)
        {
            alien_bbox_x0[i] = INVADER_MAG_BBOX_X0; // for CD, remainder of (Bullet_x - Alien_ref_x)/16 s/b between x0 and x1, +/-2 based on update flag
            alien_bbox_x1[i] = INVADER_MAG_BBOX_X1;
            alien_width[i] = 12;
        }
        else if (i < 44)
        {
            alien_bbox_x0[i] = INVADER_BLU_BBOX_X0;
            alien_bbox_x1[i] = INVADER_BLU_BBOX_X1;
            alien_width[i] = 11;
        }
        else
        {
            alien_bbox_x0[i] = INVADER_GRN_BBOX_X0;
            alien_bbox_x1[i] = INVADER_GRN_BBOX_X1;
            alien_width[i] = 8;
        }
    }

    // OTHER ALIEN VARS
    alien_x = INVADER_MTRX_START_X;
    alien_y = INVADER_MTRX_B_START_Y;
    alien_hit = 0;
    alien_x_hit = 0;
    alien_y_hit = 0;
    alien_explosion_sfx_enable = false;
    alien_march_sfx_enable = true;
    alien_march_sfx_timer = 100; // prevents premature start of SFX when timer = 0
    alien_explosion_done = false;
    alien_ref_x = INVADER_MTRX_START_X;
    alien_ref_update = true;
    alien_ref_y = INVADER_MTRX_B_START_Y; // initial value changes with each increase in level for several (8?) cycles then repeats
    alien_y_incr = 0;
    alien_1st_col_abs_x = INVADER_MTRX_START_X + 0;
    alien_row_num = 5;
    alien_col_num = NUM_INVADER_COLS;
    hit_alien_idx = 0;
    alien_last_col_abs_x = INVADER_MTRX_START_X + 176;
    alien_col_bomb_0 = NUM_INVADER_COLS; // col from which bomb 0 is dropped, default is no column
    alien_drop = 0;
    alien_landed = false;
    alien_index_wrapped = 0;
    skip_alien_sprite_update = true;

    // GUNNER VARS Init
    Gunner.state = GUNNER_SPAWNING;
    Gunner.explosion_ticks = 0;
    Gunner.sfx = false;
    Gunner.x = LIVES_P1_X_BASE;
    Gunner.direction_right = false; // game mode direction from keybd
    Gunner.direction_left = false;
    Gunner.loaded = 1; // 1 = gun is armed/loaded
    Gunner.shoot = false;
    gunner_demo_direction_right = false;

    // Bullet VAR init
    Bullet.exists = false;
    Bullet.explosion_ticks = BULLET_EXPL_TICKS;

    // Bullet.sfx = false;
    Bullet.reload = 0;
    Bullet.spawn_enable = true;
    Bullet.anim_frame = 0; // image # 0 = bullet image, 1 = explosion
    Bullet.x = Gunner.x + 4;
    Bullet.y = GUNNER_Y_BASE + 2;
    Bullet.x_base = GUNNER_P1_X_BASE + 4; // starting x,y position (
    Bullet.x_path = 0;                    // retains x position once bullet leaves the barrel
    Bullet.y_base = GUNNER_Y_BASE + 2;

    // BOMB INITIALIZATION
    for (i = 0; i < NUM_BOMB_SPR; i++)
    { // i cycles thru bomb types, 0=screw/targeted, 1=spike, 2=sawtooth
        Bomb[i].exists = false;
        // HIT: 0=no collision, 1=alien, 2=bullet, 3=macro_bunker, 4=micro_bunker, 5=gunner, 6=ground
        Bomb[i].hit = 0;
        Bomb[i].bunker_macro_hit = 0;
        Bomb[i].bunker_mem_row_addr = 0; // bunker erosion row/col image memory address
        Bomb[i].bunker_mem_col_addr = 0;
        Bomb[i].explosion_started = false; // not started
        Bomb[i].steps_taken = 0;           // number of y-axis steps taken since spawning occurred
        Bomb[i].x = 0;                     // current pos
        Bomb[i].y = DISAPPEAR_Y;           // all bombs off screen
        Bomb[i].anim_frame = 0;            // Index into animation images 0 - 3 for each bomb, 4 for explosion
        Bomb[i].x0 = BOMB_BBOX_X0;         // add x,y to these to get actual bbox
        Bomb[i].x1 = BOMB_BBOX_X1;
        Bomb[i].y1 = BOMB_BBOX_Y1;
    }
    Bomb[2].anim_frame = 3;
    Bomb[0].y0 = BOMB_BBOX_SCREW_Y0; // add x,y to these to get actual bbox
    Bomb[1].y0 = BOMB_BBOX_SPIKE_Y0;
    Bomb[2].y0 = BOMB_BBOX_SAWTOOTH_Y0;

    // BOMB VARS
    // starting indices for next bomb drop 'column table'
    bomb_type_counter = 2, bomb_type_selector = 2;
    bomb_speed = 4; // initial speed, changes to 5 when # aliens is less than 9
    bomb_reload_rate = BOMB_RELOAD_INITIAL;
    screw_bomb_cooldown = 0; // skip one spawning turn if screw bomb was just terminated
    bomb_img_start_addr = 0;

    // SAUCER VARS
    Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME; // # of ticks b4 next spawn, saucers spawn every 25.6 s from last saucer termination
    Saucer.spawn_enable = false;
    Saucer.score_start_time = 0;
    Saucer.explosion_x = DISAPPEAR_X;
    Saucer.sfx = false;
    saucer_expl_score_image_ptr = SAUCER_MAGENTA_EXPLOS_IMG_BASE;
    Saucer.moving_left = true; // default start from right & move left
    Saucer.exists = false;     // not until spawn occurs during game play

    // COLLISION VARS
    bullet_hit = 0, bullet_hit_subset1 = 0, bullet_hit_subset2 = 0;
    bullet_micro_bunker_hit = 0;
    bullet_bomb_hit = 0;
    bullet_saucer_hit = 0;
    bullet_boundary_hit = 0;

    // Bomb with Bullet and Bullet with Bomb Collisions
    shot_overlap_top = 0;
    shot_overlap_bottom = 0, shot_column = 0;
    bomb_top_row = 0, bomb_rows = 0;
    bomb_image_mem = BOMB_IMG_BASE;

    // bomb with bunker collisions
    bomb_micro_bunker_hit = false;

    // bunker
    bunker_num = 4; // default value used to indicate bunker # has not been identified

    init_sfx();
    init_sprites();

    // Time to first Gunner Spawn and to Bomb Drop Enabled  when "new level"
    Gunner.spawn_time = GUNNER_SPAWN_TICKS;
    Game.bomb_spawn_enable = false; // will be set to true when timer expires
    Game.bomb_spawn_time = BOMB_INITIAL_SPAWN_TICKS;
    Game.new_round = false;

    update_numerical_lives(!blink, Player[active_player].lives);
    print_string(29, 15, "WAVE", !slow);
    Player[active_player].level = 1;
    update_wave_number(false); // false = print at bottom of screen

    temp1 = temp2 = 0;
}

// Control loop: processes round results, handles level completion, player swapping, then runs play loop
static void control_loop(void)
{
    int i, j;
    unsigned char rounds_completed = 0;
    while (1)
    {
        // #### PROCESS RESULTS FROM LAST ROUND, UNLESS IT'S THE FIRST ROUND, IN WHICH CASE...SKIP ALL THIS
        if (round_is_over)
        { // either gunner was hit, alien landed or level was completed
            rounds_completed++;
            // turn off all 8 SFX channels
            silence_all_sfx();
            // reset PSG
            xreg(0, 1, 0x00, 0xFFFF); // turn off PSG
            coin_delay(30);
            xreg(0, 1, 0x00, SFX_BASE_ADDR); // initialize PSG... set base address of xregs

            // check for another player
            another_player = (Game.num_players == 1) && (Player[1 - active_player].exists == 1);
            // ####  'GUNNER WAS HIT' HANDLER  ####
            // ####################################
            if (Gunner.state == GUNNER_BLOWN_UP)
            {
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
                        coin_delay(180);
                        print_string(27, 8, "                      ", !slow);
                        if (demo_terminated)
                            break;
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
                        coin_delay(180);
                        print_string(5, 14, "          ", !slow);
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
            // draw std lives icons based on # of lives and draw bonus icon in next open position based on bonus flag = true
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
                hide_all_sprites();

                // CELEBRATE Level Completion!!
                print_string(6, 15, "PLAYER <", !slow);
                if (active_player == 0)
                    print_string(6, 23, "1>", !slow);
                else
                    print_string(6, 23, "2>", !slow);
                print_string(9, 10, "WAVE     COMPLETED!", !slow);
                update_wave_number(true); // true = print at top of screen, false bottom
                update_wave_number(false);
                if (coin_delay(240))
                    break;
                print_string(6, 5, "                              ", !slow);
                print_string(9, 5, "                              ", !slow);

                // RELOAD PRISTINE BUNKER IMAGES
                // restore aliens and bunkers to pristine state, but do not display... yet, use
                // PLAY LOOP sprite update to load updated sprites to screen
                restore_bunkers(active_player);

                // NEW LEVEL SO RESTORE ALL ALIENS TO HEALTH
                Player[active_player].num_of_aliens = NUM_ALIEN_SPR;
                for (i = 0; i < NUM_ALIEN_SPR; i++)
                {
                    players_alien_exists[active_player][i] = 1;
                }
                alien_anim = 1;
                // update key state save VARS and reinitialize to start the next level/wave
                // increment index into 'starting y pos' table, roll-over after 8
                if (Player[active_player].index_start_y_pos > 8)
                    Player[active_player].index_start_y_pos = 1;
                Player[active_player].alien_ref_y = mtrx_y_start[Player[active_player].index_start_y_pos];
                alien_ref_y = mtrx_y_start[Player[active_player].index_start_y_pos];
                Player[active_player].index_start_y_pos++;
                Player[active_player].level++;
                Player[active_player].alien_ref_x = INVADER_MTRX_START_X;
                alien_ref_x = INVADER_MTRX_START_X;
                Player[active_player].alien_x_incr = 2; // right = +2, left equal -2
                Player[active_player].alien_1st_col = 0;
                Player[active_player].alien_1st_col_rel_x = 0;
                Player[active_player].alien_last_col = 10;
                Player[active_player].alien_last_col_rel_x = 176;

                // Reset count of unoccupied alien columns and rows to zero
                // When an alien is terminated the bullet handler will add 1 to the # of unoccupied cols, when = 5, then col is empty
                for (j = 0; j < NUM_INVADER_COLS; j++)
                    alien_unoccupied_rows_per_col[active_player][j] = 0;
                for (j = 0; j < NUM_INVADER_ROWS; j++)
                    alien_unoccupied_cols_per_row[active_player][j] = 0;
                alien_num = NUM_ALIEN_SPR - 1;
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
                hide_all_sprites();
                // show new player ID at top of screen
                print_string(6, 14, "PLAYER <", slow);
                if (active_player == 0)
                    print_string(6, 22, "1>", slow);
                else
                    print_string(6, 22, "2>", slow);
                update_wave_number(false);
                // update lives ICONs to represent # lives of new player
                update_lives_icons();
                update_numerical_lives(blink, Player[active_player].lives);
                // erase the text displayed above
                print_string(6, 9, "                          ", !slow);

                // change BUNKER SPRITE IMAGE pointers to OTHER PLAYER'S bunker images
                bunker_img_ptr = (1 - active_player) * bunker_plyr1_img[0] + active_player * bunker_plyr2_img[0];
                for (i = 0; i < 4; i++)
                { // switch to the other bank of 4 bunker images
                    ptr = SPR_CFG_BASE + ((BUNKER_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
                    xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, bunker_img_ptr);
                    bunker_img_ptr += SPR_32X32_SIZE;
                }

                // update working copy of ref positions using new player's data
                // change ALIEN SPRITE X/Y POSITIONS to OTHER PLAYER"S prior positions
                alien_ref_x = Player[active_player].alien_ref_x;
                alien_ref_y = Player[active_player].alien_ref_y;
                // ditto for state of animation
                alien_anim = Player[active_player].alien_anim;

                // reinit ALIEN NUM index for ALIEN MATRIX, reinit ALIEN# 0 X/Y POS = REFERENCE VALUES
                alien_num = NUM_ALIEN_SPR - 1;
                alien_x = alien_ref_x;
                alien_y = alien_ref_y;
                skip_alien_sprite_update = true;
            } // ELSE there is NO OTHER PLAYER, so keep current player

            // reappear fresh set of bunker sprites
            for (i = 0; i < NUM_OF_BUNKER_SPR; i++)
            {
                ptr = SPR_CFG_BASE + ((BUNKER_FIRST_SPR_NUM + i) * sizeof(vga_mode4_sprite_t));
                xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, BUNKER_Y);
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

        update_lives_icons();
        if (Player[active_player].bonus_active)
        { // Handle BONUS case
            ptr = SPR_CFG_BASE + (LIVES_FIRST_SPR_NUM + ((Player[active_player].lives - 1) + (4 * active_player))) * (sizeof(vga_mode4_sprite_t));
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, LIVES_BONUS_IMG_BASE);
        }

        // update # waves at bottom of screen
        update_wave_number(false); // false = print at bottom of screen

        // (Re)initialize KEY VARS
        round_is_over = false;
        Gunner.state = GUNNER_SPAWNING;
        inflight_complete = false;
        alien_landed = false;
        Gunner.spawn_time = GUNNER_SPAWN_TICKS;
        Game.bomb_spawn_enable = false;
        Game.bomb_spawn_time = BOMB_INITIAL_SPAWN_TICKS;
        Saucer.exists = false;
        Saucer.spawn_enable = false;
        Saucer.next_spawn_time = SAUCER_SPAWN_TIME;
        alien_index_wrapped = 0;
        alien_hit = 0;

        // SFX VARS
        dummy_read = 0;
        toggle_tones = 1; // put at SFX var initialization, may need unique one for each channel
        loops = 0;
        bullet_loops = 0;
        ramp_up = true;
        alien_march_sfx_enable = true;
        alien_march_sfx_timer = alien_march_sfx_rate[Player[active_player].alien_march_index];
        alien_march_sfx_timer = 100;
        alien_march_sfx_start = 0;

        // #######  GETTING READY TO TRANSITION TO PLAY LOOP  #######
        // grab current value of vsync, wait for VSYNC to increment to v + 1, sync play loop to that event
        v = RIA.vsync;
        // END CONTROL LOOP

        play_loop();
        if (Game.restart)
            break;
    } // END OF CONTROL LOOP
} // END OF CONTROL_LOOP

// ####################################################################################################
// ####################    EXTRACTED PLAY LOOP SUB-FUNCTIONS    ####################################
// ####################################################################################################

static void update_sprites(void)
{
    int i;
    // ###########################################################################################
    // ##########################    UPDATE SPRITE CONFIG DATA    ################################
    // ###########################################################################################

    // ALIEN SPRITE UPDATE
    // *******************
    // IMAGE POINTER HANDLER - alternates between EXPLOSION and ALIEN IMAGE
    // image pointer needs to point to the alien that has been hit
    if (skip_alien_sprite_update == false)
    {
        ptr = SPR_CFG_BASE + ((hit_alien_idx + ALIEN_FIRST_SPR_NUM) * sizeof(vga_mode4_sprite_t));
        // if collision has occurred and explosion is done, disappear alien
        if (alien_explosion_done)
        {                                                                                                         // indicates termination process has been completed/is done
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, DISAPPEAR_Y);                                     // disappear dead alien
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, alien_img_ptr[alien_anim][hit_alien_idx]); // restore alien image for next round
            alien_explosion_done = false;                                                                         // reset flag for next time
        }
        // if hit, do explosion image
        if (alien_hit == 1)
        {
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, ALIEN_EXPL_IMG_BASE);
        }
        // otherwise, just do regular (once per tick) update of sprite pos
        else
        {
            ptr = SPR_CFG_BASE + ((alien_num + ALIEN_FIRST_SPR_NUM) * sizeof(vga_mode4_sprite_t));
            xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, alien_x);
            xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, alien_y);
            xram0_struct_set(ptr, vga_mode4_sprite_t, xram_sprite_ptr, alien_img_ptr[alien_anim][alien_num]);
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
    ptr = SPR_CFG_BASE + ((BOMB_FIRST_SPR_NUM + active_bomb_idx) * sizeof(vga_mode4_sprite_t));
    // only update explos image right after explosion starts
    if (active_bomb_idx < 3)
    {
        if (Bomb[active_bomb_idx].hit > 0)
        {
            bomb_image_ptr = BOMB_EXPL_IMG_BASE;
        }
        else
        {
            bomb_image_ptr = BOMB_IMG_BASE + (((active_bomb_idx * 4) + Bomb[active_bomb_idx].anim_frame) * SPR_8X8_SIZE);
        }
        xram0_struct_set(ptr, vga_mode4_sprite_t, x_pos_px, Bomb[active_bomb_idx].x);
        xram0_struct_set(ptr, vga_mode4_sprite_t, y_pos_px, Bomb[active_bomb_idx].y);
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
}

static void alien_march_sfx(void)
{
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
    else if (alien_march_sfx_enable)
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
            sfx_gate(ALIEN_MARCH_SFX_BASE_ADDR, 0); // push pause
            toggle_tones = 1 - toggle_tones;
            alien_march_note_sequ = 0;
            // reload timer using current index, same value if # of aliens hasn't crossed the next threshold
            alien_march_sfx_timer = alien_march_sfx_rate[Player[active_player].alien_march_index];
        }
        else
        {
            if (toggle_tones == 1)
                frequency += 50;
            alien_march_note_sequ++;
            // load frequ
            load_freq_to_ria(ALIEN_MARCH_SFX_BASE_ADDR, frequency);
            // push play, play the next note in the sequence
            sfx_gate(ALIEN_MARCH_SFX_BASE_ADDR, 1);
            // sequence done, push pause and wait for the trigger to restart sequence
        }
    }
}

static void handle_keyboard(void)
{
    do
    {
        poll_input();

        if (!paused)
        {
            Gunner.direction_left = Input.move_left;
            Gunner.direction_right = Input.move_right;
            if (Input.shoot)
                Gunner.shoot = true;
        }

        if (Input.pause_toggle)
            paused = !paused;
        if (Input.restart)
            Game.restart = true;
        if (Input.coin && act_on_coin(Input.coin))
        {
            Game.restart = true;
            break;
        }
    } while (paused);
}

static void bullet_move_spawn(void)
{
    // #############################################
    // ############   BULLET MOVE/SPAWN   ##########
    // #############################################

    // ####  BULLET SPAWN/RELOAD  ####
    if (Bullet.reload == 1 && Gunner.state == GUNNER_ALIVE)
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
            if (bullet_loops < 5)
            {
                bullet_freq = 5000 - (bullet_loops * 200);
                bullet_loops++;
                // load frequ
                load_freq_to_ria(BULLET_SFX_BASE_ADDR, bullet_freq);
                // PUSH PLAY
                sfx_gate(BULLET_SFX_BASE_ADDR, 0);
            }
            else
            {
                sfx_gate(BULLET_SFX_BASE_ADDR, 1);
            }

            // alternate between frequencies while inflight
            if (bullet_loops > 4)
            {
                if (bullet_loops == 10)
                    bullet_freq = 2000;
                if (bullet_loops == 5)
                    bullet_freq = 1000;
                bullet_loops++;
                if (bullet_loops == 15)
                    bullet_loops = 5;
                // load frequ
                load_freq_to_ria(BULLET_SFX_BASE_ADDR, bullet_freq);
            }
        }
        else
            bullet_loops = 0;
    }
    // END BULLET MOVE/SPAWN
}

static void bullet_collision_detect(void)
{
    int i;
    int bullet_x1;
    unsigned char delta_rel_y;
    // ###########################################################
    // ###########     BULLET COLLISION DETECTION     ############
    // ###########################################################

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
                    shot_overlap_top = bullet_y1 - Bomb[i].y;
                    shot_overlap_bottom = Bomb[i].y1 - bullet_y0;
                    shot_column = 2 + bullet_x0 - Bomb[i].x0;
                    if (shot_overlap_bottom > 0 && shot_overlap_top > 0)
                    { // then bullet overlaps bomb in y axis
                        if (shot_overlap_top < 4)
                        {
                            bomb_top_row = 0;
                            bomb_rows = shot_overlap_top;
                        }
                        else if (shot_overlap_bottom < 4)
                        {
                            bomb_top_row = 8 - shot_overlap_bottom;
                            bomb_rows = shot_overlap_bottom;
                        }
                        else
                        {
                            bomb_top_row = bullet_y0 - Bomb[i].y;
                            bomb_rows = 4;
                        }

                        bomb_image_mem = BOMB_IMG_BASE + (((i * 4) + Bomb[i].anim_frame) * SPR_8X8_SIZE);
                        bullet_bomb_hit = bullet_bomb_micro_collision(bomb_image_mem, bomb_top_row, shot_column, bomb_rows);
                    }
                    if (bullet_bomb_hit == 1)
                    {
                        Bullet.explosion_ticks = BULLET_EXPL_TICKS;
                        bullet_image_ptr = BULLET_EXPL_IMG_BASE;
                    }
                }
            }
        }
    } // END BULLET HITS BOMB COLLISION DETECT

    // RESTORE - This bbox y1 is used for all but BOMB COLLISION DETECTION
    bullet_y1 = bullet_y + 1; // bounding box is just one pixel at the tip of the spear

    // COLLISION BULLET to BUNKER
    // ##########################
    // Macro CD y-axis
    if (bullet_hit == 0)
    {
        delta_y = BUNKER_MACRO_BBOX_Y1 - bullet_y0;
        if ((delta_y > 0) && (delta_y <= 16))
        { // Macro check for y-axis
            // Macro test x-axis where do we have overlap, if any?
            delta_x = bullet_x0 - BUNKER_ZERO_X0;
            // Macro CD x-axis -- at least partially inside left edge of 1st bunker and right edge of last bunker
            // Last Bunker x1 + alien width, 3*45 + 22 + alien_width[alien_num] (45 = width bunker+gap)
            if ((delta_x >= 0) && (delta_x < 45 + 45 + 45 + 22))
            {
                bunker_num = find_bunker_for_x(22);
                if (bunker_num < 4)
                {
                    bunker_start_addr = bunker_img_base_addr + ((24 - delta_y) * 64) + (2 * (delta_x + 5)); // image offset is 5 px, 2 bytes/px, clear 2nd byte of 2 bytes/px
                    if (bunker_bullet_micro_collision(bunker_start_addr) > 0)
                    {
                        bullet_micro_bunker_hit = 1;
                        Bullet.explosion_ticks = BULLET_EXPL_TICKS;
                        bullet_y -= 2; // this is to move explosion up to match the hole being made in bunker (the hole is bullet y - 2)
                        bullet_image_ptr = BULLET_EXPL_IMG_BASE;
                        erase_bunker_explos(bunker_start_addr - (64 * 2) - (2 * 3), BULLET_EXPL_IMG_BASE, 8);
                    }
                }
            }
        }
    } // END BULLET BUNKER CD

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
        if (delta_x >= 0 && delta_x < INVADER_MTRX_WIDTH + 16)
        {
            delta_rel_x = (uint8_t)delta_x & 0x0F; // get the bullet position relative to alien x (which is the remainder after divide by 16)
            alien_col_num = (uint8_t)delta_x >> 4; // divide by 16 to get column number
            hit_alien_idx = (alien_row_num * NUM_INVADER_COLS) + alien_col_num;
            if (players_alien_exists[active_player][hit_alien_idx] == 1)
            {
                if (delta_rel_y < alien_bbox_y1[hit_alien_idx] && (delta_rel_y >= alien_bbox_y0[hit_alien_idx]))
                    alien_y_hit = 1;
                if (alien_update[hit_alien_idx] != alien_ref_update)
                {
                    // hit alien hasn't been updated, if moving right subtract 2, left add 2 to bbox
                    if (Player[active_player].alien_x_incr == +2)
                    { // moving aliens to the right, offset bbox to the left by 2
                        if ((delta_rel_x >= alien_bbox_x0[hit_alien_idx] - 2) && (delta_rel_x < alien_bbox_x1[hit_alien_idx] - 2))
                            alien_x_hit = 1;
                    }
                    else
                    {
                        if ((delta_rel_x >= alien_bbox_x0[hit_alien_idx] + 2) && (delta_rel_x < alien_bbox_x1[hit_alien_idx] + 2))
                            alien_x_hit = 1;
                    }
                }
                else
                { // hit alien has already been updated to match reference pos
                    if ((delta_rel_x >= alien_bbox_x0[hit_alien_idx]) && (delta_rel_x < alien_bbox_x1[hit_alien_idx]))
                    {
                        alien_x_hit = 1;
                    }
                }
                alien_hit = alien_x_hit && alien_y_hit;
                alien_x_hit = 0;
                alien_y_hit = 0;
                if (alien_hit == 1)
                {
                    alien_unoccupied_cols_per_row[active_player][alien_row_num]++; // count number of terminated aliens in each row & column
                    alien_unoccupied_rows_per_col[active_player][alien_col_num]++;
                    bullet_y = DISAPPEAR_Y; // disapper bullet
                    // use explosion done time to delay spawning next bullet until alien explos is over
                    Bullet.explosion_ticks = ALIEN_EXPL_TICKS;
                    alien_explosion_ticks = ALIEN_EXPL_TICKS;
                    // keep score
                    // NOTE - score values are divided by 10 (LSD always zero)
                    if (hit_alien_idx < 21)
                        Player[active_player].score += 1;
                    else if (hit_alien_idx < 43)
                        Player[active_player].score += 2;
                    else
                        Player[active_player].score += 3;
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
    } // END BULLET TO ALIEN  COLLISION DETECT

    // COLLISION BULLET to SAUCER
    // ##########################
    if (Saucer.score_start_time > 0)
        Saucer.score_start_time--;
    if (Saucer.score_start_time == 1)
    { // since it's a one time event, need to use value = 1 (not 0)
        saucer_expl_score_image_ptr = saucer_score.image_ptr;
    }
    if (bullet_hit == 0 && Saucer.exists)
    {
        if (AABB_OVERLAP(
                bullet_x0, bullet_y, bullet_x1, bullet_y1,
                Saucer.x + SAUCER_BBOX_X0, SAUCER_BASE_Y + SAUCER_BBOX_Y0,
                Saucer.x + SAUCER_BBOX_X1, SAUCER_BASE_Y + SAUCER_BBOX_Y1))
        {
            bullet_saucer_hit = 1;
            Saucer.sfx = true;
            Bullet.explosion_ticks = SAUCER_EXPL_TICKS;
            Saucer.score_start_time = SAUCER_EXPL_TICKS / 2;
            Saucer.explosion_x = Saucer.x - 8;
            Saucer.exists = false;
            bullet_y = DISAPPEAR_Y;
            Saucer.next_spawn_time = 0;
            Saucer.spawn_enable = false;
            Player[active_player].score += saucer_score_table[Player[active_player].bullets_fired];
            switch (saucer_score_table[Player[active_player].bullets_fired])
            {
            case 5:
                saucer_score.image_ptr = SAUCER_SCORE50_IMG_BASE;
                break;
            case 10:
                saucer_score.image_ptr = SAUCER_SCORE100_IMG_BASE;
                break;
            case 15:
                saucer_score.image_ptr = SAUCER_SCORE150_IMG_BASE;
                break;
            case 30:
                saucer_score.image_ptr = SAUCER_SCORE300_IMG_BASE;
                break;
            }
        }
    } // END BULLET TO SAUCER COLLISION DETECT

    // COLLISION BULLET to UPPER BNDRY
    // ###############################
    if ((bullet_y < TOP_BOUNDARY) && (bullet_boundary_hit == 0))
    {
        bullet_boundary_hit = 1;
        Bullet.explosion_ticks = BULLET_EXPL_TICKS;
        bullet_image_ptr = BULLET_EXPL_IMG_BASE; // Boundary explosion address
    }
    // END BULLET COLLISION DETECT/HANDLER
}

static void bomb_move_spawn_all(void)
{
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
    active_bomb_idx = 3; // default, indicates none have been updated
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
                bomb_move(0, BOMB_BBOX_SCREW_Y0);
        }
        else
        { // bomb doesn't exist so SPAWN one
            // ###  SPAWN  ###
            // Define screw sprite pos, image/anim sequ, update steps, inidcate existence
            if (screw_bomb_cooldown == 0)
            { // don't skip the first cycle, but every other cycle after that
                if (bombs_allow_spawn(1, 2))
                {
                    if (Game.bomb_spawn_enable && (Player[active_player].num_of_aliens > 0) && Gunner.state <= GUNNER_EXPLODING)
                    {
                        // spawn from the column the gunner is under
                        alien_col_num = NUM_INVADER_COLS;           // default is no column (which is flagged by col = 11)
                        delta_rel_x = (Gunner.x + 8) - alien_ref_x; // center of gunner minus left edge of matrix
                        if (delta_rel_x > 0)
                            alien_col_num = delta_rel_x >> 4;
                        if (alien_col_num < NUM_INVADER_COLS)
                        {
                            Bomb[0].drop_rel_y = alien_ref_y + 21; // +17 to start drop 5 px below bottom edge of lowest alien
                            bomb_spawn_from_column(0, alien_col_num, alien_ref_x + (delta_rel_x & 0xF0) + 5, BOMB_BBOX_SCREW_Y0);
                        }
                    }
                }
            }
            else
                screw_bomb_cooldown = 0; // this is set to 1 when screw bomb terminates to prevent immediately respawn
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
                bomb_move(1, BOMB_BBOX_SPIKE_Y0);
        }
        // ### SPAWN ###
        else
        {
            if (bombs_allow_spawn(0, 2))
            {
                // bomb 1 drops are disabled when one alien remains
                if (Game.bomb_spawn_enable && (Player[active_player].num_of_aliens > 1) && Gunner.state <= GUNNER_EXPLODING)
                {
                    alien_col_num = bomb_column_sequ[Player[active_player].col_index_spike++];
                    if (Player[active_player].col_index_spike > 14)
                        Player[active_player].col_index_spike = 0;
                    Bomb[1].drop_rel_y = alien_ref_y + 20; // +16 to start drop 4 px below bottom edge of lowest alien
                    bomb_spawn_from_column(1, alien_col_num, alien_ref_x + (alien_col_num * 16) + 5, BOMB_BBOX_SPIKE_Y0);
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
                bomb_move(2, BOMB_BBOX_SAWTOOTH_Y0);
        }
        else
        {
            if (bombs_allow_spawn(0, 1))
            {
                if (Game.bomb_spawn_enable && Gunner.state <= GUNNER_EXPLODING)
                {
                    alien_col_num = bomb_column_sequ[Player[active_player].col_index_sawtooth];
                    if (++Player[active_player].col_index_sawtooth > 15)
                        Player[active_player].col_index_sawtooth = 0;
                    Bomb[2].drop_rel_y = alien_ref_y + 21; // +17 to start drop 5 px below bottom edge of lowest alien
                    bomb_spawn_from_column(2, alien_col_num, alien_ref_x + (alien_col_num * 16) + 5, BOMB_BBOX_SAWTOOTH_Y0);
                }
            }
        }
    } // END SAWTOOTH BOMB 2 MOVE SPAWN
}

static void bomb_collision_detect(void)
{
    unsigned bunker_start_addr1, bunker_start_addr2;
    // #########################################################
    // ###########     BOMB COLLISION DETECTION     ############
    // #########################################################
    // NOTE; need to know if Gunner is hit b4 executing handlers, so we can shutdown alien movement, but let inflight objects terminate naturually
    // 0 = no collision, 1 = alien, 2 = bullet, 3 = macro_bunker, 4 = micro_bunker, 5 = ground, 6 = gunner
    //      all but gunner collisions trigger a bomb explosion image
    //      the explosion duration for actual explosions is the same all that have one
    //      but the duration for a gunner collisions matches the gunner explosion duration

    // ########################################
    // BOMB hits BULLET (NOT>>> bullet to bomb)
    // ########################################
    // hit = 2 for contact with bullet (but not the other way around)
    if (bullet_x0 >= Bomb[active_bomb_idx].x0 && bullet_x0 < Bomb[active_bomb_idx].x1)
    {
        bullet_y1 = bullet_y + 4;
        shot_overlap_top = bullet_y1 - Bomb[active_bomb_idx].y;
        shot_overlap_bottom = Bomb[active_bomb_idx].y1 - bullet_y0;
        shot_column = 2 + bullet_x0 - Bomb[active_bomb_idx].x0;
        if (shot_overlap_bottom > 0 && shot_overlap_bottom < 13 && shot_overlap_top > 0 && shot_overlap_top < 13)
        {
            if (shot_overlap_top < 4)
            {
                bomb_top_row = 0;
                bomb_rows = shot_overlap_top;
            }
            else if (shot_overlap_bottom < 4)
            {
                bomb_top_row = 8 - shot_overlap_bottom;
                bomb_rows = shot_overlap_bottom;
            }
            else
            {
                bomb_top_row = bullet_y0 - Bomb[active_bomb_idx].y;
                bomb_rows = 4;
            }
            bomb_image_mem = BOMB_IMG_BASE + (((active_bomb_idx * 4) + Bomb[active_bomb_idx].anim_frame) * SPR_8X8_SIZE);
            Bomb[active_bomb_idx].hit = 2 * bullet_bomb_micro_collision(bomb_image_mem, bomb_top_row, shot_column, bomb_rows);
        }
        if (Bomb[active_bomb_idx].hit == 2)
        {
            Bomb[active_bomb_idx].explosion_ticks = BOMB_EXPL_TICKS;
        }
    }

    // RESTORE this var so it can be used later in another COLLISION DETECTION algo
    bullet_y1 = bullet_y + 1;

    // ###################################
    // COLLISION BOMB to EXPLODING ALIEN
    // ###################################
    // hit = 1 for contact with "alien explosion" in progress
    //      The detection occurs on prior tick in the move/spawn section, but acted on here (1 tick later)
    if (Bomb[active_bomb_idx].hit == 1)
    {
        Bomb[active_bomb_idx].explosion_ticks = BOMB_EXPL_TICKS;
    }

    // ##########################
    // COLLISION BOMB to BUNKER
    // ##########################
    // Macro CD y-axis
    if (Bomb[active_bomb_idx].hit != 4)
    {
        delta_y = Bomb[active_bomb_idx].y - BUNKER_Y;
        if ((delta_y > 0) && (delta_y <= 16 + 4))
        { // Macro check for y-axis, using +4 to allow hit when 1/2 of bomb is non-overlapping at bottom of bunker
            // Macro test x-axis where do we have overlap, if any?
            delta_x = Bomb[active_bomb_idx].x0 - BUNKER_ZERO_X;
            // Macro CD x-axis -- need at least 1 px overlap of BOMB on left and right edges of bunker
            // Less than last Bunker x1 and greater than num px's to Bomb X1 (3)...45 = width bunker+gap, 27 = distance from bunker x to bunker x1
            if ((delta_x > 2) && (delta_x < 45 + 45 + 45 + 27))
            {
                bunker_num = find_bunker_for_x(27);
                if (bunker_num < 4)
                {
                    // bomb image offset = (bomb# * 4 + anim#) * sprite_size + 2px byte offset
                    bomb_img_start_addr = BOMB_IMG_BASE +
                                          ((active_bomb_idx * 4 + Bomb[active_bomb_idx].anim_frame) * SPR_8X8_SIZE) + (2 * 2);
                    // define position of explosion and hole that will be made in bunker image
                    bunker_start_addr1 = bunker_img_base_addr + (delta_y * 64) + (2 * delta_x);
                    if (bunker_bomb_micro_collision(bunker_start_addr1, bomb_img_start_addr) > 0)
                    {
                        // snap delta_y up to the next multiple of 4
                        {
                            unsigned char snapped = ((delta_y / 4) + 1) * 4;
                            Bomb[active_bomb_idx].y = BUNKER_Y + snapped;
                            bunker_start_addr2 = snapped * 64;
                        }
                        bomb_micro_bunker_hit = true;
                        Bomb[active_bomb_idx].hit = 4;
                        Bomb[active_bomb_idx].explosion_ticks = BOMB_EXPL_TICKS;
                        // image pointer is handled in SPRITE UPDATE code
                        // DON"T TOUCH
                        bunker_start_addr2 = bunker_img_base_addr + bunker_start_addr2 + (2 * delta_x) - 4;
                        erase_bunker_explos(bunker_start_addr2, BOMB_EXPL_IMG_BASE, 6);
                    }
                }
            }
        }
    } // END BOMB BUNKER CD

    // ##################
    // BOMB hits GROUND
    // ##################
    // hit = 5 for ground, show bomb explosion
    if (Bomb[active_bomb_idx].y >= 224)
    {
        Bomb[active_bomb_idx].hit = 5;
        Bomb[active_bomb_idx].y = 224;
        Bomb[active_bomb_idx].explosion_ticks = BOMB_EXPL_TICKS;
    }

    // ####################################################
    // BOMB hits GUNNER  OR  ALIEN LANDS on the ground
    // ####################################################
    // hit = 6 for gunner hit, alien_landed = true for alien landed, DON'T SHOW bomb explosion for either
    // action taken are the same except, if collision wiht BOMB, BOMB is disappeared
    if (Gunner.state != GUNNER_EXPLODING)
    {
        if (AABB_OVERLAP(
                Bomb[active_bomb_idx].x0, Bomb[active_bomb_idx].y0,
                Bomb[active_bomb_idx].x1, Bomb[active_bomb_idx].y1,
                Gunner.x + GUNNER_BBOX_X0, GUNNER_Y_BASE + GUNNER_BBOX_Y0,
                Gunner.x + GUNNER_BBOX_X1, GUNNER_Y_BASE + GUNNER_BBOX_Y1))
        {
            Bomb[active_bomb_idx].hit = 6;
            Bomb[active_bomb_idx].y = DISAPPEAR_Y;
        }
        if ((Bomb[active_bomb_idx].hit == 6) || alien_landed)
        {
            Gunner.state = GUNNER_EXPLODING;
            if (Player[active_player].lives > 0)
                Player[active_player].lives--;
            update_numerical_lives(!blink, Player[active_player].lives);
            Gunner.explosion_ticks = GUNNER_EXPL_TICKS;
            Bomb[active_bomb_idx].exists = false;
            Bomb[active_bomb_idx].hit = 0;
            Game.bomb_spawn_enable = false;
            Game.bomb_spawn_time = 0;
            Saucer.spawn_enable = false;
            // clear spawn timer to make it inactive and immediately disasble spawning
            Saucer.next_spawn_time = 0;
            Bullet.spawn_enable = false;
            // initialize SFX
            sfx_gate(GUNNER_SFX_BASE_ADDR, 1); // Turn on FX
            // turn off alien march SFX
            alien_march_sfx_enable = false;
        }
    } // END BOMB to GUNNER and ALIEN to GROUND COLLISION DETECTION
}

static void terminate_bullet(void)
{
    bullet_hit_subset1 = bullet_boundary_hit + bullet_micro_bunker_hit + bullet_bomb_hit;
    bullet_hit_subset2 = bullet_saucer_hit + alien_hit;
    bullet_hit = bullet_hit_subset1 + bullet_hit_subset2; // flag any collision with a bullet
    if (Bullet.explosion_ticks > 0)
        Bullet.explosion_ticks--;
    if (bullet_hit > 0)
    {
        if (bullet_hit_subset2 > 0)
        {
            if (Player[active_player].score > BOMB_RATE_SCORE_4)
                bomb_reload_rate = BOMB_RELOAD_FASTEST;
            else if (Player[active_player].score > BOMB_RATE_SCORE_3)
                bomb_reload_rate = BOMB_RELOAD_FAST;
            else if (Player[active_player].score > BOMB_RATE_SCORE_2)
                bomb_reload_rate = BOMB_RELOAD_MEDIUM;
            else if (Player[active_player].score > BOMB_RATE_SCORE_1)
                bomb_reload_rate = BOMB_RELOAD_SLOW;
            else
                bomb_reload_rate = BOMB_RELOAD_INITIAL;
        }
        if (Bullet.explosion_ticks == 0)
        {
            if (bullet_hit_subset2 > 0)
                update_score_board();
            // turn off SFX
            if (alien_hit == 1)
            {
                sfx_gate(BULLET_SFX_BASE_ADDR, 0);
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
                sfx_gate(SAUCER_SFX_BASE_ADDR, 0);
            }
            // clear flags
            bullet_hit = 0;
            bullet_bomb_hit = 0;
            bullet_micro_bunker_hit = 0;
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
            if (alien_explosion_sfx_enable)
            {
                // transition to explosion SFX - load new bullet SFX parameters for explosion
                // PUSH PLAY
                sfx_gate(BULLET_SFX_BASE_ADDR, 1);
                bullet_freq = 1500 - (bullet_loops * 50);
                load_freq_to_ria(BULLET_SFX_BASE_ADDR, bullet_freq);
                bullet_loops++;
            }
            else
            {
                sfx_gate(BULLET_SFX_BASE_ADDR, 0);
            }
            if (bullet_saucer_hit == 1)
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
                load_freq_to_ria(SAUCER_SFX_BASE_ADDR, frequency);
            }
            else
            {
                sfx_gate(SAUCER_SFX_BASE_ADDR, 0);
            }
        }
    }
}

static void terminate_alien(void)
{
    if (alien_explosion_ticks > 0)
        alien_explosion_ticks--;
    if (alien_hit == 1)
    {
        // IF time expired, alien explosion done, terminate alien, adjust # of aliens, set/reset flags
        if (alien_explosion_ticks == 0)
        {
            alien_explosion_done = true;                            // flag transition to completion
            alien_hit = 0;                                          // clear collision flag to terminate explosion cycle
            players_alien_exists[active_player][hit_alien_idx] = 0; // terminate alien
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
                silence_all_sfx();
            }
            // update ALIEN MARCH SFX rate based on # aliens remaining
            else if (alien_march_sfx_threshold[Player[active_player].alien_march_index] > Player[active_player].num_of_aliens)
            {
                // move index to next faster pulse rate, note... actual rate is not updated until current cycle is over
                Player[active_player].alien_march_index++;
            }
        }
        else
        {
            alien_explosion_sfx_enable = true;
        }
    }
}

static void terminate_bombs(void)
{
    int i;
    for (i = 0; i < 3; i++)
    {
        if (Bomb[i].explosion_ticks > 0)
            Bomb[i].explosion_ticks--;
        if ((Bomb[i].hit > 0) && (Bomb[i].explosion_ticks == 0))
        { // bomb explosion is done, terminate
            // must wait one cycle after screw bomb terminates before enabling respawn
            if (i == 0)
                screw_bomb_cooldown = 1;
            // clean up/reset flags/vars for termination
            Bomb[i].hit = 0;
            Bomb[i].exists = false;
            Bomb[i].steps_taken = 0;
            Bomb[i].y = DISAPPEAR_Y;
        }
    }
}

static void terminate_gunner(void)
{
    if (Gunner.explosion_ticks > 0)
        Gunner.explosion_ticks--;
    if (Gunner.state == GUNNER_EXPLODING)
    {
        if (Gunner.explosion_ticks != 0)
        {
            // do explosion animation for active player's gunner, alternate between images
            gunner_image_ptr = GUNNER_IMG_FOR_PLAYER(active_player) + SPR_16X16_SIZE;
            if (current_time % 16 > 8)
            {
                gunner_image_ptr += SPR_16X16_SIZE;
            }
            // do explosion SFX
            RIA.addr0 = GUNNER_SFX_BASE_ADDR;
            RIA.step0 = 1;
            Sfx[GUNNER_CHAN].freq = ((rand() % (0x2A0 - 0x40 + 1)) + 0x40);
            RIA.rw0 = Sfx[GUNNER_CHAN].freq & 0xFF;
            RIA.rw0 = (Sfx[GUNNER_CHAN].freq >> 8) & 0xFF;
            if (Gunner.explosion_ticks > GUNNER_EXPL_TICKS - 5)
                RIA.rw0 = ((uint8_t)((rand() % (0xF0 - 0x08 + 1)) + 0x08) & 0xFF);
            else
                dummy_read = RIA.rw0;
            dummy_read = RIA.rw0;
            RIA.rw0 = (GUNNER_EXPL_TICKS - Gunner.explosion_ticks) * 2;
        }
        else
        {
            sfx_gate(GUNNER_SFX_BASE_ADDR, 0);
            Gunner.state = GUNNER_BLOWN_UP;
            Gunner.y = DISAPPEAR_Y;
            gunner_image_ptr = GUNNER_IMG_FOR_PLAYER(active_player);
        }
    }
}

static void object_termination(void)
{
    terminate_bullet();
    terminate_alien();
    terminate_bombs();
    terminate_gunner();
}

static void gunner_move_spawn(void)
{
    // ############   GUNNER MOVE/SPAWN   ############
    // ###############################################

    // ####  MOVE  ####
    // ################
    if (Gunner.state == GUNNER_ALIVE || Gunner.state == GUNNER_EXPLODING)
    {
        if (Gunner.state == GUNNER_ALIVE)
        {
            // #####   DEMO MODE AI   #####
            Gunner.y = GUNNER_Y_BASE;
            // this is the movement/firing algo for demo mode
            if (!Game.play_mode)
            {
                if (rand() % 96 == 0)
                    gunner_demo_direction_right = !gunner_demo_direction_right;
                // if GUNNER is out of bounds, put back in bounds and change direction
                if (Gunner.x > GUNNER_DEMO_MAX_X)
                {
                    gunner_demo_direction_right = false;
                    Gunner.x = GUNNER_DEMO_MAX_X;
                }
                if (Gunner.x < GUNNER_MIN_X)
                {
                    gunner_demo_direction_right = true;
                    Gunner.x = GUNNER_MIN_X;
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
                if (Gunner.direction_right && Gunner.x <= GUNNER_MAX_X)
                    Gunner.x += GUNNER_SPEED;
                if (Gunner.x > GUNNER_MAX_X)
                {
                    Gunner.x = GUNNER_MAX_X;
                }
                if (Gunner.direction_left && Gunner.x >= GUNNER_MIN_X)
                    Gunner.x -= GUNNER_SPEED;
                if (Gunner.x < GUNNER_MIN_X)
                {
                    Gunner.x = GUNNER_MIN_X;
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
            gunner_image_ptr = GUNNER_IMG_FOR_PLAYER(active_player);
            Bullet.reload = 1;
            Gunner.state = GUNNER_ALIVE;
            Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // s/b SAUCER_SPAWN_TIME;
            Saucer.spawn_enable = false;
        }
    } // END GUNNER MOVE/SPAWN HANDLER
}

static void saucer_move_spawn(void)
{
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
    if (Saucer.exists && !level_completed)
    {
        if (Saucer.moving_left)
            Saucer.x -= SAUCER_SPEED; // move saucer one px left or right depending on direction flag
        else
            Saucer.x += SAUCER_SPEED;
        // do SFX
        sfx_gate(SAUCER_SFX_BASE_ADDR, 1); // PUSH PLAY
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
        load_freq_to_ria(SAUCER_SFX_BASE_ADDR, frequency);
        // has the saucer moved off screen? if so, terminate
        if (((Saucer.x < 48 - 16) && Saucer.moving_left) || (!Saucer.moving_left && (Saucer.x > 319 - 48)))
        {
            // TERMINATE SAUCER AND SET TIMER FOR AUTO RESPAWN
            Saucer.exists = false;                      // reset "exists" flag
            Saucer.next_spawn_time = SAUCER_SPAWN_TIME; // SAUCER_SPAWN_TIME;
            Saucer.spawn_enable = false;
            // pause SFX
            sfx_gate(SAUCER_SFX_BASE_ADDR, 0);
        }
    }
    // ####  SPAWN NEW SAUCER  ####
    if (!Saucer.exists && Saucer.spawn_enable)
    {
        Saucer.exists = true;
        Saucer.spawn_enable = false;
        Saucer.moving_left = (Player[active_player].bullets_fired & 0x01) != 0;
        if (Saucer.moving_left)
            Saucer.x = 319 - 48; // start off screen on the selected side
        else
            Saucer.x = 47 - 16;
    } // END SAUCER TERMINATE/MOVE/SPAWN
}

static void alien_move_animate(void)
{
    // #########################################################
    // ######   ALIEN  MOVE/ANIMATION & MATRIX UPDATES   #######
    // #########################################################
    if (!((alien_hit == 1) || Gunner.state == GUNNER_EXPLODING || Gunner.state == GUNNER_BLOWN_UP || level_completed))
    {
        alien_num++;
        if (alien_num > NUM_ALIEN_SPR - 1)
        {
            alien_num = 0;
            alien_index_wrapped = 1;
        }
        // loop while alien does NOT exist, exit loop with alien_num = # of next existing alien and handle roll-over
        while (players_alien_exists[active_player][alien_num] == 0)
        {
            alien_num++;
            // roll-over alien num, find unoccupied 1st/last row/col, check x boundary collision and set drop flag,
            // .. continue search for next existing alien
            if (alien_num > NUM_ALIEN_SPR - 1)
            { // handle 'end/start of matrix" calculations
                alien_num = 0;
                alien_index_wrapped = 1;
            } // END OF ROLLOVER PROCESSING
        } // END OF WHILE LOOP TO FIND NEXT EXISTING ALIEN - alien_num is now valid

        // Bullet Collision Handler will count the number of terminations in each col (up to 5),
        //      so when count = 5 it means column is UNOCCUPIED
        // This is used for boundary crossing detection and bullet/alien macro CD bounding box
        // each pass thru here, uses the results of the last pass (i.e. no redundant rechecking)
        if (alien_index_wrapped == 1)
        {
            if (alien_march_sfx_start < 3)
                alien_march_sfx_start++;
            // reset flags
            alien_index_wrapped = 0;
            alien_drop = 0;
            // find left and right edge (x) of matrix
            // check to see if 1st column is empty, i.e. 5 terminated aliens in the column
            while (alien_unoccupied_rows_per_col[active_player][Player[active_player].alien_1st_col] == NUM_INVADER_ROWS)
            {
                Player[active_player].alien_1st_col++; // if so, update 1st column #
                Player[active_player].alien_1st_col_rel_x += 16;
            } // ditto for last col
            while (alien_unoccupied_rows_per_col[active_player][Player[active_player].alien_last_col] == NUM_INVADER_ROWS)
            {
                Player[active_player].alien_last_col--;
                Player[active_player].alien_last_col_rel_x -= 16;
            }
            alien_1st_col_abs_x = alien_ref_x + Player[active_player].alien_1st_col_rel_x;
            alien_last_col_abs_x = alien_ref_x + Player[active_player].alien_last_col_rel_x;

            // Check for left/right edge boundary crossing
            if (alien_1st_col_abs_x < INVADER_MTRX_LIMIT_LX || alien_last_col_abs_x > INVADER_MTRX_LIMIT_RX)
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
        alien_x = alien_ref_x + (int16_t)alien_rel_x[alien_num];
        alien_y = alien_ref_y + alien_rel_y[alien_num];
        alien_update[alien_num] = alien_ref_update;

        // ALIEN COLLSION WITH GROUND -- "ALIEN HAS LANDED - GAME OVER"
        // ##########################################
        // If an alien touches the ground, game over, explode gunner, but let bombs/bullets/saucers/collisions/scoring continue until normal termination
        // pseudo code
        //      check for any alien with y > XYZ
        //      set flag indicating collision that will be used to trigger an orderly shutdown
        //      including stopping motion, animations, etc., clearing display/printing game over screen
        //      cycling back to new game section
        if (alien_y > ALIEN_LANDING_Y)
            alien_landed = true;
    } // END ALIEN CODE
}

static void alien_bunker_collision(void)
{
    unsigned char bunker_num_col;
    unsigned char lower_half_bunker;
    // ######################################################################
    // ###########     ALIEN COLLISION WITH BUNKER DETECTION     ############
    // ######################################################################
    // When an alien to bunker collision occurs, erase top y lines of bunker where y is the
    // ... overlap between alien and bunker top

    // Macro CD y-axis
    if ((alien_rel_y[alien_num] + alien_ref_y + 4) >= (BUNKER_Y + 8) &&
        (alien_rel_y[alien_num] + alien_ref_y + 4) <= (BUNKER_Y + 16))
    {
        // Macro test x-axis where do we have overlap, if any?
        delta_x = (alien_rel_x[alien_num] + alien_ref_x + alien_bbox_x1[alien_num]) - BUNKER_ZERO_X0;
        // Macro CD x-axis -- at least partially inside left edge of 1st bunker and right edge of last bunker
        // Last Bunker x1 + alien width, 3*45 + 22 + alien_width[alien_num] (45 = width bunker+gap)
        if ((delta_x > 0) && (delta_x < 45 + 45 + 45 + 22 + alien_width[alien_num]))
        {
            bunker_num = find_bunker_for_x(22 + alien_width[alien_num]);
            if (bunker_num < 4)
            {
                if (delta_x <= alien_width[alien_num])
                {
                    bunker_start_addr = bunker_img_base_addr + (8 * 64) + 10; // image offset is 5 px, 2 bytes/px, clear 2nd byte of 2 bytes/px
                    bunker_num_col = delta_x;                                 // # col = overlap
                }
                else if (delta_x > 22)
                {
                    bunker_start_addr = bunker_img_base_addr + (8 * 64) + (2 * (delta_x - alien_width[alien_num] + 5)); // -alien_width[alien_num] to get left edge of alien, +5 for memory offset
                    bunker_num_col = alien_width[alien_num] - (delta_x - 22);
                }
                else
                {
                    bunker_start_addr = bunker_img_base_addr + (8 * 64) + (2 * (delta_x - alien_width[alien_num] + 5)); // -alien_width[alien_num] to get left edge of alien, +5 for memory offset
                    bunker_num_col = alien_width[alien_num];
                }
                lower_half_bunker = 0;
                if ((alien_rel_y[alien_num] + alien_ref_y + 4) >= BUNKER_Y + 16)
                    lower_half_bunker = 1;
                erase_top_of_bunker(bunker_start_addr, bunker_num_col, lower_half_bunker);
            }
        }
    }
}

// Primary play loop: runs at 60 FPS, handles all game tick logic
static void play_loop(void)
{
    while (1)
    {
        // Wait for VSYNC blanking period
        if (RIA.vsync == v)
            continue;
        v = RIA.vsync;
        current_time++;
        bomb_type_selector = bomb_type_counter;

        update_sprites();
        alien_march_sfx();

        // Check if round is over (gunner terminated or level completed)
        inflight_complete = !Bullet.exists && !Bomb[0].exists && !Bomb[1].exists && !Bomb[2].exists && !alien_hit && Gunner.state != GUNNER_EXPLODING;
        if (Gunner.state == GUNNER_BLOWN_UP && inflight_complete && !Saucer.exists && (bullet_saucer_hit == 0))
        {
            round_is_over = true;
            break;
        }
        if (level_completed && inflight_complete)
        {
            Gunner.y = DISAPPEAR_Y;
            round_is_over = true;
            break;
        }

        handle_keyboard();
        if (Game.restart)
        {
            xreg(0, 1, 0x00, 0xFFFF);
            fptr = fopen("raiders.hiscore", "wb+");
            fwrite(&Game.hi_score, sizeof(Game.hi_score), 1, fptr);
            fclose(fptr);
            break;
        }

        bullet_move_spawn();
        bullet_collision_detect();
        bomb_move_spawn_all();
        bomb_collision_detect();
        object_termination();
        gunner_move_spawn();
        if (Gunner.spawn_time > 0)
            Gunner.spawn_time -= 1;
        saucer_move_spawn();
        alien_move_animate();
        alien_bunker_collision();
    }
}
