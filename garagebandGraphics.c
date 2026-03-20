#include <stdlib.h>
#include <stdbool.h>

//function declarations
void plot_pixel(int x, int y, short int line_color);
short int get_pixel(int x, int y);

void draw_line (int x0, int y0, int x1, int y1, short int line_color);
void draw_circle(int cx, int cy, int radius, short int color);
void draw_digit(int digit, int x, int y, short int color);
void draw_play_button_gray();
void draw_skip_button();
void fill_area (int x0, int x1, int y0, int y1, short int fill_color);
void fill_shape(int minX, int minY, int maxX, int maxY, short int fill_color, short int edge_color);

void draw_main_screen(int playActive, int recordActive);
void clear_screen();
void wait_for_vsync();

void init_mouse();
int read_ps2_byte();
void clear_ps2();
void read_mouse(int *clicked, int *clickX, int *clickY);
void draw_cursor(int x, int y, short int color);
	
void swap (int* x, int* y);

//global variables
volatile int pixel_buffer_start;
short int Buffer1[240][512]; // 240 rows, 512 (320 + padding) columns
short int Buffer2[240][512];
// mouse position tracked globally
int mouseX = 160;  // start in center of screen
int mouseY = 120;

#define PS2_BASE 0xFF200100
#define FRAME_BASE 0xFF203020

int main (void) {
	volatile int * pixel_ctrl_ptr = (int *)FRAME_BASE;
    /* Read location of the pixel buffer from the pixel buffer controller */
    pixel_buffer_start = *pixel_ctrl_ptr;

    //set front pixel buffer to Buffer 1
    *(pixel_ctrl_ptr + 1) = (int) &Buffer1; // first store the address in the  back buffer
	//now, swap the front/back buffers, to set the front buffer location
    wait_for_vsync();
    //initialize a pointer to the pixel buffer, used by drawing functions
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen(); // pixel_buffer_start points to the pixel buffer

    //set back pixel buffer to Buffer 2
    *(pixel_ctrl_ptr + 1) = (int) &Buffer2;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1); // we draw on the back buffer
	clear_screen();
	
	//initialize mouse position
	init_mouse();
	clear_ps2();
	
	int prevMouseX1 = 160, prevMouseY1 = 120;  // 1 frame ago
	int prevMouseX2 = 160, prevMouseY2 = 120;  // 2 frames ago
	
	//toggle states
	int playActive = 0;
	int recordActive = 0;

	while (1) {
		draw_main_screen(playActive, recordActive);
		
		int clicked = 0, clickX = 0, clickY = 0;
		read_mouse(&clicked, &clickX, &clickY);  // just one packet per frame
		
		// erase old cursor
		draw_cursor(prevMouseX2, prevMouseY2, 0x0000);

		// draw new cursor
		draw_cursor(mouseX, mouseY, 0xFFFF);

		// save position for next frame
		prevMouseX2 = prevMouseX1;
		prevMouseY2 = prevMouseY1;
		prevMouseX1 = mouseX;
		prevMouseY1 = mouseY;
		
		if (clicked) {
			
			//check if play button was clicked
			if (clickX > 149 && clickX < 169 && clickY > 5 && clickY < 16) {
				playActive = !playActive; //toggle on/off
			} else if (clickX > 169 && clickX < 184 && clickY > 5 && clickY < 16) {
				recordActive = !recordActive;
			}
		}
		
		wait_for_vsync(); // swap front and back buffers on VGA vertical sync
        pixel_buffer_start = *(pixel_ctrl_ptr + 1); // new back buffer
	}
}

//function implementations

//plots a pixel at any coordinate x, y
void plot_pixel(int x, int y, short int line_color){
    volatile short int *one_pixel_address;
        one_pixel_address = (short int*)(pixel_buffer_start + (y << 10) + (x << 1));
        *one_pixel_address = line_color;
}

//gets the color of pixel at any coordinate x, y
short int get_pixel(int x, int y) {
    volatile short int *one_pixel_address;
    one_pixel_address = (short int*)(pixel_buffer_start + (y << 10) + (x << 1));
    return *one_pixel_address;  // read
}

