/*
 *	Serial Mosiac Code
 *	Written by Andrew Louie Nov 9, 2018
 *
 *	System Specific Alterations:
 *		(unix style code was used to reduce platform limitations)
 *		Windows - nothing
 *		POSIX - comment out dirent.h & any time headers
 *
 *	Requirements:
 *		- 24bit color depth BMP files ONLY
 *			BulkImageConverter.exe works well on Windows to convert images to BMP
 *
 *	Usage:
 *		1) Set DEBUG VARIABLES
 *		2) Set USER DEFINABLE VARIABLES
 *		3) Set FILES
 *		4) Run the program and wait for an output image
 *
 *	Notes:
 *		- BMP files can be very very large (1.3GB for a 30,000 x 20,000 image)
 *		- BMP file resolution caps out for specific programs (30,000 pixels max for Windows Photo viewer)
 *
 *		- ASP_RATIO_ERR may need to be adjusted for diff reference files
 *		- TILE_RPT_COUNT & TILE_MIN_DIST can be hardcoded, choose wisely
 *			adjusting these values have a STRONG effect on performance
 *
 *		- Extremely large number of tiles (20,000+) will take 20+ minutes to run
 *		- Max tested tiles: 250,000 (83m 20s)
 *		- Good representation for minimal wait time: 10,000 - 40,000 tiles
 *		- Your reference image affects visual representation a lot
 *
 *		- Memory use for very large mosaics can be taxing (250,000 tiles used 8GB of RAM)
 *
 *	How it works:
 *		1) Component Metadata:	Tile (component) images are read
 *		2) Mosaic Metadata:		Mosaic (output) image canvas is set up
 *		3) Tile Ranking:		Each tile in the mosaic ranks each tile image by RootMeanSquare RGB distance
 *		4) Tile Fitting:		Each tile selects a best tile while checking repeat conditions
 *		5) Mosaic Write:		The mosaic is written to file
 *
 */

// Reference code used:
// https://williamedwardscoder.tumblr.com/post/84505278488/making-image-mosaics
// https://github.com/tronkko/dirent/blob/master/examples/scandir.c


#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "bitmap_image.hpp"
#include "mosaic.h"

// these defines must change on a posix system
// change the quotes to angle brackets on unix
#include "dirent.h"
#include "time.h"
#include "systimes.h"

#if 0
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#endif

using namespace std;

/***********************************************/
/*************** DEBUG VARIABLES ***************/
/***********************************************/

/*
 * DEBUG PRINT LEVEL
 * 0: Critical Errors
 * 1: State + Summary
 * 2: Verbose
 */
#define DEBUG 1

/* 
 * SHOW TIMESTEPS
 * 0: Off
 * 1: On
 */
#define TIMESTEPS 1

 /*
/* TEST LEVEL
 * 0: Output mosaic image, no testing code
 * 1: Output image weight to an image, no mosaic
 */
#define TEST 0

// Enable filtering to match original image color
#define FILTER 1

/***********************************************/
/*************** USER DEFINABLE VARIABLES ******/
/***********************************************/
// aspect ratio error
// the final image is slightly cropped to make life easier
// this value defines how much may be cropped
// rotunda 0.01, tiger = 0.05, rainier = 0.05
float ASP_RATIO_ERR = 0.01;

// number of tiles per ROW and COL
int TILE_LDA = 40;

// number of times we can repeat an image
// a low number will result in no image or VERY slow code
// the total # of titles is TILE_RPT_COUNT * N_COMPONENT_IMAGES
int TILE_RPT_COUNT = 5;

// minimum distance between same images
// a high value will result in VERY slow code
int TILE_MIN_DIST = 10;

// filter strength
// current tile's original image RGB contribution (gives our mosaics color)
float FILTER_PERCENT = 0.5;

/*************************************/
/*************** FILES ***************/
/*************************************/

// Reference file
const string FILE_REF = "_rotunda.bmp";

// Output file
const string FILE_OUT = "mosaic.bmp";

// Source directory of tile images
const char DIR_IMG_PATH[] = "img60_2249";


