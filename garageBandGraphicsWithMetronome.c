//------------------- LIBRARIES -------------------
#include <stdlib.h>
#include <stdbool.h>

//---------------- GLOBAL CONSTANTS ---------------
#define PS2_BASE 		0xFF200100
#define FRAME_BASE 		0xFF203020

#define AUDIO_BASE        0xFF203040
#define AUDIO_CTRL        (*(volatile int *)(AUDIO_BASE + 0x0))
#define AUDIO_FIFOSPACE   (*(volatile int *)(AUDIO_BASE + 0x4))
#define AUDIO_LEFT        (*(volatile int *)(AUDIO_BASE + 0x8))
#define AUDIO_RIGHT       (*(volatile int *)(AUDIO_BASE + 0xC))
#define METRONOME_NUM_SAMPLES 19798
	

#define TIMER_FREQ 		100000000 //100 MHz
#define TIMER_BASE 		0xFF202000
#define TIMER_STATUS  	(*(volatile int *)(TIMER_BASE))
#define TIMER_CTRL  	(*(volatile int *)(TIMER_BASE + 0x4))
#define TIMER_LOW   	(*(volatile int *)(TIMER_BASE + 0x8))
#define TIMER_HIGH 		(*(volatile int *)(TIMER_BASE + 0xC))
#define TIMER_SNAP_LOW	(*(volatile int *)(TIMER_BASE + 0x10))
#define TIMER_SNAP_HIGH	(*(volatile int *)(TIMER_BASE + 0x14))

//---------------- GLOBAL VARIABLES ---------------
volatile int pixel_buffer_start;

short int Buffer1[240][512]; // 240 rows, 512 (320 + padding) columns
short int Buffer2[240][512];

// mouse position tracked globally
int mouseX = 160;  // start in center of screen
int mouseY = 120;

//metronome sample
const int metronome_sample[];

int beats_per_minute = 120;

//separator with plus sign
int new_instrument_location_y1 = 29;
int new_instrument_location_y2 = 49;

//plus sign symbol location
int add_instrument_plus_y0 = 39; //horizontal line

int add_instrument_plus_y1 = 36; //for vertical line
int add_instrument_plus_y2 = 43;

//location of the instrument separators 
int instrument_separator_positions[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

//instrument type per slot
int instrument_types[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

int instrument_count = 0;

//locations of each instrument label in y coordinates
int instrument_label_positions[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

static const int characters[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x00}, // A
  	{0x1E,0x11,0x11,0x1E,0x11,0x1E,0x00}, // B
    {0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x1E,0x00}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x1F,0x00}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x00}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x0F,0x00}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x00}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x0E,0x00}, // I
    {0x07,0x02,0x02,0x02,0x12,0x0C,0x00}, // J
    {0x11,0x12,0x14,0x18,0x14,0x13,0x00}, // K
    {0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, // L
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x00}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x00}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x00}, // P
    {0x0E,0x11,0x11,0x15,0x12,0x0D,0x00}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x13,0x00}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x1E,0x00}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x00}, // T
    {0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x04,0x00}, // V
    {0x11,0x11,0x15,0x15,0x1B,0x11,0x00}, // W
    {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x00}, // Y
    {0x1F,0x02,0x04,0x08,0x10,0x1F,0x00}, // Z
};


