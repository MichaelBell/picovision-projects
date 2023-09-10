# Conway's Game of Life for PicoDV

Conway's game of life implementation for DV Stick.

Code needs some structuring, but the basics are there to either run starting from a random start or from standard Game of Life RLE data.

I've made it so the cells at the edge of the screen can never be alive, which produces some interesting effects as objects hit the edges.

The simulation normally manages 30 FPS on a moderately busy 640x480 board - further optimization is definitely possible.