/***********************************************/
/*************** COMPONENT IMAGE DATA **********/
/***********************************************/

// min width and height resolution in a given image set
unsigned int cmp_img_min_width = UINT_MAX;
unsigned int cmp_img_min_height = UINT_MAX;

// list of tile image metadata
vector <component_metadata> components;
int components_size;

// sorted list of component images per tile by how much they are preferred
vector <vector<mosaic_map>> tile_map;


/***********************************************/
/****************** MOSAIC DATA ****************/
/***********************************************/
int TOTAL_TILES = TILE_LDA * TILE_LDA;

// mosaic info
mosaic_metadata mosaic;

// mosaic image mapping of tile images
// when parallelized, this needs to have a lock on it
vector <mosaic_tile> tiles;


/***********************************************/
/*************** UTILITY FUNCTIONS *************/
/***********************************************/

inline int min_n(int a, int b) { return a < b ? a : b; };
inline int max_n(int a, int b) { return a > b ? a : b; };

// debug print function
// debug_level: level of print
// text: text to be printed, newlines already included
void dbgprint(int debug_level, string text)
{
	if (debug_level <= DEBUG)	
		cout <<  text << '\n';
}

//
//  timer
//
double read_timer()
{
	static bool initialized = false;
	static struct timeval start;
	struct timeval end;
	if (!initialized)
	{
		gettimeofday(&start, NULL);
		initialized = true;
	}
	gettimeofday(&end, NULL);
	return (end.tv_sec - start.tv_sec) + 1.0e-6 * (end.tv_usec - start.tv_usec);
}

/***********************************************/
/*************** FILE OUTPUT FUNCTIONS *********/
/***********************************************/

// set up bmp output file
// filestream: open FILE handler to be written to
// returns 0 for success, 1 for failure
int write_bmp_template()
{
	bitmap_image image(mosaic.width, mosaic.height);
	image.clear();

	string output = "\n\nStart BMP Template  " + to_string(mosaic.width) + " x " + to_string(mosaic.height);
	dbgprint(1, output);

#if !TEST
	image.set_region(0, 0, mosaic.width, mosaic.height, e_black);
#endif
	// code in the TEST section below:
	// writes the average weight only,
	// good for checking if weights are calculated correctly
#if TEST
	for (size_t y = 0; y < mosaic.height; y++)
	{
		for (size_t x = 0; x < mosaic.width; x++)
		{
			// write the weight to this tile (just for debugging)
			for(int n = 0; n < components_size; n++)
				if ((x >= tiles[n].start_x && y >= tiles[n].start_y) && (x <= tiles[n].end_x && y <= tiles[n].end_y))
					image.set_pixel(x, y, tiles[n].rgb);
			if (x == 0)
				printf("XY %d %d\n", x, y);
		}
	}
#endif

	image.save_image(FILE_OUT);

	output = "Done BMP Template  " + to_string(mosaic.width) + " x " + to_string(mosaic.height);
	dbgprint(1, output);
	return 0;
}

// write pixel data to a file
// tile_index: location to be written
// VERY VERY VERY slow due to constantly loading the entire final_img (which can be huge)
int write_component_img(int tile_index)
{
	mosaic_tile cur_tile = tiles[tile_index];
	bitmap_image final_img(FILE_OUT);
	bitmap_image tile_img(components[cur_tile.img_index].path);
	bitmap_image buffer;
	string output = "Placing tile " + to_string(tile_index);
	dbgprint(2, output);

	// write only the region specified by tile_index
	tile_img.region(0, 0, mosaic.cmp_width, mosaic.cmp_height, buffer);
	final_img.copy_from(buffer, cur_tile.start_x, cur_tile.start_y);

	final_img.save_image(FILE_OUT);

	return 0;
}