static const int digits[10][5] = {
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
    

//------------- FUNCTION DECLARATIONS -------------
void plot_pixel(int x, int y, short int line_color);
short int get_pixel(int x, int y);

void draw_line (int x0, int y0, int x1, int y1, short int line_color);
void draw_circle(int cx, int cy, int radius, short int color);
void draw_digit(int digit, int x, int y, short int color);
void draw_char(char ch, int x, int y, int scale, short colour);
void draw_play_button_green();
void draw_play_button_gray();
void draw_skip_button();
void fill_area (int x0, int x1, int y0, int y1, short int fill_color);
void fill_shape(int minX, int minY, int maxX, int maxY, short int fill_color, short int edge_color);

void draw_main_screen(int playActive, int recordActive, int chooseActive);
void clear_screen();
void wait_for_vsync();

void init_mouse();
int read_ps2_byte();
void clear_ps2();
void read_mouse(int *clicked, int *clickX, int *clickY);
void draw_cursor(int x, int y, short int color);
	
void swap (int* x, int* y);
void play_audio();
double find_seconds_per_bar(int bpm);
double find_bpm_for_small_tick(int bpm);
void count_bars(int bpm, int bars, int continuous, int *stop);

void add_instrument();
void remove_instrument();
void choose_instrument(); 
void choose_piano();
void choose_snare();
void choose_hihat();
void choose_bass();
void choose_vocal();

//---------- MAIN FUNCTION -----------
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
		
	//toggle states
	int playActive = 0;
	int recordActive = 0;
	int chooseActive = 0;
	
	int stop = 0;
	int *stop_ptr = &stop;

	//draw main screen once
	draw_main_screen(playActive, recordActive, chooseActive);
    draw_cursor(mouseX, mouseY, 0xFFFF);
    wait_for_vsync(); 
    pixel_buffer_start = *(pixel_ctrl_ptr + 1); 

	//initialize mouse position
	init_mouse();
	/*
	int prevMouseX1 = 160, prevMouseY1 = 120;  // 1 frame ago
	int prevMouseX2 = 160, prevMouseY2 = 120;  // 2 frames ago
	*/
	clear_ps2();

	while (1) {
		// erase old cursor
		//draw_cursor(prevMouseX2, prevMouseY2, 0x0000);
		
		draw_main_screen(playActive, recordActive, chooseActive);
		
		int clicked = 0, clickX = 0, clickY = 0;
		read_mouse(&clicked, &clickX, &clickY);  // just one packet per frame
	
		// draw new cursor
		draw_cursor(mouseX, mouseY, 0xFFFF);

		/*// save position for next frame
		prevMouseX2 = prevMouseX1;
		prevMouseY2 = prevMouseY1;
		prevMouseX1 = mouseX;
		prevMouseY1 = mouseY;*/
		if (chooseActive) {
			//check if piano was clicked
			if (clickX > 108 && clickX < 158 && clickY > 74 && clickY < 123) {
				chooseActive = !chooseActive;
				choose_piano();
			//check if vocal was clicked
			} else if (clickX > 161 && clickX < 211 && clickY > 74 && clickY < 123) {
				chooseActive = !chooseActive;
				choose_vocal();
			//check if hi hat was clicked
			} else if (clickX > 82 && clickX < 132 && clickY > 129 && clickY < 178) {
				chooseActive = !chooseActive;
				choose_hihat();
			//check if snare was clicked
			} else if (clickX > 135 && clickX < 185 && clickY > 129 && clickY < 178) {
				chooseActive = !chooseActive;
				choose_snare();
			//check if bass was clicked
			} else if (clickX > 188 && clickX < 238 && clickY > 129 && clickY < 178) {
				chooseActive = !chooseActive;
				choose_bass();
			}
		}

		if (clicked) {
			
			//check if play button was clicked
			if (clickX > 149 && clickX < 169 && clickY > 5 && clickY < 16) {
				playActive = !playActive; //toggle on/off
				
			//check if record button was clicked
			} else if (clickX > 169 && clickX < 184 && clickY > 5 && clickY < 16) {
				//toggle on
				recordActive = !recordActive;
				
				//draw active record button
				draw_main_screen(playActive, recordActive, chooseActive);
				pixel_buffer_start = *(pixel_ctrl_ptr + 1);
				wait_for_vsync();
				
				//play the metronome
				play_audio(metronome_sample, METRONOME_NUM_SAMPLES);
				
				//wait for 8 bars after the metronome
				count_bars(beats_per_minute, 12, 0, stop_ptr);
				
				//toggle off
				recordActive = !recordActive;
				
			//check if add instrument (plus sign) was clicked
			} else if (clickX > 0 && clickX < 21 && clickY > new_instrument_location_y1 && clickY < new_instrument_location_y2) {	
				chooseActive = !chooseActive;
			}
		}
					   
		wait_for_vsync(); // swap front and back buffers on VGA vertical sync
        pixel_buffer_start = *(pixel_ctrl_ptr + 1); // new back buffer
	}
}

//------------- FUNCTION IMPLEMENTATIONS -------------

void add_instrument() {
	if ((new_instrument_location_y2 + 20) > 240) {return;}
		
	int temp = new_instrument_location_y2;
	new_instrument_location_y2 += 20;
	new_instrument_location_y1 = temp;
	
	//horizontal line
	add_instrument_plus_y0 = new_instrument_location_y1 + 10;
	
	//for vertical line
	add_instrument_plus_y1 = new_instrument_location_y1 + 7; 
	add_instrument_plus_y2 = new_instrument_location_y1 + 14;
}