//draws the starting screen
void draw_main_screen(int playActive, int recordActive) {
	//draw top menu rectangle
	fill_area(0, 319, 0, 20, 0x39E7);
	
	//fill side menu rectangle
	fill_area(0, 20, 0, 239, 0x39E7);

	//fill in the record, play, skip to start background
	fill_area(134, 184, 5, 15, 0x2104);
	
	//record, play, skip to start outline (vertical)
	draw_line(149, 5, 149, 16, 0x39E7);
	draw_line(169, 5, 169, 16, 0x39E7);

	//play button
	if (playActive) {
		draw_play_button_green();
		fill_area(137, 145, 7, 13, 0xE71C);
	} else if (recordActive) {
		draw_play_button_green();
		fill_area(137, 145, 7, 13, 0xE71C);
		fill_area(170, 184, 5, 16, 0xFC10);
	} else {
		draw_play_button_gray();
		draw_skip_button();
	}
		
	//record button
	draw_circle(177, 10, 3, 0xF800);
	fill_shape(177 - 3, 10 - 3, 177 + 3, 10 + 3, 0xF800, 0xF800);

	//separating instruments
	draw_line(0, 29, 21, 29, 0x2104);
	draw_line(0, 49, 21, 49, 0x2104);
	
	//plus symbol for add instrument
	draw_line(7, 39, 14, 39, 0xBDF7);
	draw_line(10, 36, 10, 43, 0xBDF7);
	
	//bars
	fill_area(21, 319, 21, 28, 0x1082);
	
	//draw large ticks
	draw_line(59, 21, 59, 29, 0x7BEF);
	draw_line(96, 21, 96, 29, 0x7BEF);
	draw_line(133, 21, 133, 29, 0x7BEF);
	draw_line(170, 21, 170, 29, 0x7BEF);
	draw_line(207, 21, 207, 29, 0x7BEF);
	draw_line(244, 21, 244, 29, 0x7BEF);
	draw_line(281, 21, 281, 29, 0x7BEF);
	
	//draw numbers on the ticks
	draw_digit(1, 22, 22, 0x7BEF); 
	draw_digit(2, 61, 22, 0x7BEF); 
	draw_digit(3, 98, 22, 0x7BEF); 
	draw_digit(4, 135, 22, 0x7BEF); 
	draw_digit(5, 172, 22, 0x7BEF); 
	draw_digit(6, 209, 22, 0x7BEF); 
	draw_digit(7, 246, 22, 0x7BEF); 
	draw_digit(8, 283, 22, 0x7BEF); 
	
	//draw small ticks
	draw_line(30, 24, 30, 29, 0x39E7);
	draw_line(40, 24, 40, 29, 0x39E7);
	draw_line(50, 24, 50, 29, 0x39E7);
	
	draw_line(68, 24, 68, 29, 0x39E7);
	draw_line(77, 24, 77, 29, 0x39E7);
	draw_line(86, 24, 86, 29, 0x39E7);
	
	draw_line(105, 24, 105, 29, 0x39E7);
	draw_line(114, 24, 114, 29, 0x39E7);
	draw_line(123, 24, 123, 29, 0x39E7);

	draw_line(142, 24, 142, 29, 0x39E7);
	draw_line(151, 24, 151, 29, 0x39E7);
	draw_line(160, 24, 160, 29, 0x39E7);
	
	draw_line(179, 24, 179, 29, 0x39E7);
	draw_line(188, 24, 188, 29, 0x39E7);
	draw_line(197, 24, 197, 29, 0x39E7);
	
	draw_line(216, 24, 216, 29, 0x39E7);
	draw_line(225, 24, 225, 29, 0x39E7);
	draw_line(234, 24, 234, 29, 0x39E7);
	
	draw_line(253, 24, 253, 29, 0x39E7);
	draw_line(262, 24, 262, 29, 0x39E7);
	draw_line(271, 24, 271, 29, 0x39E7);
	
	draw_line(290, 24, 290, 29, 0x39E7);
	draw_line(299, 24, 299, 29, 0x39E7);
	draw_line(308, 24, 308, 29, 0x39E7);
	
	//major 8 bars
	draw_line(59, 29, 59, 239, 0x2104);
	draw_line(96, 29, 96, 239, 0x2104);
	draw_line(133, 29, 133, 239, 0x2104);
	draw_line(170, 29, 170, 239, 0x2104);
	draw_line(207, 29, 207, 239, 0x2104);
	draw_line(244, 29, 244, 239, 0x2104);
	draw_line(281, 29, 281, 239, 0x2104);
	
	//tiny 4 bars for each major 8 bars
	draw_line(30, 29, 30, 239, 0x1082);
	draw_line(40, 29, 40, 239, 0x1082);
	draw_line(50, 29, 50, 239, 0x1082);
	
	draw_line(68, 29, 68, 239, 0x1082);
	draw_line(77, 29, 77, 239, 0x1082);
	draw_line(86, 29, 86, 239, 0x1082);
	
	draw_line(105, 29, 105, 239, 0x1082);
	draw_line(114, 29, 114, 239, 0x1082);
	draw_line(123, 29, 123, 239, 0x1082);

	draw_line(142, 29, 142, 239, 0x1082);
	draw_line(151, 29, 151, 239, 0x1082);
	draw_line(160, 29, 160, 239, 0x1082);
	
	draw_line(179, 29, 179, 239, 0x1082);
	draw_line(188, 29, 188, 239, 0x1082);
	draw_line(197, 29, 197, 239, 0x1082);
	
	draw_line(216, 29, 216, 239, 0x1082);
	draw_line(225, 29, 225, 239, 0x1082);
	draw_line(234, 29, 234, 239, 0x1082);
	
	draw_line(253, 29, 253, 239, 0x1082);
	draw_line(262, 29, 262, 239, 0x1082);
	draw_line(271, 29, 271, 239, 0x1082);
	
	draw_line(290, 29, 290, 239, 0x1082);
	draw_line(299, 29, 299, 239, 0x1082);
	draw_line(308, 29, 308, 239, 0x1082);
}

