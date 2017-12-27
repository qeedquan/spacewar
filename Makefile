all:
	cc -o spacewar src/*.c `sdl2-config --cflags --libs` -lm -Wall -pedantic -Wextra -march=native -O3 #-fsanitize=undefined
