
//####################################################################################################
//################################ -= COMPILER DIRECTIVES =- #########################################
//####################################################################################################

//#define OLD_CODE
//#define NEW_CODE

// 6522 VIA REGISTERS
#define VIA_BASE 0xFF00U
#define VIA_CNTR_1_LSB 0xFF04U       // TIMER 1, COUNTER LS BYTE
#define VIA_CNTR_1_MSB 0xFF05U       // MS BYTE
#define VIA_ACR 0xFF0BU              // AUX CONTROL REGISTER
#define VIA_IFR 0xFF0DU              // INTERRUPT FLAG REG
#define VIA_IER 0xFF0EU              // INTERRUPT ENABLE REG

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

// SPRITE CONFIG BASE ADDR AND IMAGE BASE ADDR... and SPRITE IMAGE BUFFER SIZES
#define SPR_CFG_BASE 0xC000U     // Base address of sprite config array
#define SPR_IMG_BASE 0x0200U     // Base address of sprite image array, # images not necessarily equal to number of sprites
#define SPR_32X32_SIZE 0x0800U   // Buffer size for each sprite size
#define SPR_16X16_SIZE 0X0200U
#define SPR_8X8_SIZE 0x0080U

// ####  BUNKER  ####
// NOTE: each sprite image is eaten away by colors being changed to transparent...
//      based on bomb/bullet blast pattern being AND'd with bunkr sprite image (zeroes are tansparent, 0xFF no change to color)
//      4 images per player plus one restoration copy, but only 4 sprites for visible Military bunkrs, ID # are 0 thru 3
// during game time, the proper set of 4 bunker images is pointed to by the 4 sprite img ptrs, based on active player #
#define NUM_OF_BUNKR_SPR 4
#define BUNKR_FIRST_SPR_NUM 0                   // first sprite config is number 0, last bunker sprite is 3
// IMAGES
#define NUM_OF_BUNKR_IMGS 9
#define BUNKR_IMG_BASE   SPR_IMG_BASE           // base address of Military bunkr Image

#define BUNKR_PRISTINE_IMG_BASE   BUNKR_IMG_BASE
#define BUNKR_IMG_SIZE   SPR_32X32_SIZE         // 32 x 32 = 0x0400

#define BUNKR_IMG_BUF   (SPR_32X32_SIZE * NUM_OF_BUNKR_IMGS)       // Total size of bunkr image buffer

#define BUNKR_0_PLYR1_IMG_BUF  (BUNKR_IMG_BASE + SPR_32X32_SIZE)
#define BUNKR_1_PLYR1_IMG_BUF  (BUNKR_0_PLYR1_IMG_BUF + SPR_32X32_SIZE)
#define BUNKR_2_PLYR1_IMG_BUF  (BUNKR_1_PLYR1_IMG_BUF + SPR_32X32_SIZE)
#define BUNKR_3_PLYR1_IMG_BUF  (BUNKR_2_PLYR1_IMG_BUF + SPR_32X32_SIZE)

#define BUNKR_0_PLYR2_IMG_BUF  (BUNKR_0_PLYR1_IMG_BUF + (4 * SPR_32X32_SIZE))
#define BUNKR_1_PLYR2_IMG_BUF  (BUNKR_0_PLYR2_IMG_BUF + SPR_32X32_SIZE)
#define BUNKR_2_PLYR2_IMG_BUF  (BUNKR_1_PLYR2_IMG_BUF + SPR_32X32_SIZE)
#define BUNKR_3_PLYR2_IMG_BUF  (BUNKR_2_PLYR2_IMG_BUF + SPR_32X32_SIZE)

// math is bnkr image offset is 5, bnkr width = 22, space between = 22, 320 is screen width, so (320-4*22-3*22-2*5)/2 = ???