// writes the full image to a file
// very fast due to only loading the final_img once
int write_full_img() 
{
	bitmap_image final_img(FILE_OUT);
	// write all the regions one by one
	for (int tile_index = 0; tile_index < TOTAL_TILES; tile_index++) {
		mosaic_tile cur_tile = tiles[tile_index];
		bitmap_image tile_img(components[cur_tile.img_index].path);
		bitmap_image buffer;

		string output = "Placing tile " + to_string(tile_index);
		dbgprint(2, output);

		if (FILTER) {
			for (size_t y = 0; y < mosaic.cmp_height; y++)
			{
				for (size_t x = 0; x < mosaic.cmp_width; x++)
				{
					rgb_t rgb;
					tile_img.get_pixel(x, y, rgb);

					// avg the colors to add a color filter (better matches original super pixel
					if (cur_tile.rgb.red > cur_tile.rgb.green && cur_tile.rgb.red > cur_tile.rgb.blue)
						rgb.red += FILTER_PERCENT * (float)(cur_tile.rgb.red - rgb.red);
					else if (cur_tile.rgb.green > cur_tile.rgb.red && cur_tile.rgb.green > cur_tile.rgb.blue)
						rgb.green += FILTER_PERCENT * (float)(cur_tile.rgb.green - rgb.green);
					else if (cur_tile.rgb.blue > cur_tile.rgb.red && cur_tile.rgb.blue > cur_tile.rgb.green)
						rgb.blue += FILTER_PERCENT * (float) (cur_tile.rgb.blue - rgb.blue);

					tile_img.set_pixel(x, y, rgb);
				}
			}
		}

		tile_img.region(0, 0, mosaic.cmp_width, mosaic.cmp_height, buffer);
		final_img.copy_from(buffer, cur_tile.start_x, cur_tile.start_y);

	}

	final_img.save_image(FILE_OUT);

	return 0;
}


/*********************************************/
/*************** FIT FUNCTIONS ***************/
/*********************************************/

// chooses the best available image, allows repeats without care
// tile_index: current tile to map an image to
// returns best fitting image
int fit_best_pick(unsigned int tile_index)
{
	int n = 0;
	int img_index = tile_map[tile_index][n].index;

	// choose first best pick that isnt repeated as much
	while (n < components_size && components[img_index].placed >= TILE_RPT_COUNT) {
		img_index = tile_map[tile_index][n].index;
		n++;
	}

	return img_index;
}

// checks around if we've used the tile before
// if TILE_MIN_DIST is too high, we could run out of images to use
// tile_index: reference tile to check around
// img_index: image we do not want to repeat
// returns 0 for no repeats, 1 for a repeat seen
int fit_check_repeated(unsigned int tile_index, unsigned int img_index)
{
	// check in an area around us
	for (int row = -TILE_MIN_DIST; row <= TILE_MIN_DIST; row++)
		for (int col = -TILE_MIN_DIST; col <= TILE_MIN_DIST; col++) {
			int r = tile_index / mosaic.cols + row;
			int c = tile_index % mosaic.cols + col;
			int loc = r * mosaic.cols + c;

			if (loc < TOTAL_TILES && loc >= 0)
				if (tiles[loc].img_index == img_index)
					return 1;
		}

	return 0;
}

// chooses the best available image
// does not allow repeats within a region
// tile_index: current tile to map an image to
// returns best fitting image
int fit_best_pick_sparse(unsigned int tile_index)
{
	int n = 0;
	int img_index = tile_map[tile_index][n].index;
	int seen = 1;

	// choose first best pick that isnt repeated as much
	// keep looking for first usable picture
	// 1. do not exceed image list
	// 2. do not choose an image that's been placed too much
	// 3. do not choose an image that's been seen recently
	while (n < components_size && (components[img_index].placed >= TILE_RPT_COUNT  || seen)) {
		img_index = tile_map[tile_index][n].index;
 		seen = fit_check_repeated(tile_index, img_index);
		n++;
	}

	return img_index;
}

// Populates tiles with images (does not write to disk)
// tile_index: tile we want to find an image for
// This uses a lot of memory
void tile_place_best_fit(unsigned int tile_index)
{
	string output = "Best Fit Tile " + to_string(tile_index);
	dbgprint(2, output);

	// note we can have more images than tiles we place
	// look for best fit image we can place
	//int img_index = fit_rand_pick(tile_index);
	int img_index = fit_best_pick_sparse(tile_index);

	// found best fit, save index
	tiles[tile_index].img_index = img_index;
	components[img_index].placed++;
}


