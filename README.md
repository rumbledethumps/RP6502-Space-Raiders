# Clone of Retro Arcade Game for the RP6502 Picocomputer, developed using CC65 

Objective: Eliminate all raiders in each wave

Lives - 3 lives plus an additional "bonus" life when Player's score exceeds 1500
Game over when player has zero lives remaining or when a Raider touches down
Player firing rate is limited, only one shot can be in flight at a time
Player shots and Raider bombs can collide but won't necessarily eliminate either the shot or the bomb
Player shots and Raider bombs will erode the protective bunkers
When a Raider makes contact with a bunker the overlapping portion of the bunker is destroyed 
A Raider Saucer appears periodically from the left or right margin, scoring for a Saucer hit is variable
The number of Raider bombs increases as the Player's score increases 
The initial position of the Raider matrix is lowered for successive waves of attack, after seven waves, the cycle repeats
Scoring is as shown on the 2nd Splash Screen
Number of lives remaining is indicated numerically near the bottom of the screen and by the quantity of Player Gunner Icons shown at the bottom of the screen plus the active gunner
Player1 is blue, Player2 is yellow
If a USB memory device is attached, the "high score" will be saved to the device's non-volatile memory and reloaded when a new game is started

GAME CONTROLS
"1" - Single player mode
"2" - Two player mode
"d" - Demo mode
"p" - Pause the game
"r" - Restart the game
"esc" - Terminate the game, return to Picocomputer OS

PLAYER CONTROLS:
"left arrow" - Move player left
"right arrow" - Move player right
"space bar" or "up arrow" or "down arrow" - fire button