// POSITIONS/BOUNDING BOXES
// DID THE RESEARCH given my layout is 240 tall and original is 256, removed top and bottom blank lines
// ... with this GUNNER and BUNKERS move up 8, bunker to 176
//#define BUNKR_Y (192 - 8)       // -8 due to image offset change from y0 = y + 0 to y + 8
#define BUNKR_Y (192 - 8 - 8)       // -8 due to image offset change from y0 = y + 0 to y + 8
#define BUNKR_WIDTH 22          // width of displayed image
#define BUNKR_FULL_WIDTH 32     // width (# of columns) of sprite image buffer
#define BUNKR_GAP 23            // # px gap between visible bunker images
#define BUNKR_X_SPACING  (BUNKR_WIDTH + BUNKR_GAP)     // 22 (visible) bunker, 23 space between bunkers = 45 left edge to left edge
#define BUNKR_ZERO_X 76         // full sprite x, left side = 76, right 77 since first bunker left edge to last bunker right edge is 157 (odd #)
#define BUNKR_ZERO_X0  (BUNKR_ZERO_X + 5)     // bunker image is offset 5 from left edge of 32x32 sprite
#define BUNKR_ZERO_X1  (BUNKR_ZERO_X + 27)    // width of image = 22, right edge is 22 + 5 from left edge of sprite
#define BUNKR_ONE_X0    (BUNKR_ZERO_X0 + BUNKR_X_SPACING)
#define BUNKR_ONE_X1    (BUNKR_ZERO_X1 + BUNKR_X_SPACING)
#define BUNKR_TWO_X0    (BUNKR_ONE_X0 + BUNKR_X_SPACING)
#define BUNKR_TWO_X1    (BUNKR_ONE_X1 + BUNKR_X_SPACING)
#define BUNKR_THREE_X0   (BUNKR_TWO_X0 + BUNKR_X_SPACING)
#define BUNKR_THREE_X1   (BUNKR_TWO_X1 + BUNKR_X_SPACING)
#define BUNKR_THREE_X1_PLUS16   (BUNKR_THREE_X1 + 16)

#define BUNKR_MACRO_BBOX_X0      (BUNKR_ZERO_X + 5)
#define BUNKR_MACRO_BBOX_X1      (BUNKR_ZERO_X + 27)
#define BUNKR_MACRO_BBOX_Y0      (BUNKR_Y + 8)
#define BUNKR_MACRO_BBOX_Y1      (BUNKR_Y + 24)

// SAUCER EXPLOSION IMAGES
#define NUM_OF_SAUCER_EXPLOS_SPR 1
#define SAUCER_EXPLOS_FIRST_SPR_NUM   (BUNKR_FIRST_SPR_NUM + NUM_OF_BUNKR_SPR)    // s/b 4
#define NUM_SAUCER_EXPLOS_IMGS    5
#define SAUCER_MAGENTA_EXPLOS_IMG_BASE     (BUNKR_IMG_BASE + BUNKR_IMG_BUF)
#define SAUCER_SCORE50_IMG_BASE     (SAUCER_MAGENTA_EXPLOS_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE100_IMG_BASE     (SAUCER_SCORE50_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE150_IMG_BASE     (SAUCER_SCORE100_IMG_BASE + SPR_32X32_SIZE)
#define SAUCER_SCORE300_IMG_BASE     (SAUCER_SCORE150_IMG_BASE + SPR_32X32_SIZE)
// # define SAUCER_TYPE2_EXPLOS_IMG_BASE       (SAUCER_MAGENTA_EXPLOS_IMG_BASE + SPR_32X32_SIZE)
# define SAUCER_EXPLOS_IMG_BUF  (NUM_SAUCER_EXPLOS_IMGS * SPR_32X32_SIZE)