void remove_instrument() {
	if ((new_instrument_location_y1 - 20) < 29) {return;}

	int temp = new_instrument_location_y1;
	new_instrument_location_y1 -= 20;
	new_instrument_location_y2 = temp;
	
	//horizontal line
	add_instrument_plus_y0 = new_instrument_location_y2 - 10;
	
	//for vertical line
	add_instrument_plus_y1 = new_instrument_location_y2 - 13; 
	add_instrument_plus_y2 = new_instrument_location_y2 - 6;
}
					
void choose_instrument() {
	fill_area(70, 250, 50, 190, 0x1082);
	fill_area(76, 244, 68, 184, 0x2104);
	
	//choose
	draw_char('C', 113, 56, 1, 0xFFFF);
	draw_char('H', 119, 56, 1, 0xFFFF);
	draw_char('O', 125, 56, 1, 0xFFFF);
	draw_char('O', 131, 56, 1, 0xFFFF);
	draw_char('S', 137, 56, 1, 0xFFFF);
	draw_char('E', 143, 56, 1, 0xFFFF);
	
	//instrument
	draw_char('I', 153, 56, 1, 0xFFFF);
	draw_char('N', 159, 56, 1, 0xFFFF);
	draw_char('S', 165, 56, 1, 0xFFFF);
	draw_char('T', 171, 56, 1, 0xFFFF);
	draw_char('R', 177, 56, 1, 0xFFFF);
	draw_char('U', 183, 56, 1, 0xFFFF);
	draw_char('M', 189, 56, 1, 0xFFFF);
	draw_char('E', 195, 56, 1, 0xFFFF);
	draw_char('N', 201, 56, 1, 0xFFFF);
	draw_char('T', 207, 56, 1, 0xFFFF);
	
	//first row
	//piano background
	fill_area(108, 158, 74, 123, 0x1082);
	fill_area(110, 156, 76, 105, 0x2104);
	
	draw_char('P', 119, 112, 1, 0xFFFF);
	draw_char('I', 125, 112, 1, 0xFFFF);
	draw_char('A', 131, 112, 1, 0xFFFF);
	draw_char('N', 137, 112, 1, 0xFFFF);
	draw_char('O', 143, 112, 1, 0xFFFF);
	
	//vocal background
	fill_area(161, 211, 74, 123, 0x1082);
	fill_area(163, 209, 76, 105, 0x2104);
	
	draw_char('V', 172, 112, 1, 0xFFFF);
	draw_char('O', 178, 112, 1, 0xFFFF);
	draw_char('C', 184, 112, 1, 0xFFFF);
	draw_char('A', 190, 112, 1, 0xFFFF);
	draw_char('L', 196, 112, 1, 0xFFFF);
	
	//second row
	//hi hat background
	fill_area(82, 132, 129, 178, 0x1082);
	fill_area(84, 130, 131, 160, 0x2104);
	
	draw_char('H', 93, 167, 1, 0xFFFF);
	draw_char('I', 99, 167, 1, 0xFFFF);
	draw_char('H', 105, 167, 1, 0xFFFF);
	draw_char('A', 111, 167, 1, 0xFFFF);
	draw_char('T', 117, 167, 1, 0xFFFF);
	
	//snare background
	fill_area(135, 185, 129, 178, 0x1082);
	fill_area(137, 183, 131, 160, 0x2104);
	
	draw_char('S', 146, 167, 1, 0xFFFF);
	draw_char('N', 152, 167, 1, 0xFFFF);
	draw_char('A', 158, 167, 1, 0xFFFF);
	draw_char('R', 164, 167, 1, 0xFFFF);
	draw_char('E', 170, 167, 1, 0xFFFF);
	
	//bass background
	fill_area(188, 238, 129, 178, 0x1082);
	fill_area(190, 236, 131, 160, 0x2104);
	
	draw_char('B', 202, 167, 1, 0xFFFF);
	draw_char('A', 208, 167, 1, 0xFFFF);
	draw_char('S', 214, 167, 1, 0xFFFF);
	draw_char('S', 220, 167, 1, 0xFFFF);
}

void choose_piano() {
    add_instrument();
    if (instrument_count < 9) {
        instrument_types[instrument_count] = 1;
    }
	
	instrument_separator_positions[instrument_count] = new_instrument_location_y1 - 20;
	instrument_label_positions[instrument_count] = new_instrument_location_y1 - 12;
	
    instrument_count++;
}

