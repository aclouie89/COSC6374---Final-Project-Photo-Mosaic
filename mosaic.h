#pragma once

#include <string>

typedef struct mosaic_metadata
{
	// width and height of the mosaic in pixels
	unsigned int width;
	unsigned int height;
	// max number of images that can be placed
	int total;
	// max number of images on row and column
	int rows;
	int cols;
	// width and height of a component image in pixels
	unsigned int cmp_width;
	unsigned int cmp_height;
};

typedef struct component_metadata
{
	unsigned int width;
	unsigned int height;
	rgb_t rgb;
	std::string path;
	int placed = 0;
};

typedef struct mosaic_tile
{
	size_t start_x;
	size_t start_y;
	int img_index;
	rgb_t rgb;
	// just testing with end vars
	//size_t end_x;
	//size_t end_y;
};

typedef struct mosaic_map
{
	int index;	// index to component_metadata image
	int value;	// RMS value of the image
};