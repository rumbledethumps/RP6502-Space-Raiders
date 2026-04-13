
//####################################################################################################
//################################ -= COMPILER DIRECTIVES =- #########################################
//####################################################################################################

// XREG DATA/CONFIG ARRAY ADDRESSES

// CHARACTER MODE
#define CHAR_MODE_CFG 0xFF00U    // location of character mode configuration array

// GAMEPAD
#define GAMEPAD_INPUT 0xFF80U   // 40 bytes of gamepad data

// USB KEYBOARD
#define KEYBOARD_INPUT 0xFF10U   // KEYBOARD_BYTES (32 bytes, 256 bits) of key press bitmask data
// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
uint8_t keystates[KEYBOARD_BYTES] = {0};

// SOUND FX CONTROL ARRAY
#define SFX_BASE_ADDR 0xFF40U       // location of 8 sound FX control arrays, 8 channels, 8 bytes per channel
// Decoder ring for the index into the array of structures
//      ch 0 = gunner, 1 = alien, 2 = saucer, 3 = bullet, 4 = alien advance
#define GUNNER_CHAN 0           // 1 sound, gunner explosion
#define ALIEN_CHAN 1          // not needed, see bullet
#define SAUCER_CHAN 2           // 2 sounds, saucer exists, saucer explosion
#define BULLET_CHAN 3           // 3 sounds, bullet firing, bullet in flight, alien explosion
#define ALIEN_MARCH_CHAN 4    // 1 sound, alien march

#define GUNNER_SFX_BASE_ADDR        SFX_BASE_ADDR + (GUNNER_CHAN * 8)
#define ALIEN_SFX_BASE_ADDR         SFX_BASE_ADDR + (ALIEN_CHAN * 8)
#define SAUCER_SFX_BASE_ADDR        SFX_BASE_ADDR + (SAUCER_CHAN * 8)
#define BULLET_SFX_BASE_ADDR        SFX_BASE_ADDR + (BULLET_CHAN * 8)
#define ALIEN_MARCH_SFX_BASE_ADDR   SFX_BASE_ADDR + (ALIEN_MARCH_CHAN * 8)

// SFX Controls (offset from channel base address)
#define FREQ_LSB 0
#define FREQ_MSB 1
#define DUTY_CYCLE 2
#define ATTEN_ATTACK 3
#define ATTEN_DECAY 4
#define WAVE_RELEASE 5
#define PAN_GATE 6

// DISPLAY DIMENSIONS, POINTER TO BACKGROUND IMAGE (IF THERE IS ONE)
#define CANVAS_WIDTH 320U
#define CANVAS_HEIGHT 240
#define CANVAS_DATA 0x0000U
#define DISAPPEAR_Y 240
#define DISAPPEAR_X 320U

#define TOP_BOUNDARY 32
#define BOTTOM_BOUNDARY 224

// CHARACTER SCREEN
#define CHAR_SCREEN_BASE 0xB000U
#define CHARS_PER_ROW 40
#define CHAR_ROWS 30
#define BYTES_PER_CHAR 3

// GAMEPLAY TIMING
#define GUNNER_SPAWN_TICKS      64
#define BOMB_INITIAL_SPAWN_TICKS 120
#define BONUS_SCORE_THRESHOLD   150  // score is stored / 10, so this = 1500 points

// BOMB RELOAD RATE (higher = slower spawning)
#define BOMB_RELOAD_FASTEST      7
#define BOMB_RELOAD_FAST         8
#define BOMB_RELOAD_MEDIUM      11
#define BOMB_RELOAD_SLOW        16
#define BOMB_RELOAD_INITIAL     48

// BOMB RELOAD SCORE THRESHOLDS (score / 10)
#define BOMB_RATE_SCORE_4     1229   // fastest
#define BOMB_RATE_SCORE_3      819
#define BOMB_RATE_SCORE_2      409
#define BOMB_RATE_SCORE_1       51   // slowest non-initial

// GUNNER MOVEMENT BOUNDS
#define GUNNER_MIN_X  55
#define GUNNER_MAX_X 249
#define GUNNER_DEMO_MAX_X (320 - 96)

// ALIEN LANDING THRESHOLD
#define ALIEN_LANDING_Y 200