/**********************************************/
/*************** RANK FUNCTIONS ***************/
/**********************************************/

// compares two tiles values
bool compare_tiles(const mosaic_map& img1, const mosaic_map& img2)
{
	return abs(img1.value) < abs(img2.value);
}

// Ranks all the images in order for a given tile
// tile_index: tile we want to rank images on
// This uses a lot of memory
void tile_rank_fits(unsigned int tile_index)
{
	//tiles[tile_index]
	tile_map[tile_index].resize(components_size);

	string output = "Ranking Tile " + to_string(tile_index) + " Thread: ";
	dbgprint(2, output);

	// note we can have more images than tiles we place
	// for each image, check their weighting and add it into the list
	for(int n = 0; n < components_size; n++) {
		int r = (int)components[n].rgb.red - (int)tiles[tile_index].rgb.red;
		int g = (int)components[n].rgb.green - (int)tiles[tile_index].rgb.green;
		int b = (int)components[n].rgb.blue - (int)tiles[tile_index].rgb.blue;

		// take root mean square average (simple average results in image being dark)
		tile_map[tile_index][n].index = n;
		// we want the absolute distance (0 being most preferred match)
		tile_map[tile_index][n].value = abs(r + g + b);
	}

	// biggest bottle neck for high tile images
	sort(tile_map[tile_index].begin(), tile_map[tile_index].end(), compare_tiles);
}


/***********************************************/
/*************** INPUT FUNCTIONS ***************/
/***********************************************/
int get_component_file_weight()
{
	struct dirent **files;
	int img = 0;

	// Scan files in directory
	int n = scandir(DIR_IMG_PATH, &files, NULL, alphasort);
	if (n >= 0) {
		// Loop through file names
		for (int i = 0; i < n; i++) {
			struct dirent *ent;
			// Get pointer to file entry 
			ent = files[i];
			// Output file name
			switch (ent->d_type) {
			case DT_REG:
				//printf("%s\n", ent->d_name);
				string fullpath = DIR_IMG_PATH;
				fullpath += "\\";
				fullpath += ent->d_name;

				string output = "Weighing File: " + fullpath;
				dbgprint(2, output);

				bitmap_image image(fullpath);

				// calculate image rgb
				if (img < components_size) {
					rgb_t rgb;
					float r = 0, g = 0, b = 0;
					int count = 0;

					for (size_t y = 0; y < mosaic.cmp_height; y++)
						for (size_t x = 0; x < mosaic.cmp_width; x++) {
							image.get_pixel(x, y, rgb);
							r += rgb.red * rgb.red;
							g += rgb.green * rgb.green;
							b += rgb.blue * rgb.blue;
							count++;
						}

					// average weight for this image
					r = sqrt(r / count);
					g = sqrt(g / count);
					b = sqrt(b / count);
					components[img].rgb.red = r;
					components[img].rgb.green = g;
					components[img].rgb.blue = b;

					img++;
				}

				break;
			}
		}
		// Release file names
		for (int i = 0; i < n; i++) {
			free(files[i]);
		}
		free(files);

	}
	else {
		string output = "ERROR: Cannot open component directory";
		dbgprint(1, output);
	}
	string output = "Done finding weight for " + to_string(components_size) + " component images";
	dbgprint(1, output);
	return components_size;
}