//fills any rectangular area from point x0, y0 to x1, y1
void fill_area (int x0, int x1, int y0, int y1, short int fill_color) {
	for (int i = x0; i <= x1; i++) {
		for (int j = y0; j <= y1; j++) {
			plot_pixel(i, j, fill_color);
		}
	}
}

//fills any convex polygon
void fill_shape(int minX, int minY, int maxX, int maxY, short int fill_color, short int edge_color) {
    for (int y = minY; y <= maxY; y++) {
        int xLeft = maxX, xRight = minX;
        
        for (int x = minX; x <= maxX; x++) {
            if (get_pixel(x, y) == edge_color) {
                if (x < xLeft)  xLeft = x;
                if (x > xRight) xRight = x;
            }
        }
        
        for (int x = xLeft; x <= xRight; x++) {
            plot_pixel(x, y, fill_color);
        }
    }
}

void draw_line (int x0, int y0, int x1, int y1, short int line_color) {
	
	//determine if the line is steep
	//steep line changes more in y than x
	bool is_steep = abs(y1 - y0) > abs(x1 - x0);
	
	//swap x and y coordinates to iterate over the longer axis if steep
	if (is_steep) {
		swap (&x0, &y0);
		swap (&x1, &y1);
	}
	
	//make sure we always draw left to right
	if (x0 > x1) {
		swap (&x0, &x1);
		swap (&y0, &y1);
	}
	
	int y_step = 0;
	int deltax = x1 - x0; 		//total horizontal distance
	int deltay = abs(y1 - y0);	//total vertical distance 
	int error = -(deltax/2);	//error accumulates, is centered at -(deltax/2)
	int y = y0;					//current y position starts at y0
	
	//check if y increases or decreases as we step through x
	if (y0 < y1) {
		y_step = 1;
	} else {
		y_step = -1;
	}
	
	//step through every x pixel from x0 to x1
	for (int x = x0; x < x1; x++) {
		//if the line is steep, unswap x and y
		if (is_steep) {
			plot_pixel (y, x, line_color);
		} else {
			plot_pixel (x, y, line_color);
		}
		
		//accumulate vertical error
		error = error + deltay;
		
		//if error is positive, we can increment why
		if (error > 0) {
			y = y + y_step;
			error = error - deltax; //reset error
		}
	} 	
}

void draw_circle(int cx, int cy, int radius, short int color) {
    int x = 0;
    int y = radius;
    int d = 1 - radius;  // decision variable
    
    // plot all 8 symmetric points of the circle
    while (x <= y) {
        plot_pixel(cx + x, cy + y, color);
        plot_pixel(cx - x, cy + y, color);
        plot_pixel(cx + x, cy - y, color);
        plot_pixel(cx - x, cy - y, color);
        plot_pixel(cx + y, cy + x, color);
        plot_pixel(cx - y, cy + x, color);
        plot_pixel(cx + y, cy - x, color);
        plot_pixel(cx - y, cy - x, color);
        
        if (d < 0) {
            d = d + 2 * x + 3;  // move right
        } else {
            d = d + 2 * (x - y) + 5;  // move right and down
            y--;
        }
        x++;
    }
}

void draw_cursor(int x, int y, short int color) {
    // draw a small plus/cross shape
	if (x + 1 < 320 && y < 240) {
	    plot_pixel(x, y, color);
		plot_pixel(x + 1, y, color);
		plot_pixel(x - 1, y, color);
		plot_pixel(x, y + 1, color);
		plot_pixel(x, y - 1, color);
	}
}