// OBJECT STATE ENUMS
typedef enum {
    GUNNER_SPAWNING,    // spawn timer counting down, not visible
    GUNNER_ALIVE,       // on screen, player-controllable
    GUNNER_EXPLODING,   // hit by bomb, explosion animation playing
    GUNNER_BLOWN_UP     // explosion complete, waiting for round to end
} GunnerState;

typedef enum {
    SAUCER_INACTIVE,    // not spawned, timer may be running
    SAUCER_FLYING,      // on screen, moving
    SAUCER_EXPLODING,   // hit by bullet, showing explosion
    SAUCER_SCORING      // showing score value
} SaucerState;

// COLLISION HELPERS
#define AABB_OVERLAP(a_x0, a_y0, a_x1, a_y1, b_x0, b_y0, b_x1, b_y1) \
    ((a_x0) < (b_x1) && (a_x1) > (b_x0) && (a_y0) < (b_y1) && (a_y1) > (b_y0))

// SPRITE CONFIG BASE ADDR AND IMAGE BASE ADDR... and SPRITE IMAGE BUFFER SIZES
#define SPR_CFG_BASE 0xC000U     // Base address of sprite config array
#define SPR_IMG_BASE 0x0200U     // Base address of sprite image array, # images not necessarily equal to number of sprites
#define SPR_32X32_SIZE 0x0800U   // Buffer size for each sprite size
#define SPR_16X16_SIZE 0X0200U
#define SPR_8X8_SIZE 0x0080U

// ####  BUNKER  ####
// NOTE: each sprite image is eaten away by colors being changed to transparent...
//      based on bomb/bullet blast pattern being AND'd with bunker sprite image (zeroes are transparent, 0xFF no change to color)
//      4 images per player plus one restoration copy, but only 4 sprites for visible Military bunkers, ID # are 0 thru 3
// during game time, the proper set of 4 bunker images is pointed to by the 4 sprite img ptrs, based on active player #
#define NUM_OF_BUNKER_SPR 4
#define BUNKER_FIRST_SPR_NUM 0                   // first sprite config is number 0, last bunker sprite is 3
// IMAGES
#define NUM_OF_BUNKER_IMGS 9
#define BUNKER_IMG_BASE   SPR_IMG_BASE           // base address of Military bunker Image

#define BUNKER_PRISTINE_IMG_BASE   BUNKER_IMG_BASE
#define BUNKER_IMG_SIZE   SPR_32X32_SIZE         // 32 x 32 = 0x0400

#define BUNKER_IMG_BUF   (SPR_32X32_SIZE * NUM_OF_BUNKER_IMGS)       // Total size of bunker image buffer

#define BUNKER_0_PLYR1_IMG_BUF  (BUNKER_IMG_BASE + SPR_32X32_SIZE)
#define BUNKER_1_PLYR1_IMG_BUF  (BUNKER_0_PLYR1_IMG_BUF + SPR_32X32_SIZE)
#define BUNKER_2_PLYR1_IMG_BUF  (BUNKER_1_PLYR1_IMG_BUF + SPR_32X32_SIZE)
#define BUNKER_3_PLYR1_IMG_BUF  (BUNKER_2_PLYR1_IMG_BUF + SPR_32X32_SIZE)

#define BUNKER_0_PLYR2_IMG_BUF  (BUNKER_0_PLYR1_IMG_BUF + (4 * SPR_32X32_SIZE))
#define BUNKER_1_PLYR2_IMG_BUF  (BUNKER_0_PLYR2_IMG_BUF + SPR_32X32_SIZE)
#define BUNKER_2_PLYR2_IMG_BUF  (BUNKER_1_PLYR2_IMG_BUF + SPR_32X32_SIZE)
#define BUNKER_3_PLYR2_IMG_BUF  (BUNKER_2_PLYR2_IMG_BUF + SPR_32X32_SIZE)

// math: bunker image offset is 5, bunker width = 22, space between = 22, 320 is screen width, so (320-4*22-3*22-2*5)/2 = 6