// builds the component list and finds the min/max width/height of this set
// returns number of component images found
int get_component_file_list()
{
	struct dirent **files;

	// Scan files in directory
	int n = scandir(DIR_IMG_PATH, &files, NULL, alphasort);
	if (n >= 0) {
		// Loop through file names
		for (int i = 0; i < n; i++) {
			struct dirent *ent;
			// Get pointer to file entry 
			ent = files[i];
			// Output file name
			switch (ent->d_type) {
				case DT_REG:
					//printf("%s\n", ent->d_name);
					string fullpath = DIR_IMG_PATH;
					fullpath += "\\";
					fullpath += ent->d_name;

					string output = "Loading File: " + fullpath;
					dbgprint(2, output);
					
					bitmap_image image(fullpath);
				
					// save this component image
					component_metadata tmp;
					tmp.width = image.width();
					tmp.height = image.height();
					tmp.path = fullpath;

					tmp.rgb.red = 0;
					tmp.rgb.green = 0;
					tmp.rgb.blue = 0;
					tmp.placed = 0;

					// save component image
					components.push_back(tmp);
					
					// save image set minimum width and height
					if (tmp.width < cmp_img_min_width)
						cmp_img_min_width = tmp.width;
					if (tmp.height < cmp_img_min_height)
						cmp_img_min_height = tmp.height;

					break;
			}
		}
		// Release file names
		for (int i = 0; i < n; i++) {
			free(files[i]);
		}
		free(files);

	}
	else {
		string output = "ERROR: Cannot open component directory";
		dbgprint(1, output);
	}
	string output = "Done loading " + to_string(components.size()) + " component images" ;
	dbgprint(1, output);
	return components.size();
}

// gets mosaic metadata
// returns 0 for success, 1 for failure
int get_mosaic_metadata(int num_tiles)
{
	string file_name(FILE_REF);
	bitmap_image image(file_name);

	if (!image) {
		dbgprint(0, "ERROR: FILE_REF not found");
	}

	unsigned int w = image.width();
	unsigned int h = image.height();
	mosaic.total = num_tiles;

	// determine desired tile pixel size
	// maintain pixel ratio to our reference image
	// all images will be the same size, large images are cropped
	float ref_aspect_ratio = (float) w / (float) h;
	float cmp_aspect_ratio = (float) cmp_img_min_width / (float) cmp_img_min_height;
	// crop the height
	if (cmp_aspect_ratio < ref_aspect_ratio) {
		dbgprint(1, "Cropping Mosaic Height");
		// reduce pixels until we get within error margin
		for (int new_height = cmp_img_min_height; new_height > 0; new_height--) {
			float ratio_err = abs(ref_aspect_ratio - (float) cmp_img_min_width / (float) new_height);

			if (ratio_err <= ASP_RATIO_ERR) {
				mosaic.cmp_height = new_height;
				mosaic.cmp_width = cmp_img_min_width;
				break;
			}

			if (new_height <= 20) {
				dbgprint(0, "ERROR: Cropped height value too low, increase ASP_RATIO_ERR to use this image");
				return 1;
			}
		}
	}
	// crop the width
	else {
		dbgprint(1, "Cropping Mosaic Width");
		// reduce pixels until we get within error margin
		for (int new_width = cmp_img_min_width; new_width > 0; new_width--) {
			float ratio_err = abs(ref_aspect_ratio - (float) new_width / (float) cmp_img_min_height);
			if (ratio_err <= ASP_RATIO_ERR) {
				mosaic.cmp_width = new_width;
				mosaic.cmp_height = cmp_img_min_height;
				break;
			}

			if (new_width <= 20) {
				dbgprint(0, "ERROR: Cropped height value too low");
				return 1;
			}
		}
	}

  // determine number of images on a row and in a col
  // look for the closest aspect ratio
  /*dbgprint(1, "Saving Mosaic Rows + Cols");
  mosaic.rows = mosaic.cols = 1;
  cmp_aspect_ratio = (float)mosaic.cmp_width / (float)mosaic.cmp_height;
  while ((mosaic.rows+1) * (mosaic.cols+1) <= mosaic.total) {
    mosaic.rows += 1;
    mosaic.cols += 1;
  }*/
  // this should work for squares
   mosaic.rows = mosaic.cols = TILE_LDA;

	// mosaic resolution
	dbgprint(1, "Saving Mosaic Dimensions");
	mosaic.width = mosaic.cmp_width * mosaic.cols;
	mosaic.height = mosaic.cmp_height * mosaic.rows;

	int y_count = 0;
	int x_count = 0;

	// dont track the edges if the aspect ratio isnt exact
	// using tricky integer rounding to get the exact pixels we want
	int h_scaled = h / mosaic.rows;
	int w_scaled = w / mosaic.cols;
	h_scaled *= mosaic.rows;
	w_scaled *= mosaic.cols;

	// calculate the rgb weight for every tile
	// if the aspect ratio is off some pixels will get trimmed from the calculations
	dbgprint(1, "Calculating Mosaic Weights");
	for (size_t y = 0; y < h_scaled; y += h / mosaic.rows) {
		for (size_t x = 0; x < w_scaled; x += w / mosaic.cols) {
			mosaic_tile tmp;
			rgb_t rgb;
			float r = 0, g = 0, b = 0, count = 0;
			tmp.start_y = y_count * mosaic.cmp_height;
			tmp.start_x = x_count * mosaic.cmp_width;

			// check the rgb weight for this tile region
			for (size_t j = y; j < y + mosaic.cmp_height && j < h_scaled; j++)
				for (size_t i = x; i < x + mosaic.cmp_width && i < w_scaled; i++) {
					image.get_pixel(i, j, rgb);
					r += rgb.red * rgb.red;
					g += rgb.green * rgb.green; 
					b += rgb.blue * rgb.blue;
					count++;
				}

			// average rgb weight for this tile
			r = sqrt(r / count); 
			g = sqrt(g / count);
			b = sqrt(b / count);
			tmp.rgb.red = r;
			tmp.rgb.green = g;
			tmp.rgb.blue = b;
			tmp.img_index = -1;	// no index

			tiles.push_back(tmp);

			x_count++;
		}
		y_count++;
		x_count = 0;
	}

	string output = "Done loading metadata";
	output += "\n\tMetadata Summary:";
	output += "\n\tTotal Tiles: " + to_string(mosaic.total);
	output += "\n\t(Rows, Cols): " + to_string(mosaic.rows) + ", " + to_string(mosaic.cols);
	output += "\n\tMosaic Res: " + to_string(mosaic.width) + " x " + to_string(mosaic.height);
	output += "\n\tTile Res: " + to_string(mosaic.cmp_width) + " x " + to_string(mosaic.cmp_height);
	dbgprint(1, output);
	return 0;
}