// ####  ALIENS  ####
// INVR SPRITES
#define NUM_INVR_SPR 55                  // ID is 1 through 55
#define INVR_FIRST_SPR_NUM   (SAUCER_EXPLOS_FIRST_SPR_NUM + NUM_OF_SAUCER_EXPLOS_SPR)      // SPRITE # for 1st alien sprite, s/b 5
// INVR ANIMATION
#define NUM_INVR_ANIM_SPR 3          // 3 types
#define NUM_INVR_IMGS_PER 2          // each with 2 animation images
// INVR IMAGES
#define NUM_INVR_IMGS   (NUM_INVR_ANIM_SPR * NUM_INVR_IMGS_PER)
#define INVR_IMG_BASE   (SAUCER_MAGENTA_EXPLOS_IMG_BASE + SAUCER_EXPLOS_IMG_BUF)
#define INVR_IMG_BUF    (SPR_16X16_SIZE * NUM_INVR_IMGS)
#define INVR_IMG_GREENIE    INVR_IMG_BASE
#define INVR_IMG_GREENIE_0    INVR_IMG_BASE
#define INVR_IMG_GREENIE_1    (INVR_IMG_BASE + SPR_16X16_SIZE)
#define INVR_IMG_BLUIE    (INVR_IMG_BASE + (2 * SPR_16X16_SIZE))
#define INVR_IMG_BLUIE_0    (INVR_IMG_BASE + (2 * SPR_16X16_SIZE))
#define INVR_IMG_BLUIE_1    (INVR_IMG_BASE + (3 * SPR_16X16_SIZE))
#define INVR_IMG_MAGENTAIE    (INVR_IMG_BASE + (4 * SPR_16X16_SIZE))
#define INVR_IMG_MAGENTAIE_0    (INVR_IMG_BASE + (4 * SPR_16X16_SIZE))
#define INVR_IMG_MAGENTAIE_1    (INVR_IMG_BASE + (5 * SPR_16X16_SIZE))

// ALIEN MATRIX number of rows/columns,  pos, width, height in pixels of matrix
#define NUM_INVR_ROWS 5
#define NUM_INVR_COLS 11
#define INVR_X_SPACING 16                // spacing from left edge of alien to left edge of next alien left edge
#define INVR_Y_SPACING 16

// will need to change this to eliminate 2 rows since screen is supposed to be 256 but is only 240
#define INVR_MTRX_START_X 64
// TEST #define INVR_MTRX_START_X 40

// changed from 72 to 76... original's starting y (bottom of matrix) was 104 out of 240 px rows, 104 - 60 (height)
#define INVR_MTRX_T_START_Y (56 - 4)
#define INVR_MTRX_WIDTH 160     // width (x1 - x0) of the full matrix structure, irrespective of occupancy of aliens
#define INV_MTRX_HEIGHT 64
#define INVR_MTRX_LIMIT_LX 48   // x axis - furthest occupied matrix can travel to the left, right and y- axis for bottom limit
#define INVR_MTRX_LIMIT_RX (320U - 48)
#define INVR_MTRX_B_START_Y    (INVR_MTRX_T_START_Y + INV_MTRX_HEIGHT)
// ALIEN BOUNDING BOXES per type
#define INVR_BBOX_Y0 4
#define INVR_BBOX_Y1 12
#define INVR_GRN_BBOX_X0 4       // greenie (alien # 44 - 54)
#define INVR_GRN_BBOX_X1 12
#define INVR_BLU_BBOX_X0 2       // bluie (alien # 22 - 43)
#define INVR_BLU_BBOX_X1 13
#define INVR_MAG_BBOX_X0 2       // magentaie (alien 0 - 21)
#define INVR_MAG_BBOX_X1 14
// ALIEN MOVEMENT & ANIMATION TIMING, EXPLOSION DURATION
// x = 2 pixels/tick, y = 8 pixels per drop, explosion lasts 20 ticks
#define INVR_X_INCR 2           // effectively speed 2px/tick
#define INVR_DROP_RATE 8
// timed this on original, explos lasted 16 ticks, same as bullet/player short... #define INVR_EXPL_TICKS 20
// counted ticks on original, it's 17
#define INVR_EXPL_TICKS 17