// POSITIONS/BOUNDING BOXES
// DID THE RESEARCH given my layout is 240 tall and original is 256, removed top and bottom blank lines
// ... with this GUNNER and BUNKERS move up 8, bunker to 176
#define BUNKER_Y (192 - 8 - 8)       // -8 due to image offset change from y0 = y + 0 to y + 8
#define BUNKER_WIDTH 22          // width of displayed image
#define BUNKER_FULL_WIDTH 32     // width (# of columns) of sprite image buffer
#define BUNKER_GAP 23            // # px gap between visible bunker images
#define BUNKER_X_SPACING  (BUNKER_WIDTH + BUNKER_GAP)     // 22 (visible) bunker, 23 space between bunkers = 45 left edge to left edge
#define BUNKER_ZERO_X 76         // full sprite x, left side = 76, right 77 since first bunker left edge to last bunker right edge is 157 (odd #)
#define BUNKER_ZERO_X0  (BUNKER_ZERO_X + 5)     // bunker image is offset 5 from left edge of 32x32 sprite
#define BUNKER_ZERO_X1  (BUNKER_ZERO_X + 27)    // width of image = 22, right edge is 22 + 5 from left edge of sprite

#define BUNKER_MACRO_BBOX_Y1      (BUNKER_Y + 24)

// SAUCER EXPLOSION IMAGES
#define NUM_OF_SAUCER_EXPLOS_SPR 1
#define SAUCER_EXPLOS_FIRST_SPR_NUM   (BUNKER_FIRST_SPR_NUM + NUM_OF_BUNKER_SPR)    // s/b 4
#define NUM_SAUCER_EXPLOS_IMGS    5
#define SAUCER_MAGENTA_EXPLOS_IMG_BASE     (BUNKER_IMG_BASE + BUNKER_IMG_BUF)
#define SAUCER_SCORE50_IMG_BASE     (SAUCER_MAGENTA_EXPLOS_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE100_IMG_BASE     (SAUCER_SCORE50_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE150_IMG_BASE     (SAUCER_SCORE100_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE300_IMG_BASE     (SAUCER_SCORE150_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_EXPLOS_IMG_BUF  (NUM_SAUCER_EXPLOS_IMGS * SPR_32X32_SIZE)

// ####  ALIENS  ####
// INVADER SPRITES
#define NUM_ALIEN_SPR 55                  // ID is 1 through 55
#define ALIEN_FIRST_SPR_NUM   (SAUCER_EXPLOS_FIRST_SPR_NUM + NUM_OF_SAUCER_EXPLOS_SPR)      // SPRITE # for 1st alien sprite, s/b 5
// INVADER ANIMATION
#define NUM_INVADER_ANIM_SPR 3          // 3 types
#define NUM_INVADER_IMGS_PER 2          // each with 2 animation images
// INVADER IMAGES
#define NUM_INVADER_IMGS   (NUM_INVADER_ANIM_SPR * NUM_INVADER_IMGS_PER)
#define INVADER_IMG_BASE   (SAUCER_MAGENTA_EXPLOS_IMG_BASE + SAUCER_EXPLOS_IMG_BUF)
#define INVADER_IMG_BUF    (SPR_16X16_SIZE * NUM_INVADER_IMGS)
#define INVADER_IMG_ALIEN_GREEN    INVADER_IMG_BASE
#define INVADER_IMG_ALIEN_GREEN_0    INVADER_IMG_BASE
#define INVADER_IMG_ALIEN_GREEN_1    (INVADER_IMG_BASE + SPR_16X16_SIZE)
#define INVADER_IMG_ALIEN_BLUE    (INVADER_IMG_BASE + (2 * SPR_16X16_SIZE))
#define INVADER_IMG_ALIEN_BLUE_0    (INVADER_IMG_BASE + (2 * SPR_16X16_SIZE))
#define INVADER_IMG_ALIEN_BLUE_1    (INVADER_IMG_BASE + (3 * SPR_16X16_SIZE))
#define INVADER_IMG_ALIEN_MAGENTA    (INVADER_IMG_BASE + (4 * SPR_16X16_SIZE))
#define INVADER_IMG_ALIEN_MAGENTA_0    (INVADER_IMG_BASE + (4 * SPR_16X16_SIZE))
#define INVADER_IMG_ALIEN_MAGENTA_1    (INVADER_IMG_BASE + (5 * SPR_16X16_SIZE))