/************************************/
/*************** MAIN ***************/
/************************************/

int main()
{
	int check = 1;

	cout << "\n\n";

	double program_time = read_timer();
	double time_step = read_timer();

	// Load component images and:
	// 1) calculate RGB weights
	// 2) save metadata (filename, width, height)
	// (very slight bottle neck)
	dbgprint(1, "Starting Image Indexing");
	components_size = get_component_file_list();
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	// Load the reference file and subdivide it into regions
	// for each region calculate the weight
	// (performance bottle neck here)
	dbgprint(1, "\n\nStarting Mosaic Calculations");
	check = get_mosaic_metadata(TOTAL_TILES);
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	// should calculate weight on the portion we crop and not the entire tile image
	dbgprint(1, "\n\nStarting Image Weight Calculations");
	get_component_file_weight();
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	if (check)	return 0;
	// create a blank template
	// (no bottle neck here)
	write_bmp_template();
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	dbgprint(1, "\n\nStarting Ranking");
	// set our size or we run into allocation errors
	tile_map.resize(TOTAL_TILES);
	// rank tiles 
	// (significant performance bottle neck here)
	for (int i = 0; i < TOTAL_TILES; i++)
		tile_rank_fits(i);
	dbgprint(1, "Done Ranking");
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	dbgprint(1, "\n\nStarting Fitting");
	// find the best fit for all tiles
	// (significant performance bottle neck here)
	for (int i = 0; i < TOTAL_TILES; i++)
		tile_place_best_fit(i);
	dbgprint(1, "Done Fitting");
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}

	dbgprint(1, "\n\nStarting Mosaic Write");
#if !TEST
	// place tiles
	// writing full image uses more memory but is significantly faster
	// (bottle neck with FILTER == 1)
	write_full_img();
#endif
	dbgprint(1, "Done Mosaic Write");
	if (TIMESTEPS) {
		printf("Time taken  [%g seconds]", read_timer() - time_step);
		time_step = read_timer();
	}
	
	program_time = read_timer() - program_time;

   cout << "\n\nFinished Mosaic \n\n";
   printf("Program time = %g seconds", program_time);
   return 0;
}