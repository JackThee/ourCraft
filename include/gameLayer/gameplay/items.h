#pragma once
#include <blocks.h>
#include <vector>


constexpr static unsigned short ItemsStartPoint = 4'096;

enum ItemTypes
{

	stick = ItemsStartPoint,


};


struct Item
{
	Item() {};
	Item(unsigned short type, unsigned char counter = 1):type(type), counter(counter) {};

	unsigned short type = 0;
	std::vector<unsigned char> metaData;
	unsigned char counter = 1;

	bool isBlock();
};

struct PlayerInventory
{
	
	Item items[36] = {};
	
	Item heldInMouse = {};


	//todo
	void sanitize() {};
	
};