// ALIEN MATRIX number of rows/columns,  pos, width, height in pixels of matrix
#define NUM_INVADER_ROWS 5
#define NUM_INVADER_COLS 11
#define INVADER_X_SPACING 16                // spacing from left edge of alien to left edge of next alien left edge
#define INVADER_Y_SPACING 16

#define INVADER_MTRX_START_X 64

// changed from 72 to 76... original's starting y (bottom of matrix) was 104 out of 240 px rows, 104 - 60 (height)
#define INVADER_MTRX_T_START_Y (56 - 4)
#define INVADER_MTRX_WIDTH 160     // width (x1 - x0) of the full matrix structure, irrespective of occupancy of aliens
#define INVADER_MTRX_HEIGHT 64
#define INVADER_MTRX_LIMIT_LX 48   // x axis - furthest occupied matrix can travel to the left, right and y- axis for bottom limit
#define INVADER_MTRX_LIMIT_RX (320U - 48)
#define INVADER_MTRX_B_START_Y    (INVADER_MTRX_T_START_Y + INVADER_MTRX_HEIGHT)
// ALIEN BOUNDING BOXES per type
#define INVADER_BBOX_Y0 4
#define INVADER_BBOX_Y1 12
#define INVADER_GRN_BBOX_X0 4       // green alien (# 44 - 54)
#define INVADER_GRN_BBOX_X1 12
#define INVADER_BLU_BBOX_X0 2       // blue alien (# 22 - 43)
#define INVADER_BLU_BBOX_X1 13
#define INVADER_MAG_BBOX_X0 2       // magenta alien (# 0 - 21)
#define INVADER_MAG_BBOX_X1 14
// ALIEN MOVEMENT & ANIMATION TIMING, EXPLOSION DURATION
// x = 2 pixels/tick, y = 8 pixels per drop, explosion lasts 20 ticks
#define INVADER_X_INCR 2           // effectively speed 2px/tick
#define INVADER_DROP_RATE 8
// timed this on original, explos lasted 16 ticks, same as bullet/player short... #define ALIEN_EXPL_TICKS 20
// counted ticks on original, it's 17
#define ALIEN_EXPL_TICKS 17

// ####  SAUCERS  ####
// SPRITES
#define NUM_SAUCER_SPR 1    // One SAUCER SPRITE, but is used for multiple types/images/bounding box
#define SAUCER_FIRST_SPR_NUM    (ALIEN_FIRST_SPR_NUM + NUM_ALIEN_SPR)  // s/b 60
// Need to add SAUCER SCORE Text to sprite images
// Note: 2nd saucer is in a holding pattern
//      print score # for a period of time  before terminating saucer
// IMAGES
#define NUM_SAUCER_IMGS     2   // for now MAGENTA and TYPE2 these are 16x16, there are two 32x32 explosions above
#define SAUCER_IMG_BUF     (SPR_16X16_SIZE * NUM_SAUCER_IMGS)
#define SAUCER_IMG_BASE     (INVADER_IMG_BASE + INVADER_IMG_BUF)
// POSITION/BOUNDING BOX
#define SAUCER_BASE_X 320U   // off screen initially
#define SAUCER_BASE_Y 40
#define SAUCER_BBOX_X0 0
#define SAUCER_BBOX_X1 16
#define SAUCER_BBOX_Y0 4
#define SAUCER_BBOX_Y1 11
// SPEED/EXPLOSION DURATION
#define SAUCER_SPEED 1
#define SAUCER_EXPL_TICKS 60
#define SAUCER_SPAWN_TIME 0x0600    // about 25 seconds

// ####  GUNNER  ####
// Note - we only need one gunner sprite for both players, just need to switch gunner images as players alternate
// SPRITES
#define NUM_GUNNER_SPR 1
#define GUNNER_FIRST_SPR_NUM    (SAUCER_FIRST_SPR_NUM + NUM_SAUCER_SPR)  // s/b 61
// ANIMATIONS
#define NUM_GUNNER_EXPLOS_ANIM 2    // two anim images per gunner type (player 1 blue/player2 yellow)
// IMAGES
// plyr1 blue with 2 explosion images, ditto ply2 yellow, plus bonus magenta gunner