// ####  SAUCERS  ####
// SPRITES
#define NUM_SAUCER_SPR 1    // One SAUCER SPRITE, but is used for multiple types/images/bounding box
#define SAUCER_FIRST_SPR_NUM    (INVR_FIRST_SPR_NUM + NUM_INVR_SPR)  // s/b 60
// Need to add SAUCER SCORE Text to sprite images
// Note: 2nd saucer is in a holding pattern
//      print score # for a period of time  before terminating saucer
// IMAGES
#define NUM_SAUCER_IMGS     2   // for now MAGENTA and TYPE2 these are 16x16, there are two 32x32 explosions above
#define SAUCER_IMG_BUF     (SPR_16X16_SIZE * NUM_SAUCER_IMGS)
#define SAUCER_IMG_BASE     (INVR_IMG_BASE + INVR_IMG_BUF)
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

// FIX THIS gunner image should be 8 px tall, need to make the bottom (widest) section taller by 1 px
#define NUM_GUNNER_IMG      6
#define GUNNER_IMG_BASE     (SAUCER_IMG_BASE + SAUCER_IMG_BUF)
#define GUNNER_IMG_BUF      (NUM_GUNNER_IMG * SPR_16X16_SIZE)
#define GUNNER_PLYR1_IMG_BASE   ((uint16_t) GUNNER_IMG_BASE)
#define GUNNER_PLYR2_IMG_BASE   (GUNNER_IMG_BASE + (3 * SPR_16X16_SIZE))
// GUNNER STARTING POSIITIONS, BOUNDING BOX
#define GUNNER_P1_X_BASE 55 // was 24
#define GUNNER_P2_X_BASE (320U - 55 - 16)// temp for intial demo/test mode, 57 was 24

// DID THE RESEARCH given my layout is 240 tall and original is 256, removed top and bottom blank lines
// ... with this GUNNER and BUNKERS move up 8, gunner to 204
//#define GUNNER_Y_BASE 212
#define GUNNER_Y_BASE 212 - 8
#define GUNNER_BBOX_X0 1
#define GUNNER_BBOX_X1 14
// was 8, but should be 7 with proper sprite layout
#define GUNNER_BBOX_Y0 7
#define GUNNER_BBOX_Y1 12
// GUNNER SPEED/EXPLOSION DURATION
#define GUNNER_EXPL_TICKS 90

// TEMP TEST
// supposedly its 1, but this player is updated twice per frame (every 120th of second)
//#define GUNNER_SPEED 2
#define GUNNER_SPEED 1

// TO DO - need to init and update lives sprites as separate entities from gunner sprite which represent an active player
// LIVES
// SPRITES
// TEMP converting from 6 to 8 sprites, total is now 74 SPRITES
//#define NUM_LIVES_SPR 6     // 3 LIVE sprites per player, including 1 or 2 lives remaining, plus a potential bonus LIFE
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
#define INVR_EXPL_IMG_BASE     (LIVES_BONUS_IMG_BASE + LIVES_BONUS_IMG_BUF)
#define INVR_EXPL_IMG_BUF   SPR_16X16_SIZE
#define NUM_SMALL_EXPL_IMG 2
#define SMALL_EXPL_IMG_BASE    (INVR_EXPL_IMG_BASE + INVR_EXPL_IMG_BUF)
#define SMALL_EXPL_IMG_BUF     (NUM_SMALL_EXPL_IMG * SPR_8X8_SIZE)
#define BULLET_EXPL_IMG_BASE   (INVR_EXPL_IMG_BASE + INVR_EXPL_IMG_BUF)
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

// Note: BOMB SPEED is variable between 4 and 5 px per step
// BASED ON???? see original behavior, I think speeds up when fewer than TBD aliens remain

#define TOTAL_NUM_SPR (NUM_OF_BUNKR_SPR + NUM_OF_SAUCER_EXPLOS_SPR + NUM_INVR_SPR + NUM_SAUCER_SPR + NUM_GUNNER_SPR + NUM_LIVES_SPR + NUM_BULLET_SPR + NUM_BOMB_SPR)
//#define TOTAL_NUM_SPR 72

// For accessing the font library
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
