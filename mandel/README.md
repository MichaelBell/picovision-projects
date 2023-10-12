# Mandelbrot set for PicoVision

Drawing the Mandelbrot set on PicoVision

This uses both cores on the CPU to generate the Mandelbrot set, and also uses a custom frame table to mirror the display, meaning the mirrored bottom half of the screen is just drawing the same data as the top half of the screen.