void choose_vocal() {
    add_instrument();
    if (instrument_count < 9) {
        instrument_types[instrument_count] = 2;
    }
	
	instrument_separator_positions[instrument_count] = new_instrument_location_y1 - 20;
	instrument_label_positions[instrument_count] = new_instrument_location_y1 - 12;
	
	instrument_count++;
}

void choose_hihat() {
    add_instrument();
    if (instrument_count < 9) {
        instrument_types[instrument_count] = 3;
    }
	
	instrument_separator_positions[instrument_count] = new_instrument_location_y1 - 20;
	instrument_label_positions[instrument_count] = new_instrument_location_y1 - 12;
	
    instrument_count++;
}

void choose_snare() {
    add_instrument();
    if (instrument_count < 9) {
        instrument_types[instrument_count] = 4;
    }
	
	instrument_separator_positions[instrument_count] = new_instrument_location_y1 - 20;
	instrument_label_positions[instrument_count] = new_instrument_location_y1 - 12;
	
    instrument_count++;
}

void choose_bass(){
    add_instrument();
    if (instrument_count < 9) {
        instrument_types[instrument_count] = 5;
    }
	
	instrument_separator_positions[instrument_count] = new_instrument_location_y1 - 20;
	instrument_label_positions[instrument_count] = new_instrument_location_y1 - 12;
	
    instrument_count++;
}

void draw_instrument_label(int type, int y) {
    char letter;
    switch (type) {
        case 1: letter = 'P'; break;  // piano
        case 2: letter = 'V'; break;  // vocal
        case 3: letter = 'H'; break;  // hihat
        case 4: letter = 'S'; break;  // snare
        case 5: letter = 'B'; break;  // bass
        default: return;
    }
    draw_char(letter, 8, y, 1, 0xFFFF);
}

double find_seconds_per_bar(int bpm) {
	return 60.0 /bpm;
}

double find_bpm_for_small_tick(int bpm) {
	return bpm * 4;
}

void count_bars(int bpm, int bars, int continuous, int *stop) {
    // calculate total seconds for the bars
    double seconds = bars * find_seconds_per_bar(bpm);
    unsigned int counter_start_value = (unsigned int)(TIMER_FREQ * seconds);

    // stop timer first
    TIMER_CTRL = 0;

    // clear TO bit
    TIMER_STATUS = 0;

    // split 32-bit start value into low/high 16-bit registers
    int counter_low  = counter_start_value & 0xFFFF;
    int counter_high = (counter_start_value >> 16) & 0xFFFF;

    TIMER_LOW  = counter_low;
    TIMER_HIGH = counter_high;

    // control bits
    int begin = 0b100; // single-shot
    if (continuous == 1) {
        begin = 0b110; // start + continuous
    }

    // start timer
    TIMER_CTRL = begin;

    if (continuous == 1) {
        // keep running until stop is set
        while (*stop == 0) {
            // poll TO bit
            if ((TIMER_STATUS & 0x1) != 0) {
                TIMER_STATUS = 0; // clear TO if counter is done
            }
        }
        TIMER_CTRL = 0; // stop timer
    } else {
        // single-shot: wait until done
        while ((TIMER_STATUS & 0x1) == 0);
        TIMER_STATUS = 0;
    }
}

