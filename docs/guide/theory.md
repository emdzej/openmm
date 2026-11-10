# Theory of operation

How Micro Machines for DOS was built and how it works, for someone who wants to build such a game. It
is written from the port: every statement here was read in the original program and checked against it
running. The internal notes in the repository (`docs/re/`, `docs/formats/`) carry the exact layouts.

## The shape of the program

The game is three programs and a handful of loadable pieces. The player types `MICRO`, a tiny launcher
that shrinks its own memory, has DOS run the Codemasters splash screen, then the game proper, and prints
a message if either cannot be run. The game itself is a real-mode program written by hand in assembly,
compressed with PKLITE; the decompressor at its front unpacks it in memory and jumps in.

Three more files are programs although they do not look like it. The copy protection is one, loaded
before anything else and called like a subroutine; it draws its own screen in another video mode and
answers pass or fail. The two sound drivers are the others: whichever one the settings name is loaded
into the same slot of memory after the protection is done, and the game talks to it through a single
entry point with a function number, "play this tune", "start this effect", "tick". The game never
touches the sound hardware for music; the driver does, at the timer's rate.

Everything else the game drives itself, straight through the hardware's ports: the VGA in its 320 x 200,
256-colour mode, the timer chip, the keyboard controller, the joystick port. It replaces the timer's and
the keyboard's interrupt handlers with its own, and puts DOS's back on the way out (setting the clock
again from the real-time clock, because its timer ran DOS's clock fast all along).

## Time

The whole game is paced by one interrupt. The timer handler fires once per displayed frame, about 70
times a second, and it is locked to the monitor: inside the interrupt it waits for the vertical retrace
to begin, then reprograms the timer to fire again a little before the next one. So "a tick" in this game
means "a frame of the monitor", and every wait in the program, in the menus and in the race, is a loop
that spins until a counter the handler advances has moved. The same handler ticks the sound driver, so
music and pictures share one clock.

This is why the port keeps the original's loops instead of restructuring the game into a callback per
frame: each loop spins on memory the interrupt handlers change, and the port makes the frame boundary
the place where the handlers run.

## Memory

The game sizes nothing at run time. Its data segment holds the variables, the tables and the small
graphics; around it sit fixed buffers, each a 64 KB segment or a run of them, chosen once: the graphic
tiles of the current track, the expanded map, the vehicles' pictures, the effects, and one work segment
that is in turn the decompressor's output, the palettes of the fades and, during a race, the back buffer.
The same buffer serves several loads in a row, each emptied before the next arrives. Because everything
is at a known place, a routine can address any of it with a constant segment, and several routines read
past the end of a file into whatever lies behind it; the port reproduces those reads by keeping the same
memory layout.

## The front end

Before the game proper there is the options screen: who controls player one and two (keys, two key sets,
joysticks, a mouse), which sound device, and the "smoothness". The settings are saved to a small file
when they change and the game runs from a hard disk. A secret sequence of keys typed here unlocks a
debug mode for the pause screen.

Then the loop the player sees: attract screens (the logo, then each class of vehicle in turn), the
choice of one or two players, the game type, and the drivers' carousel. The menus share a few tools:
sprite objects drawn into the back buffer with what they covered saved, a two-way chooser with a
pointing thumb, blinking prompts timed off the frame counter, text in two bitmap fonts. Each screen is
composed off screen and copied to the display whole, or in bands of rows when only a band changed (the
carousel turning).

The carousel is a ring of eleven drivers turned in eased steps; picking one marks it taken in a pool so
the next player cannot have it. Each driver carries a handicap byte that later sets how fast the
computer drives that character.

The one-player game is a series: a qualifying race, then races against three computer drivers, with
lives lost for finishing low, a championship map between races, bonus races, and a winner's screen.
Two players either race a single race on a chosen class or play a tournament.

A race quit with Esc jumps straight back to the menu loop from whatever depth it was at: the program
saved its stack pointer at the start and simply reloads it, the assembly programmer's longjmp.

## A track

Each class of vehicle has its own world (a bath, a pool table, a checked tablecloth and so on), and each
world has a few tracks. A track is small: a 32 x 32 grid of bytes, each naming one of the world's 64
tile types, plus a second grid of checkpoint numbers. The tile types are where the work is. For each,
the world supplies a 6 x 6 composition of 16-pixel graphic tiles (so a track tile is 96 pixels square
and the world 3,072), a 12 x 12 collision mask of solid cells, a 6 x 6 grid of surface cells, and a flag
byte. At the start of a race the track is expanded into a 192 x 192 map of graphic-tile numbers; the
race draws from that map and asks the small grid what is underneath.

A surface cell holds two things in one byte. Its high half is the terrain class, which picks a handler
for that world: a ramp, a hole, a slick patch, a bump, the edge of a table, stepped heights, a warp. Its
low half is a direction, laid into the track by its designer, that the computer drivers follow. The
checkpoint numbers give the order of the course: the lap count, the race position and the short-cut
test all come from them, and a number of "off the track" means a fall.

Some worlds draw certain tiles a second time after the vehicles, so a car can pass under a bridge or a
plant. The program marks those tiles with a spare bit of the map at the start of a race and keeps the
list of where they are on the processor's stack while it draws, popping it afterwards.

## A race

The race loop runs a number of game steps per displayed frame (the smoothness setting: on a slow
machine more steps are simulated per picture), composes the picture only on the last of them, waits for
the frame counter, shows the picture and starts again. A game step reads the inputs, moves the four
vehicles, resolves their collisions, runs the terrain under each, and keeps the rules.

**Driving.** A vehicle has a heading on a 256-step circle and a signed speed. Accelerating and braking
change the speed up to the class's limits; with neither pressed a drag brings it back to rest. The speed
and the heading give a wanted velocity, and the actual velocity follows it: at once when the difference
is small, or by a fixed step per frame when it is larger and the vehicle can slide. That is the whole of
the drift: in a sharp turn the car keeps its momentum until the velocity catches up with where it
points. Oil and ice swap the class's grip for the world's slide values for a while. Released from a
turn, the heading snaps to the nearest of sixteen directions, so cars run straight down clean lines.

**Moving.** Positions are fixed point, whole pixels and a fraction. Each step a candidate position is
computed and looked up in the collision mask; on a solid cell four probes to the sides tell which way
the wall lies, and the velocity bounces off that axis. A position on a tile marked safe is remembered, so
that a car that falls off the table or into the water can be put back where it last was on the track.

**Collisions.** Every pair of vehicles is tested; when two are close, a small precomputed table gives
the direction of contact for their offset (a round shape), and an impulse along that direction is shared
between them. The impulse has a floor, so every touch is a push: cars never stick together.

**The computer drivers.** A computer car steers towards the direction its surface cell names, through a
per-lane table where the track has several lines, holding its heading within a couple of steps of the
wanted one. A per-checkpoint byte of the track tells it to go flat out, brake above a limit or lift, so
the designer tunes the computer's speed corner by corner. When player one leads, every computer car gets
a burst of acceleration and grip: the rubber band that keeps the race close.

**The terrain.** Each terrain class runs its handler every step the car is on it: jumps from a raised
level (with a height, a vertical speed and the world's gravity, and the car's shadow drawn offset by the
height), holes that swallow a car unless it is fast enough, edges to fall off, slick patches, the warp
holes of one world that carry a car across the map. A car in trouble goes through a little state machine
(falling, sinking, splashing, being put back), each state a sequence of picture frames with durations.

**The camera and the picture.** The camera follows the race, and the picture is the map drawn tile by
tile from the camera's position into the back buffer, the vehicles and effects drawn over it, the
foreground tiles over those, the race positions on the left. The scroll is kept at a multiple of four
pixels so the copies to the screen can move four bytes at a time; the program patches its own copy loops
at start-up to use the 386's 32-bit moves.

**Two players.** Head to head is a tug of war rather than a race to the line. A bar of points starts in
the middle; when one player leaves the other behind off the screen, or knocks the other out, the leader
scores: both stop, the scorer spins, the bar moves one step, and both are put back side by side at the
scorer's checkpoint. The first to pull the bar to an end wins.

## Sound

The sound drivers are general-purpose: they come from a third-party package, with their own sequencer.
The AdLib driver plays its music on the sound chip's nine channels with a copy of the chip's registers,
so it writes only what changes; the speaker driver plays a chord as a fast arpeggio, one note per tick.
During a race the game itself drives the engine notes, a pitch per human-driven vehicle that follows its
speed, rises in the air and wavers with a little random jitter.

## The copy protection

The code card that came in the box has a grid of symbols. The protection program picks a column and a
row from the BIOS clock, draws the symbols, and lets the player move a box over the one the card shows
at that place. Three tries; a wrong answer ends the game before it starts. The port has rewritten it
too, but leaves it off unless asked for.
