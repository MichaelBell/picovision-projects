# Pong Wars for PicoVision
#
# Original idea from https://twitter.com/nicolasdnl/status/1749715070928433161
# via https://hachyderm.io/@vnglst/111828811496422610

from picovision import PicoVision, PEN_RGB555, PEN_P5
import machine
import math
import time

machine.freq(200_000_000)

display = PicoVision(width=720, height=400, pen_type=PEN_RGB555)
WIDTH, HEIGHT = display.get_bounds()

BORDER_X = 160
BORDER_Y = 8
VIEW_WIDTH = WIDTH - 2*BORDER_X
VIEW_HEIGHT = HEIGHT - 2*BORDER_Y

BLOCK_SIZE = 16
FIELD_WIDTH = VIEW_WIDTH // BLOCK_SIZE
FIELD_HEIGHT = VIEW_HEIGHT // BLOCK_SIZE

#### SOLARIZED ####

# Background & Content Tones
BASE_03 = display.create_pen(0x00, 0x2b, 0x36)  # Base 03
BASE_02 = display.create_pen(0x07, 0x36, 0x42)  # Base 02
BASE_01 = display.create_pen(0x58, 0x6e, 0x75)  # base 01
BASE_00 = display.create_pen(0x65, 0x7b, 0x83)  # Base 00
BASE_0 = display.create_pen(0x83, 0x94, 0x96)   # Base 0
BASE_1 = display.create_pen(0x93, 0xa1, 0xa1)   # Base 1
BASE_2 = display.create_pen(0xee, 0xe8, 0xd5)   # Base 2
BASE_3 = display.create_pen(0xfd, 0xf6, 0xe3)   # Base 3

# Accent Colours
YELLOW = display.create_pen(0xb5, 0x89, 0x00)   # Yellow
ORANGE = display.create_pen(0xcb, 0x4b, 0x16)   # Orange
RED = display.create_pen(0xdc, 0x32, 0x2f)      # Red
MAGENTA = display.create_pen(0xd3, 0x36, 0x82)  # Magenta
VIOLET = display.create_pen(0x6c, 0x71, 0xc4)   # Violet
BLUE = display.create_pen(0x26, 0x8b, 0xd2)     # Blue
CYAN = display.create_pen(0x2a, 0xa1, 0x98)     # Cyan
GREEN = display.create_pen(0x85, 0x99, 0x00)    # Green

# Friendly names for light/dark backgrounds
BG_DARK = BASE_03
BG_LIGHT = BASE_2

####

display.set_pen(BASE_01)
display.clear()
display.update()

display.set_pen(BASE_01)
display.clear()

display.set_clip(BORDER_X, BORDER_Y, VIEW_WIDTH, VIEW_HEIGHT)

# The playing field
class Field:
    def __init__(self):
        self.field = bytearray(FIELD_WIDTH * FIELD_HEIGHT)
        for x in range(FIELD_WIDTH):
            for y in range(FIELD_HEIGHT):
                self.field[x+y*FIELD_WIDTH] = 1 if x >= FIELD_WIDTH//2 else 0
        self.changed = False
        
    def draw(self):
        for x in range(FIELD_WIDTH):
            for y in range(FIELD_HEIGHT):
                display.set_pen(BASE_03 if self.field[x+y*FIELD_WIDTH] == 0 else BASE_3)
                display.rectangle(BORDER_X + x * BLOCK_SIZE, BORDER_Y + y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)

    def get(self, x, y):
        return self.field[x+y*FIELD_WIDTH]
    
    def put(self, x, y, val):
        self.field[x+y*FIELD_WIDTH] = val
        self.changed = True

# Init the field and load ball sprites
BALL_DARK = 1
BALL_LIGHT = 0

field = Field()
for _ in range(2):
    field.draw()
    display.load_sprite("ball-dark.png", BALL_DARK)
    display.load_sprite("ball-light.png", BALL_LIGHT)
    display.update()