//plays any audio given
void play_audio(const int audio_sample[], int num_of_samples) {
	//play audio
    for (int i = 0; i < num_of_samples; i++) {
        while (((AUDIO_FIFOSPACE >> 16) & 0xFF) == 0);
        AUDIO_LEFT  = (int)audio_sample[i];
        AUDIO_RIGHT = (int)audio_sample[i];
    }
}

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
void draw_main_screen(int playActive, int recordActive, int chooseActive) {
	//draw top menu rectangle
	clear_screen();
	fill_area(0, 319, 0, 20, 0x39E7);
	
	//fill side menu rectangle
	fill_area(0, 20, 0, 239, 0x39E7);
	
	//border 
	draw_line(0, 0, 319, 0, 0x2104);
	draw_line(0, 239, 319, 239, 0x2104);
	draw_line(0, 0, 0, 239, 0x2104);
	draw_line(319, 0, 319, 239, 0x2104);

	//fill in the record, play, skip to start background
	fill_area(134, 184, 5, 15, 0x2104);
	
	//record, play, skip to start outline (vertical)
	draw_line(149, 5, 149, 16, 0x39E7);
	draw_line(169, 5, 169, 16, 0x39E7);
		
	//record button
	draw_circle(177, 10, 3, 0xF800);
	fill_shape(177 - 3, 10 - 3, 177 + 3, 10 + 3, 0xF800, 0xF800);

	//separating instruments
	draw_line(0, new_instrument_location_y1, 21, new_instrument_location_y1, 0x2104);
	draw_line(0, new_instrument_location_y2, 21, new_instrument_location_y2, 0x2104);
	
	//nine instruments not yet drawn
	for (int i = 0; i < instrument_count; i++) {
    	draw_line(0, instrument_separator_positions[i], 21, instrument_separator_positions[i], 0x2104);
	}
	
	for (int i = 0; i < instrument_count; i++) {
    	draw_instrument_label(instrument_types[i], instrument_label_positions[i]);
	}
	
	//plus symbol for add instrument
	draw_line(7, add_instrument_plus_y0, 14, add_instrument_plus_y0, 0xBDF7); //horizontal line
	draw_line(10, add_instrument_plus_y1, 10, add_instrument_plus_y2, 0xBDF7); //vertical line
	
	//fill in main playing area
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
	
	draw_play_button_gray();
	draw_skip_button();
	
	//toggle states
	if (playActive) {
		draw_play_button_green();
		fill_area(137, 145, 7, 13, 0xE71C);
	} else if (recordActive) {
		draw_play_button_green();
		fill_area(137, 145, 7, 13, 0xE71C);
		fill_area(170, 184, 5, 16, 0xFC10);
		draw_circle(177, 10, 3, 0xF800);
		fill_shape(177 - 3, 10 - 3, 177 + 3, 10 + 3, 0xF800, 0xF800);
	} else if (chooseActive) {
		choose_instrument();
	} 
	
	for (int i = 0; i < instrument_count; i++) {
		draw_instrument_label(instrument_types[i], instrument_label_positions[i]);
	}
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
    
    for (int row = 0; row < 5; row++) {
        int mask = digits[digit][row];
        if (mask & 0b100) plot_pixel(x,     y + row, color); // left
        if (mask & 0b010) plot_pixel(x + 1, y + row, color); // middle
        if (mask & 0b001) plot_pixel(x + 2, y + row, color); // right
    }
}

void draw_char(char ch, int x, int y, int scale, short colour) {
    if (ch < 'A' || ch > 'Z') return;

    int row, col, dx, dy;
    for (row = 0; row < 7; row++) {
        int byte = characters[ch - 'A'][row];
        for (col = 0; col < 5; col++) {
            if (byte & (0x10 >> col)) {
                for (dy = 0; dy < scale; dy++)
                    for (dx = 0; dx < scale; dx++)
                        plot_pixel(x + col*scale + dx,
                                   y + row*scale + dy,
                                   colour);
            }
        }
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

    clear_ps2();

    // Send reset
    *ps2_ptr = 0xFF;

    clear_ps2();

    // Enable data reporting
    *ps2_ptr = 0xF4;

    // Drain ack
    clear_ps2();
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
    // keep reading while RVALID is set
    while (*ps2_ptr & 0x8000)
        (void)*ps2_ptr;  // consume the byte
}

void read_mouse(int *clicked, int *clickX, int *clickY) {
    *clicked = 0;
    volatile int *ps2_ptr = (int *)PS2_BASE;
    int data, byte1, byte2, byte3;
	
    while (1) {
        // check if byte1 is available
        data = *ps2_ptr;
        if ((data & 0x8000) == 0) return;  // no data at all, exit

        byte1 = data & 0xFF;
        if(!(byte1 & 0x08)) return;

        // wait for byte2 — must complete the packet
        do { data = *ps2_ptr; } while ((data & 0x8000) == 0);
        byte2 = data & 0xFF;

        // wait for byte3
        do { data = *ps2_ptr; } while ((data & 0x8000) == 0);
        byte3 = data & 0xFF;

        // sign extend correctly
        int dx = (int)(signed char)byte2;
        int dy = (int)(signed char)byte3;

        if (byte1 & 0x40) dx = 0;
        if (byte1 & 0x80) dy = 0;

        dx = dx / 5;
        dy = dy / 5;

        mouseX += dx;
        mouseY -= dy;

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