#define NUM_GUNNER_IMG      6
#define GUNNER_IMG_BASE     (SAUCER_IMG_BASE + SAUCER_IMG_BUF)
#define GUNNER_IMG_BUF      (NUM_GUNNER_IMG * SPR_16X16_SIZE)
#define GUNNER_PLYR1_IMG_BASE   ((uint16_t) GUNNER_IMG_BASE)
#define GUNNER_PLYR2_IMG_BASE   (GUNNER_IMG_BASE + (3 * SPR_16X16_SIZE))
#define GUNNER_IMG_FOR_PLAYER(p) ((p) ? GUNNER_PLYR2_IMG_BASE : GUNNER_PLYR1_IMG_BASE)
// GUNNER STARTING POSITIONS, BOUNDING BOX
#define GUNNER_P1_X_BASE 55 // was 24
#define GUNNER_P2_X_BASE (320U - 55 - 16)// temp for intial demo/test mode, 57 was 24

// DID THE RESEARCH given my layout is 240 tall and original is 256, removed top and bottom blank lines
// ... with this GUNNER and BUNKERS move up 8, gunner to 204
#define GUNNER_Y_BASE (212 - 8)
#define GUNNER_BBOX_X0 1
#define GUNNER_BBOX_X1 14
// was 8, but should be 7 with proper sprite layout
#define GUNNER_BBOX_Y0 7
#define GUNNER_BBOX_Y1 12
// GUNNER SPEED/EXPLOSION DURATION
#define GUNNER_EXPL_TICKS 90

#define GUNNER_SPEED 1

// LIVES
// SPRITES
#define NUM_LIVES_SPR 8     // 4 LIVE sprites per player, including up to 3 initial lives, plus a potential bonus LIFE
#define LIVES_FIRST_SPR_NUM     (GUNNER_FIRST_SPR_NUM + NUM_GUNNER_SPR)     // s/b 62
// IMAGES
// Reusing the player 1 & 2 gunner sprites, plus a bonus gunner image to indicate remaining lives
#define NUM_LIVES_IMG   1       // 1 NEW "bonus" IMAGE, PLUS 2 EXISTING IMAGES...reusing 2 gunner sprites
#define LIVES_P1_IMG_BASE   GUNNER_PLYR1_IMG_BASE
#define LIVES_P2_IMG_BASE   GUNNER_PLYR2_IMG_BASE
#define LIVES_BONUS_IMG_BASE    (GUNNER_IMG_BASE + GUNNER_IMG_BUF)
#define LIVES_BONUS_IMG_BUF     SPR_16X16_SIZE
// POSITIONS
#define LIVES_P1_X_BASE 56
#define LIVES_P2_X_BASE 200
#define LIVES_Y_BASE 228
#define LIVES_X_SPACING 16
// EXPLOSION/EROSION IMAGES
// One 16X16 for ALIEN, Two 8X8 for BULLETS and BOMBS
#define NUM_EXPLOS_SPR  0       // these are explosion images for existing sprites (alien, bullet, bomb)
#define NUM_BIG_EXPL_IMG   1
#define ALIEN_EXPL_IMG_BASE     (LIVES_BONUS_IMG_BASE + LIVES_BONUS_IMG_BUF)
#define INVADER_EXPL_IMG_BUF   SPR_16X16_SIZE
#define NUM_SMALL_EXPL_IMG 2
#define SMALL_EXPL_IMG_BASE    (ALIEN_EXPL_IMG_BASE + INVADER_EXPL_IMG_BUF)
#define SMALL_EXPL_IMG_BUF     (NUM_SMALL_EXPL_IMG * SPR_8X8_SIZE)
#define BULLET_EXPL_IMG_BASE   (ALIEN_EXPL_IMG_BASE + INVADER_EXPL_IMG_BUF)
#define BOMB_EXPL_IMG_BASE   (BULLET_EXPL_IMG_BASE + SPR_8X8_SIZE)