void draw_digit(int digit, int x, int y, short int color) {
    // each digit is 5 pixels tall and 3 pixels wide
    // 1 = draw, 0 = skip
    // each row is represented as a bitmask of 3 bits (bits 2,1,0 = left,mid,right)
    
    int digits[10][5] = {
        {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
        {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
        {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
        {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
        {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
        {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
        {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
        {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
        {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
        {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
    };
    
    for (int row = 0; row < 5; row++) {
        int mask = digits[digit][row];
        if (mask & 0b100) plot_pixel(x,     y + row, color); // left
        if (mask & 0b010) plot_pixel(x + 1, y + row, color); // middle
        if (mask & 0b001) plot_pixel(x + 2, y + row, color); // right
    }
}

void draw_play_button_green() {
	fill_area(150, 168, 5, 16, 0x03E0);
	
	draw_line(163, 10, 156, 7, 0x3FE0);
	draw_line(156, 7, 156, 13, 0x3FE0);
	draw_line(156, 13, 163, 10, 0x3FE0);
	fill_shape(156, 7, 163, 13, 0x3FE0, 0x3FE0);
}

void draw_play_button_gray() {
	draw_line(163, 10, 156, 7, 0xE71C);
	draw_line(156, 7, 156, 13, 0xE71C);
	draw_line(156, 13, 163, 10, 0xE71C);
	fill_shape(156, 7, 163, 13, 0xE71C, 0xE71C);
}

void draw_skip_button() {
	draw_line(138, 10, 145, 7, 0xE71C);
	draw_line(145, 7, 145, 14, 0xE71C);
	draw_line(145, 13, 138, 10, 0xE71C);
	fill_shape(138, 7, 145, 13, 0xE71C, 0xE71C);
	fill_area(137, 138, 7, 13, 0xE71C);
}

//swaps the value of two integers
void swap (int* x, int* y) {
	int temp = *x;
	*x = *y;
	*y = temp;
}


void clear_screen() {
	for (int i = 0; i < 320; i++) {
		for (int j = 0; j < 240; j++) {
			plot_pixel(i, j, 0x0841);
		}
	}
}

void wait_for_vsync() {
	 
	volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
	//synchronize the VGA
	//write 1 into the buffer register
	*pixel_ctrl_ptr = 1;
	//get pointer to status register
	volatile int * status_reg = pixel_ctrl_ptr + 3;
	//wait to clear and draw the next line till the S bit in the 
	//status register becomes 1
	while ((*status_reg & 0x1) != 0);
	
}

//mouse control functions

// PS/2 mouse sends 3 bytes per event:
// byte 1: status byte (buttons + overflow flags)
// byte 2: x movement
// byte 3: y movement

void init_mouse() {
    volatile int *ps2_ptr = (int *)PS2_BASE;
    // send 0xFF to reset the mouse
    *(ps2_ptr + 1) = 0xFF;
    // send 0xF4 to enable data reporting
    *(ps2_ptr + 1) = 0xF4;
}

int read_ps2_byte() {
    volatile int *ps2_ptr = (int *)PS2_BASE;
    int data;
    // wait until RVALID bit (bit 15) is set
    do {
        data = *ps2_ptr;
    } while ((data & 0x8000) == 0);
    return data & 0xFF;
}

void clear_ps2() {
    volatile int *ps2_ptr = (int *)PS2_BASE;
    // drain any leftover bytes in the FIFO
    while (*ps2_ptr & 0x8000);
}

void read_mouse(int *clicked, int *clickX, int *clickY) {
    *clicked = 0;
    volatile int *ps2_ptr = (int *)PS2_BASE;
    int data, byte1, byte2, byte3;

    while (1) {
        // check if byte1 is available
        data = *ps2_ptr;
        if ((data & 0x8000) == 0) break;  // no data at all, exit
        byte1 = data & 0xFF;

        // wait for byte2 — must complete the packet
        do { data = *ps2_ptr; } while ((data & 0x8000) == 0);
        byte2 = data & 0xFF;

        // wait for byte3
        do { data = *ps2_ptr; } while ((data & 0x8000) == 0);
        byte3 = data & 0xFF;

        // sign extend correctly
        int dx = (int)(signed char)byte2;
        int dy = (int)(signed char)byte3;

        dx = dx / 5;
        dy = dy / 5;

        mouseX += dx;
        mouseY += dy;

        if (mouseX < 0)   mouseX = 0;
        if (mouseX > 319) mouseX = 319;
        if (mouseY < 0)   mouseY = 0;
        if (mouseY > 239) mouseY = 239;

        if (byte1 & 0x01) {
            *clicked = 1;
            *clickX = mouseX;
            *clickY = mouseY;
        }
    }
}