BOUNDS_FOR_BALL = (BORDER_X, BORDER_Y, WIDTH - BORDER_X - 16, HEIGHT - BORDER_Y - 16)

class Ball:
    def __init__(self, x, y, sprite_idx, slot):
        self.x = x
        self.y = y
        self.idx = sprite_idx
        self.slot = slot
        self.valid_field = sprite_idx
        
        self.dir_x = 1
        self.dir_y = 1
        
        self.x_changed_last_tick = 0
        self.y_changed_last_tick = 0
        
    def draw(self):
        display.display_sprite(self.slot, self.idx, self.x, self.y)
        
    def move(self, count):
        if self.x <= BOUNDS_FOR_BALL[0]:
            self.dir_x = 1
            self.x = BOUNDS_FOR_BALL[0]
        elif self.x >= BOUNDS_FOR_BALL[2]:
            self.dir_x = -1
            self.x = BOUNDS_FOR_BALL[2]
            
        if self.y <= BOUNDS_FOR_BALL[1]:
            self.dir_y = 1
            self.y = BOUNDS_FOR_BALL[1]
        elif self.y >= BOUNDS_FOR_BALL[3]:
            self.dir_y = -1
            self.y = BOUNDS_FOR_BALL[3]

        if self.dir_y > 0:
            field_y = (self.y - BORDER_Y + BLOCK_SIZE-1) // BLOCK_SIZE
        else:
            field_y = (self.y - BORDER_Y) // BLOCK_SIZE
        self.x += self.dir_x

        if self.dir_x > 0:
            field_x = (self.x - BORDER_X + BLOCK_SIZE-1) // BLOCK_SIZE
        else:
            field_x = (self.x - BORDER_X) // BLOCK_SIZE
            
        if self.x_changed_last_tick == 0 and field.get(field_x, field_y) != self.valid_field:
            self.dir_x = -self.dir_x
            field.put(field_x, field_y, self.valid_field)
            self.x_changed_last_tick = 2
            #print(f"{self.idx} {count} X {self.x} {self.dir_x}")
        elif self.x_changed_last_tick > 0:
            self.x_changed_last_tick -= 1

        self.y += self.dir_y
        if self.dir_y > 0:
            field_y = (self.y - BORDER_Y + BLOCK_SIZE-1) // BLOCK_SIZE
        else:
            field_y = (self.y - BORDER_Y) // BLOCK_SIZE
            
        if self.y_changed_last_tick == 0 and self.x_changed_last_tick != 2 and field.get(field_x, field_y) != self.valid_field:
            self.dir_y = -self.dir_y
            field.put(field_x, field_y, self.valid_field)
            self.y_changed_last_tick = 2
            #print(f"{self.idx} {count} Y {self.y} {self.dir_y}")
        elif self.y_changed_last_tick > 0:
            self.y_changed_last_tick -= 1
        

# Setup the balls - uncomment for double the bounce
balls = [Ball(WIDTH//4, HEIGHT//3, BALL_LIGHT, 0),
#         Ball(WIDTH//4 + 56, HEIGHT//3+23, BALL_LIGHT, 1),
         Ball(3*WIDTH//4, 2*HEIGHT//3, BALL_DARK, 2),
#         Ball(3*WIDTH//4-73, 2*HEIGHT//3-200, BALL_DARK, 3),
         ]
count = 0

# Update function runs on a timer, balls move as sprites without an update
def update_balls(t):
    global count
    
    try:
        for ball in balls:
            ball.move(count)
            ball.draw()
        count += 1
    except Exception as e:
        t.deinit()
        raise e

ball_timer = machine.Timer(period=3, callback=update_balls)

# Main update redraws the field and flips only when it has changed
# This could be optimized significantly to only write the changes
try:
    while True:
        while not field.changed:
            time.sleep_us(100)
        field.changed = False
        field.draw()
        display.update()
        
finally:
    ball_timer.deinit()