// ####  BULLET  ####
// SPRITE
#define NUM_BULLET_SPR 1
#define BULLET_FIRST_SPR_NUM    (LIVES_FIRST_SPR_NUM + NUM_LIVES_SPR)    // s/b 68
// #define BULLET_FIRST_SPR_NUM 68
// IMAGES
#define BULLET_IMG_BASE     (SMALL_EXPL_IMG_BASE + SMALL_EXPL_IMG_BUF)
#define BULLET_IMG_BUF     (SPR_8X8_SIZE * NUM_BULLET_SPR)
// SPEED/EXPLOSION DURATION
// SEE alien explosion timing... #define BULLET_EXPL_TICKS 20    // duration of explosion image
#define BULLET_EXPL_TICKS 16
#define BULLET_SPEED 4      // # of pixels movement/tick
#define BULLET_BBOX_X0 3
#define BULLET_BBOX_X1 4
#define BULLET_BBOX_Y0 0
#define BULLET_BBOX_Y1 3

// ####  BOMBS  ####
// SPRITES
#define NUM_BOMB_SPR 3      // 3 types, screw, spike, sawtooth
#define BOMB_FIRST_SPR_NUM      (BULLET_FIRST_SPR_NUM + NUM_BULLET_SPR)     // S/B 69
// IMAGES
#define NUM_BOMB_IMG   12   // 4 animation images for each of the 3 bomb types

#define CONST16_MASK_MIDDLE_BITS  ((uint16_t) 0x00FFFF00u)

#define BOMB_IMG_BASE       (BULLET_IMG_BASE + BULLET_IMG_BUF)
#define BOMB_IMG_BUF        (SPR_8X8_SIZE * NUM_BOMB_IMG)
#define BOMB_SCREW_IMG_BASE     BOMB_IMG_BASE
#define BOMB_SPIKE_IMG_BASE     (BOMB_SCREW_IMG_BASE + (4 * SPR_8X8_SIZE))
#define BOMB_SAWTOOTH_IMG_BASE  (BOMB_SPIKE_IMG_BASE + (4 * SPR_8X8_SIZE))
// BBOX
#define BOMB_BBOX_X0 2
#define BOMB_BBOX_X1 5
#define BOMB_BBOX_SCREW_Y0 1
#define BOMB_BBOX_SAWTOOTH_Y0 1
#define BOMB_BBOX_SPIKE_Y0 2
#define BOMB_BBOX_Y1 8
// EXPLOSION DURATION
// timed the original, explos lasted 10 ticks (timer triggers on time = 1, so +1)... #define BOMB_EXPL_TICKS 20
#define BOMB_EXPL_TICKS 10

// ####  SAUCER SCORE  ####
// SPRITES
#define NUM_SCORE_SPR 3     // 2 or 3 digits 50, 100, 150, 300
#define SCORE_FIRST_SPR_NUM      (BOMB_FIRST_SPR_NUM + NUM_BOMB_SPR)     // S/B 69 + 3 = 72
// IMAGES
#define NUM_SCORE_IMG 4    // 0, 1, 3, 5 numerals
#define SCORE_IMG_BASE       (BOMB_IMG_BASE + BOMB_IMG_BUF)
#define SCORE_IMG_BUF        (SPR_8X8_SIZE * NUM_SCORE_IMG)
#define SCORE_ZERO_IMG_BASE     SCORE_IMG_BASE
#define SCORE_ONE_IMG_BASE      SCORE_ZERO_IMG_BASE + SPR_8X8_SIZE
#define SCORE_THREE_IMG_BASE    SCORE_ONE_IMG_BASE + SPR_8X8_SIZE
#define SCORE_FIVE_IMG_BASE    SCORE_THREE_IMG_BASE + SPR_8X8_SIZE
// DISPLAY DURATION
#define SCORE_DISPLAY_TICKS 20

// Note: BOMB SPEED is variable between 4 and 5 px per step, speeds up when fewer than 9 aliens remain

#define TOTAL_NUM_SPR (NUM_OF_BUNKER_SPR + NUM_OF_SAUCER_EXPLOS_SPR + NUM_ALIEN_SPR + NUM_SAUCER_SPR + NUM_GUNNER_SPR + NUM_LIVES_SPR + NUM_BULLET_SPR + NUM_BOMB_SPR)
//#define TOTAL_NUM_SPR 72

// For accessing the font library
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
