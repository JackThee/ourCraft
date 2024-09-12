#include <chunk.h>
#include <rendering/renderer.h>
#include <blocksLoader.h>
#include <lightSystem.h>
#include <iostream>
#include <rendering/bigGpuBuffer.h>
#include <platformTools.h>
#include <algorithm>
#include <array>

#undef max
#undef min

constexpr int halfBottomStartGeometry = 36;
constexpr int cornerUpStartGeometry = 40;
constexpr int topHalfStartGeometry = 48;
constexpr int topHalfBottomPartStartGeometry = 52;
constexpr int frontalMiddleTopPieceStartGeometry = 56;
constexpr int slabTopFace = 60;


Block *Chunk::safeGet(int x, int y, int z)
{
	if (x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE || z >= CHUNK_SIZE || y >= CHUNK_HEIGHT)
	{
		return nullptr;
	}
	else
	{
		return &unsafeGet(x, y, z);
	}
}


struct TransparentCandidate
{
	glm::ivec3 position;
	float distance;
};

static std::vector<TransparentCandidate> transparentCandidates;
static std::vector<int> opaqueGeometry;
static std::vector<int> transparentGeometry;
static std::vector<glm::ivec4> lights;

void arangeData(std::vector<int> &currentVector)
{
	glm::ivec4 *geometryArray = reinterpret_cast<glm::ivec4 *>(opaqueGeometry.data());
	permaAssertComment(opaqueGeometry.size() % 4 == 0, "baking vector corrupted...");
	size_t numElements = opaqueGeometry.size() / 4;

	// Custom comparator function for sorting
	auto comparator = [](const glm::ivec4 &a, const glm::ivec4 &b)
	{
		int firstPart = ((short *)&a.x)[0];
		int secondPart = ((short *)&b.x)[0];

		//return firstPart < secondPart;

		if (firstPart != secondPart)
			return firstPart < secondPart;
		else
		{
			firstPart = ((short *)&a.x)[1];
			secondPart = ((short *)&b.x)[1];
			return firstPart < secondPart;
		};

	};

	// Sort the array of glm::ivec4
	std::sort(geometryArray, geometryArray + numElements, comparator);

}

void pushFlagsLightAndPosition(std::vector<int> &vect,
	glm::ivec3 position,
	bool isWater, bool isInWater,
	unsigned char sunLight, unsigned char torchLight, unsigned char aoShape)
{

	//0x    FF      FF      FF    FF
	//   -flags----light----position--

	unsigned char light = merge4bits(sunLight, torchLight);

	unsigned char flags = 0;
	if (isWater)
	{
		flags |= 0b1;
	}

	if (isInWater)
	{
		flags |= 0b10;
	}

	//aoShape &= 0x0F;
	aoShape <<= 4;
	flags |= aoShape;

	//shadow flag stuff.


	unsigned short firstHalf = mergeChars(flags, light);
	//unsigned short firstHalf = mergeChars(flags, 0xFF);

	int positionY = mergeShorts((short)position.y, firstHalf);

	vect.push_back(position.x);
	vect.push_back(positionY);
	vect.push_back(position.z);
};

//todo a counter to know if I have transparent geometry in this chunk
bool Chunk::bake(Chunk *left, Chunk *right, Chunk *front, Chunk *back, 
	Chunk *frontLeft, Chunk *frontRight, Chunk *backLeft, Chunk *backRight,
	glm::ivec3 playerPosition, BigGpuBuffer &gpuBuffer)
{

	bool updateGeometry = 0;
	bool updateTransparency = isDirtyTransparency();

	if (
		isDirty()
		|| (!isNeighbourToLeft() && left != nullptr)
		|| (!isNeighbourToRight() && right != nullptr)
		|| (!isNeighbourToFront() && front != nullptr)
		|| (!isNeighbourToBack() && back != nullptr)
		)
	{
		updateGeometry = true;
		updateTransparency = true;
	}

#pragma region helpers

	const int FRONT = 0;
	const int BACK = 1;
	const int TOP = 2;
	const int BOTTOM = 3;
	const int LEFT = 4;
	const int RIGHT = 5;

	const int DOWN_FRONT = 6;
	const int DOWN_BACK = 7;
	const int DOWN_LEFT = 8;
	const int DOWN_RIGHT = 9;

	const int UP_FRONT = 10;
	const int UP_BACK = 11;
	const int UP_LEFT = 12;
	const int UP_RIGHT = 13;

	const int UP_FRONTLEFT = 14;
	const int UP_FRONTRIGHT = 15;
	const int UP_BACKLEFT = 16;
	const int UP_BACKRIGHT = 17;

	const int DOWN_FRONTLEFT = 18;
	const int DOWN_FRONTRIGHT = 19;
	const int DOWN_BACKLEFT = 20;
	const int DOWN_BACKRIGHT = 21;

	const int FRONTLEFT = 22;
	const int FRONTRIGHT = 23;
	const int BACKLEFT = 24;
	const int BACKRIGHT = 25;

	auto getNeighboursLogic = [&](int x, int y, int z, Block *sides[26])
	{
		auto justGetBlock = [&](int x, int y, int z) -> Block *
		{
			if (y >= CHUNK_HEIGHT || y < 0) { return nullptr; }

			if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE)
			{
				return &unsafeGet(x, y, z);
			}

			if (x >= 0 && x < CHUNK_SIZE)
			{
				//z is the problem
				if (z < 0)
				{
					if (back)
					{
						return &back->unsafeGet(x, y, CHUNK_SIZE - 1);
					}
				}
				else
				{
					if (front)
					{
						return &front->unsafeGet(x, y, 0);
					}
				}
			}
			else if (z >= 0 && z < CHUNK_SIZE)
			{
				//x is the problem
				if (x < 0)
				{
					if (left)
					{
						return &left->unsafeGet(CHUNK_SIZE - 1, y, z);
					}
				}
				else
				{
					if (right)
					{
						return &right->unsafeGet(0, y, z);
					}
				}
			}
			else
			{
				//both are the problem
				if (x < 0 && z < 0)
				{
					if (backLeft)
					{
						return &backLeft->unsafeGet(CHUNK_SIZE - 1, y, CHUNK_SIZE - 1);
					}
				}else
				if (x >= CHUNK_SIZE && z < 0)
				{
					if (backRight)
					{
						return &backRight->unsafeGet(0, y, CHUNK_SIZE - 1);
					}
				}else if (x < 0 && z >= CHUNK_SIZE)
				{
					if (frontLeft)
					{
						return &frontLeft->unsafeGet(CHUNK_SIZE - 1, y, 0);
					}
				}else
				if (x >= CHUNK_SIZE && z >= CHUNK_SIZE)
				{
					if (frontRight)
					{
						return &frontRight->unsafeGet(0, y, 0);
					}
				}
				else
				{
					permaAssertComment(0, "error in chunk get neighbour logic!");
				}

				return nullptr;
			}
		};

		auto bfront = justGetBlock(x, y, z + 1);
		auto bback = justGetBlock(x, y, z - 1);
		auto btop = justGetBlock(x, y + 1, z);
		auto bbottom = justGetBlock(x, y - 1, z);
		auto bleft = justGetBlock(x - 1, y, z);
		auto bright = justGetBlock(x + 1, y, z);

		auto bdownfront = justGetBlock(x, y-1, z + 1);
		auto bdownback = justGetBlock(x, y-1, z - 1);
		auto bdownleft = justGetBlock(x - 1, y-1, z);
		auto bdownright = justGetBlock(x + 1, y-1, z);

		auto bupfront = justGetBlock(x, y + 1, z + 1);
		auto bupback = justGetBlock(x, y + 1, z - 1);
		auto bupleft = justGetBlock(x - 1, y + 1, z);
		auto bupright = justGetBlock(x + 1, y + 1, z);

		auto bupfrontLeft = justGetBlock(x - 1, y + 1, z + 1);
		auto bupfrontright = justGetBlock(x + 1, y + 1, z + 1);
		auto bupbackleft = justGetBlock(x - 1, y + 1, z - 1);
		auto bupbackright = justGetBlock(x + 1, y + 1, z - 1);

		sides[0] = bfront;
		sides[1] = bback;
		sides[2] = btop;
		sides[3] = bbottom;
		sides[4] = bleft;
		sides[5] = bright;

		sides[6] = bdownfront;
		sides[7] = bdownback;
		sides[8] = bdownleft;
		sides[9] = bdownright;

		sides[10] = bupfront;
		sides[11] = bupback;
		sides[12] = bupleft;
		sides[13] = bupright;

		sides[14] = bupfrontLeft;
		sides[15] = bupfrontright;
		sides[16] = bupbackleft;
		sides[17] = bupbackright;

		sides[18] = justGetBlock(x - 1, y - 1, z + 1);
		sides[19] = justGetBlock(x + 1, y - 1, z + 1);
		sides[20] = justGetBlock(x - 1, y - 1, z - 1);
		sides[21] = justGetBlock(x + 1, y - 1, z - 1);

		sides[22] = justGetBlock(x - 1, y, z + 1);
		sides[23] = justGetBlock(x + 1, y, z + 1);
		sides[24] = justGetBlock(x - 1, y, z - 1);
		sides[25] = justGetBlock(x + 1, y, z - 1);

	};

	auto determineAOShape = [&](int i, Block *sides[26])
	{
		int aoShape = 0;

		auto calculateSide = [](Block *sides[26], bool front,
			bool back, bool left, bool right,
			bool frontLeft, bool frontRight, bool backLeft, bool backRight)
		{

			int aoShape = 0;

			if (frontLeft)
			{
				aoShape = 6;
			}
			else if (frontRight)
			{
				aoShape = 7;
			}
			else if (backLeft)
			{
				aoShape = 8;
			}
			else if (backRight)
			{
				aoShape = 5;
			}


			if (front || (frontRight && frontLeft))
			{
				aoShape = 1;
			}
			else if (back || (backRight && backLeft))
			{
				aoShape = 2;
			}
			else if (left || (backLeft && frontLeft))
			{
				aoShape = 3;
			}
			else if (right || (backRight && frontRight))
			{
				aoShape = 4;
			}


			//opposite corners
			if ((frontLeft && backRight)
				|| (frontRight && backLeft))
			{
				aoShape = 14;
			}


			//darker corners
			if ((front && (left || backLeft)) ||
				(backLeft && frontLeft && frontRight) || (left && frontRight))
			{
				aoShape = 10;
			}
			else if (front && (right || backRight) ||
				(frontLeft && frontRight && backRight) || (right && frontLeft)
				)
			{
				aoShape = 11;
			}
			else if (back && (left || frontLeft) ||
				(frontLeft && backLeft && backRight) || (left && backRight)

				)
			{
				aoShape = 12;
			}
			else if (back && (right || frontRight) ||
				(backLeft && backRight && frontRight) || (right && backLeft)
				)
			{
				aoShape = 9;
			}

			bool backLeftCorner = back || backLeft || left;
			bool backRightCorner = back || backRight || right;
			bool frontLeftCorner = front || frontLeft || left;
			bool frontRightCorner = front || frontRight || right;

			if (backLeftCorner && backRightCorner && frontLeftCorner && frontRightCorner)
			{
				aoShape = 13; //full shaodw
			}
			
			return aoShape;
		};

		if (i == 0) // front
		{
			bool upFront = (sides[UP_FRONT] && sides[UP_FRONT]->isOpaque());
			bool downFront = (sides[DOWN_FRONT] && sides[DOWN_FRONT]->isOpaque());
			bool leftFront = (sides[FRONTLEFT] && sides[FRONTLEFT]->isOpaque());
			bool rightFront = (sides[FRONTRIGHT] && sides[FRONTRIGHT]->isOpaque());

			bool upFrontLeft = (sides[UP_FRONTLEFT] && sides[UP_FRONTLEFT]->isOpaque());
			bool upFrontRight = (sides[UP_FRONTRIGHT] && sides[UP_FRONTRIGHT]->isOpaque());
			bool downFrontLeft = (sides[DOWN_FRONTLEFT] && sides[DOWN_FRONTLEFT]->isOpaque());
			bool downFrontRight = (sides[DOWN_FRONTRIGHT] && sides[DOWN_FRONTRIGHT]->isOpaque());

			aoShape = calculateSide(sides, 
				leftFront,  rightFront, upFront, downFront,
				upFrontLeft, downFrontLeft, upFrontRight,
				downFrontRight);
		}else if (i == 1) // back
		{
			bool upBack = (sides[UP_BACK] && sides[UP_BACK]->isOpaque());
			bool downBack = (sides[DOWN_BACK] && sides[DOWN_BACK]->isOpaque());
			bool leftBack = (sides[BACKLEFT] && sides[BACKLEFT]->isOpaque());
			bool rightBack = (sides[BACKRIGHT] && sides[BACKRIGHT]->isOpaque());

			bool upBackLeft = (sides[UP_BACKLEFT] && sides[UP_BACKLEFT]->isOpaque());
			bool upBackRight = (sides[UP_BACKRIGHT] && sides[UP_BACKRIGHT]->isOpaque());
			bool downBackLeft = (sides[DOWN_BACKLEFT] && sides[DOWN_BACKLEFT]->isOpaque());
			bool downBackRight = (sides[DOWN_BACKRIGHT] && sides[DOWN_BACKRIGHT]->isOpaque());

			aoShape = calculateSide(sides,
				leftBack, rightBack, upBack, downBack,
				upBackLeft, downBackLeft, upBackRight,
				downBackRight);
		}
		else
		if (i == 2) //top
		{
			bool upFront = (sides[UP_FRONT] && sides[UP_FRONT]->isOpaque());
			bool upBack = (sides[UP_BACK] && sides[UP_BACK]->isOpaque());
			bool upLeft = (sides[UP_LEFT] && sides[UP_LEFT]->isOpaque());
			bool upRight = (sides[UP_RIGHT] && sides[UP_RIGHT]->isOpaque());

			bool upFrontLeft = (sides[UP_FRONTLEFT] && sides[UP_FRONTLEFT]->isOpaque());
			bool upFrontRight = (sides[UP_FRONTRIGHT] && sides[UP_FRONTRIGHT]->isOpaque());
			bool upBackLeft = (sides[UP_BACKLEFT] && sides[UP_BACKLEFT]->isOpaque());
			bool upBackRight = (sides[UP_BACKRIGHT] && sides[UP_BACKRIGHT]->isOpaque());

			aoShape = calculateSide(sides, upFront, upBack, upLeft, upRight, upFrontLeft, upFrontRight,
				upBackLeft, upBackRight);
		}else
		if (i == 3) //bottom
		{
			bool downFront = (sides[DOWN_FRONT] && sides[DOWN_FRONT]->isOpaque());
			bool downBack = (sides[DOWN_BACK] && sides[DOWN_BACK]->isOpaque());
			bool downLeft = (sides[DOWN_LEFT] && sides[DOWN_LEFT]->isOpaque());
			bool downRight = (sides[DOWN_RIGHT] && sides[DOWN_RIGHT]->isOpaque());

			bool downFrontLeft = (sides[DOWN_FRONTLEFT] && sides[DOWN_FRONTLEFT]->isOpaque());
			bool downFrontRight = (sides[DOWN_FRONTRIGHT] && sides[DOWN_FRONTRIGHT]->isOpaque());
			bool downBackLeft = (sides[DOWN_BACKLEFT] && sides[DOWN_BACKLEFT]->isOpaque());
			bool downBackRight = (sides[DOWN_BACKRIGHT] && sides[DOWN_BACKRIGHT]->isOpaque());

			aoShape = calculateSide(sides, 
				 downLeft, downRight, downFront, downBack,
				 downFrontLeft, downBackLeft, downFrontRight, downBackRight
			);
		}else if (i == 4) //left
		{
			bool upLeft = (sides[UP_LEFT] && sides[UP_LEFT]->isOpaque());
			bool downLeft = (sides[DOWN_LEFT] && sides[DOWN_LEFT]->isOpaque());
			bool leftFront = (sides[FRONTLEFT] && sides[FRONTLEFT]->isOpaque());
			bool leftBack = (sides[BACKLEFT] && sides[BACKLEFT]->isOpaque());

			bool upFrontLeft = (sides[UP_FRONTLEFT] && sides[UP_FRONTLEFT]->isOpaque());
			bool upBackLeft = (sides[UP_BACKLEFT] && sides[UP_BACKLEFT]->isOpaque());
			bool downFrontLeft = (sides[DOWN_FRONTLEFT] && sides[DOWN_FRONTLEFT]->isOpaque());
			bool downBackLeft = (sides[DOWN_BACKLEFT] && sides[DOWN_BACKLEFT]->isOpaque());

			aoShape = calculateSide(sides,
				  leftBack, leftFront, upLeft, downLeft,
				 upBackLeft, downBackLeft, upFrontLeft, downFrontLeft
				);

		}
		else if (i == 5) //right
		{
			bool upRight = (sides[UP_RIGHT] && sides[UP_RIGHT]->isOpaque());
			bool downRight = (sides[DOWN_RIGHT] && sides[DOWN_RIGHT]->isOpaque());
			bool rightFront = (sides[FRONTRIGHT] && sides[FRONTRIGHT]->isOpaque());
			bool rightBack = (sides[BACKRIGHT] && sides[BACKRIGHT]->isOpaque());

			bool upFrontRight = (sides[UP_FRONTRIGHT] && sides[UP_FRONTRIGHT]->isOpaque());
			bool upBackRight = (sides[UP_BACKRIGHT] && sides[UP_BACKRIGHT]->isOpaque());
			bool downFrontRight = (sides[DOWN_FRONTRIGHT] && sides[DOWN_FRONTRIGHT]->isOpaque());
			bool downBackRight = (sides[DOWN_BACKRIGHT] && sides[DOWN_BACKRIGHT]->isOpaque());

			aoShape = calculateSide(sides,
				rightFront, rightBack, upRight, downRight,
				upFrontRight, downFrontRight, upBackRight, downBackRight
			);

		}

		return aoShape;
	};

	auto blockBakeLogicForSolidBlocks = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b, bool isAnimated)
	{
		Block *sides[26] = {};
		getNeighboursLogic(x, y, z, sides);

		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y, z + this->data.z * CHUNK_SIZE};

		for (int i = 0; i < 6; i++)
		{

			if (
				(!(isAnyLeaves(b.getType()) && sides[i] != nullptr && isAnyLeaves((sides[i])->getType()))
				&&
				(sides[i] != nullptr && !(sides[i])->isOpaque()) )
				|| 
				(
				//(i == 3 && y == 0) ||		//display the bottom face
				(i == 2 && y == CHUNK_HEIGHT - 1) //display the top face
				)
			   )
			{
				currentVector->push_back(mergeShorts(i + isAnimated * 10, getGpuIdIndexForBlock(b.getType(), i)));

				int aoShape = determineAOShape(i, sides);
				bool isInWater = (sides[i] != nullptr) && sides[i]->getType() == BlockTypes::water;

				unsigned char sunLight = 0;
				unsigned char torchLight = 0;
				if (dontUpdateLightSystem)
				{
					sunLight = 15;
					torchLight = 0;
				}
				else if (isLightEmitor(b.getType()))
				{
					if (sides[i]) { sunLight = sides[i]->getSkyLight(); }
					torchLight = 15;
				}
				else if (i == 2 && y == CHUNK_HEIGHT - 1)
				{
					sunLight = 15; //top face;

				}
				else if (y == 0 && i == 3)
				{
					sunLight = 10; //bottom face;

				}
				else  if (sides[i] != nullptr)
				{
					sunLight = sides[i]->getSkyLight();
					torchLight = sides[i]->getLight();
				}
				///
				if (isTransparentGeometry(b.getType()))
				{
					sunLight = std::max(sunLight, b.getSkyLight());
					torchLight = std::max(torchLight, b.getLight());
				}

				pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
					sunLight, torchLight, aoShape);
				
			}
		}
	};

	//todo reuse up
	auto calculateLightThings = [&](unsigned char &sunLight, unsigned char &torchLight,
			Block *side, Block &b, int i, int y)
	{
		sunLight = 0;
		torchLight = 0;
		if (dontUpdateLightSystem)
		{
			sunLight = 15;
			torchLight = 0;
		}
		else if (isLightEmitor(b.getType()))
		{
			if (side) { sunLight = side->getSkyLight(); }
			torchLight = 15;
		}
		else if (i == 2 && y == CHUNK_HEIGHT - 1)
		{
			sunLight = 15; //top face;

		}
		else if (y == 0 && i == 3)
		{
			sunLight = 10; //bottom face;

		}
		else  if (side != nullptr)
		{
			sunLight = side->getSkyLight();
			torchLight = side->getLight();
		}
		///
		if (isTransparentGeometry(b.getType()))
		{
			sunLight = std::max(sunLight, b.getSkyLight());
			torchLight = std::max(torchLight, b.getLight());
		}
	};

	auto blockBakeLogicForStairs = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b)
	{
		Block *sides[26] = {};
		getNeighboursLogic(x, y, z, sides);

		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y, z + this->data.z * CHUNK_SIZE};

		int rotation = b.getRotationFor365RotationTypeBlocks();

		for (int i = 0; i < 6; i++)
		{

			if (
				(sides[i] != nullptr && !(sides[i])->isOpaque())
				|| (
				//(i == 3 && y == 0) ||		//display the bottom face
				(i == 2 && y == CHUNK_HEIGHT - 1) //display the top face
				)
				)
			{

				int aoShape = determineAOShape(i, sides);
				bool isInWater = (sides[i] != nullptr) && sides[i]->getType() == BlockTypes::water;
				unsigned char sunLight = 0;
				unsigned char torchLight = 0;
				calculateLightThings(sunLight, torchLight, sides[i], b, i, y);


				auto placeFlagsLightsNormally = [&]()
				{
					pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
						sunLight, torchLight, aoShape);
				};

				auto placeNormally = [&]()
				{
					currentVector->push_back(mergeShorts(i, getGpuIdIndexForBlock(b.getType(), i)));
					placeFlagsLightsNormally();
				};

				if (i == 0)
				{
					//front
					if (rotation == 2)
					{
						placeNormally();
					}
					else
					{
						//bottom rim
						currentVector->push_back(mergeShorts(halfBottomStartGeometry + 0
							, getGpuIdIndexForBlock(b.getType(), 0
						)));
						placeFlagsLightsNormally();
	
						if (rotation == 1)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 5
								, getGpuIdIndexForBlock(b.getType(), 1
							)));
							placeFlagsLightsNormally();
						}else
						if (rotation == 3)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 7
								, getGpuIdIndexForBlock(b.getType(), 1
							)));
							placeFlagsLightsNormally();
						}
					}

				}
				else if(i == 1)
				{
					//back
					if (rotation == 0)
					{
						placeNormally();
					}
					else
					{
						//bottom rim
						currentVector->push_back(mergeShorts(halfBottomStartGeometry + 1
							, getGpuIdIndexForBlock(b.getType(), 1
						)));
						placeFlagsLightsNormally();

						if (rotation == 1)
						{
								currentVector->push_back(mergeShorts(cornerUpStartGeometry + 4
							, getGpuIdIndexForBlock(b.getType(), 1
							)));
							placeFlagsLightsNormally();
						}else
						if (rotation == 3)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 6
								, getGpuIdIndexForBlock(b.getType(), 1
							)));
							placeFlagsLightsNormally();
						}
					}

				}
				else if (i == 2)
				{
					//top

				}
				else if (i == 3)
				{
					//bottom
					placeNormally();
				}
				else if (i == 4)
				{
					//left
					if (rotation == 1)
					{
						placeNormally();
					}
					else
					{
						//bottom rim
						currentVector->push_back(mergeShorts(halfBottomStartGeometry + 2
							, getGpuIdIndexForBlock(b.getType(), 2
						)));
						placeFlagsLightsNormally();

						if (rotation == 0)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 0
								, getGpuIdIndexForBlock(b.getType(), 0
							)));
							placeFlagsLightsNormally();
						}else if (rotation == 2)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 2
								, getGpuIdIndexForBlock(b.getType(), 0
							)));
							placeFlagsLightsNormally();
						}
					}


				}
				else if (i == 5)
				{
					//right
					if (rotation == 3)
					{
						placeNormally();
					}
					else
					{
						//bottom rim
						currentVector->push_back(mergeShorts(halfBottomStartGeometry + 3
							, getGpuIdIndexForBlock(b.getType(), 3
						)));
						placeFlagsLightsNormally();

						if (rotation == 0)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 1
								, getGpuIdIndexForBlock(b.getType(), 0
							)));
							placeFlagsLightsNormally();
						}else
						if (rotation == 2)
						{
							currentVector->push_back(mergeShorts(cornerUpStartGeometry + 3
								, getGpuIdIndexForBlock(b.getType(), 0
							)));
							placeFlagsLightsNormally();
						}
					
					}

				}



			}
		}


		//top face
		if (sides[2] != nullptr && !(sides[2])->isOpaque()
			|| y == CHUNK_HEIGHT - 1
			)
		{

			int aoShape = determineAOShape(2, sides);
			bool isInWater = (sides[2] != nullptr) && sides[2]->getType() == BlockTypes::water;
			unsigned char sunLight = 0;
			unsigned char torchLight = 0;
			calculateLightThings(sunLight, torchLight, sides[2], b, 2, y);

			auto placeFlagsLightsNormally = [&]()
			{
				pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
					sunLight, torchLight, aoShape);
			};

			if (rotation == 0)
			{
				currentVector->push_back(mergeShorts(topHalfStartGeometry + 0
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 2)
			{
				currentVector->push_back(mergeShorts(topHalfStartGeometry + 1
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 1)
			{
				currentVector->push_back(mergeShorts(topHalfStartGeometry + 2
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 3)
			{
				currentVector->push_back(mergeShorts(topHalfStartGeometry + 3
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
		

		}

		//middle faces
		if (
			(y == CHUNK_HEIGHT - 1) ||
			(sides[0] != nullptr && !(sides[0])->isOpaque()) ||
			(sides[1] != nullptr && !(sides[1])->isOpaque()) ||
			(sides[2] != nullptr && !(sides[2])->isOpaque()) ||
			(sides[4] != nullptr && !(sides[4])->isOpaque()) ||
			(sides[5] != nullptr && !(sides[5])->isOpaque())
			)
		{
			int aoShape = determineAOShape(2, sides);
			//bool isInWater = (sides[2] != nullptr) && sides[2]->getType() == BlockTypes::water;
			bool isInWater = 0;
			unsigned char sunLight = 0;
			unsigned char torchLight = 0;
			calculateLightThings(sunLight, torchLight, sides[2], b, 2, y);

			auto placeFlagsLightsNormally = [&]()
			{
				pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
					sunLight, torchLight, aoShape);
			};

			if (rotation == 0)
			{
				currentVector->push_back(mergeShorts(topHalfBottomPartStartGeometry + 1
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();

				currentVector->push_back(mergeShorts(frontalMiddleTopPieceStartGeometry + 0
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 2)
			{
				currentVector->push_back(mergeShorts(topHalfBottomPartStartGeometry + 0
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();

				currentVector->push_back(mergeShorts(frontalMiddleTopPieceStartGeometry + 1
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 1)
			{
				currentVector->push_back(mergeShorts(topHalfBottomPartStartGeometry + 3
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();

				currentVector->push_back(mergeShorts(frontalMiddleTopPieceStartGeometry + 2
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}
			else if (rotation == 3)
			{
				currentVector->push_back(mergeShorts(topHalfBottomPartStartGeometry + 2
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();

				currentVector->push_back(mergeShorts(frontalMiddleTopPieceStartGeometry + 3
					, getGpuIdIndexForBlock(b.getType(), 0
				)));
				placeFlagsLightsNormally();
			}


		}


	};

	auto blockBakeLogicForSlabs = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b)
	{
		Block *sides[26] = {};
		getNeighboursLogic(x, y, z, sides);

		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y, z + this->data.z * CHUNK_SIZE};

		for (int i = 0; i < 6; i++)
		{


			int aoShape = determineAOShape(i, sides);
			bool isInWater = (sides[i] != nullptr) && sides[i]->getType() == BlockTypes::water;
			unsigned char sunLight = 0;
			unsigned char torchLight = 0;
			calculateLightThings(sunLight, torchLight, sides[i], b, i, y);


			auto placeFlagsLightsNormally = [&]()
			{
				pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
					sunLight, torchLight, aoShape);
			};

			auto placeNormally = [&]()
			{
				currentVector->push_back(mergeShorts(i, getGpuIdIndexForBlock(b.getType(), i)));
				placeFlagsLightsNormally();
			};

			if (
				(sides[i] != nullptr && !(sides[i])->isOpaque())
				|| (
				//(i == 3 && y == 0) ||		//display the bottom face
				(i == 2 && y == CHUNK_HEIGHT - 1) //display the top face
				)
				)
			{

				//bottom face
				if (i == 3)
				{
					placeNormally();
				}
				else if (i == 2)
				{
					//top face

				}
				else if (i == 0)
				{
					currentVector->push_back(mergeShorts(halfBottomStartGeometry + 0
						, getGpuIdIndexForBlock(b.getType(), 0
					)));
					placeFlagsLightsNormally();
				}
				else if (i == 1)
				{
					currentVector->push_back(mergeShorts(halfBottomStartGeometry + 1
						, getGpuIdIndexForBlock(b.getType(), 0
					)));
					placeFlagsLightsNormally();
				}
				else if (i == 4)
				{
					currentVector->push_back(mergeShorts(halfBottomStartGeometry + 2
						, getGpuIdIndexForBlock(b.getType(), 0
					)));
					placeFlagsLightsNormally();
				}
				else if (i == 5)
				{
					currentVector->push_back(mergeShorts(halfBottomStartGeometry + 3
						, getGpuIdIndexForBlock(b.getType(), 0
					)));
					placeFlagsLightsNormally();
				}


			}


		}

		if (
			(y == CHUNK_HEIGHT - 1) ||
			(sides[0] != nullptr && !(sides[0])->isOpaque()) ||
			(sides[1] != nullptr && !(sides[1])->isOpaque()) ||
			(sides[2] != nullptr && !(sides[2])->isOpaque()) ||
			(sides[4] != nullptr && !(sides[4])->isOpaque()) ||
			(sides[5] != nullptr && !(sides[5])->isOpaque())
			)
		{
			int aoShape = determineAOShape(2, sides);
			//bool isInWater = (sides[2] != nullptr) && sides[2]->getType() == BlockTypes::water;
			bool isInWater = 0;
			unsigned char sunLight = 0;
			unsigned char torchLight = 0;
			calculateLightThings(sunLight, torchLight, sides[2], b, 2, y);

			currentVector->push_back(mergeShorts(slabTopFace,
				getGpuIdIndexForBlock(b.getType(), 2)));
			pushFlagsLightAndPosition(*currentVector, position, 0, isInWater,
				sunLight, torchLight, aoShape);
		}

	};

	auto blockBakeLogicForTransparentBlocks = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b, bool isAnimated
		)
	{
		int chunkPosX = data.x * CHUNK_SIZE;
		int chunkPosZ = data.z * CHUNK_SIZE;

		glm::vec3 displace[6] = {{0,0,0.5f},{0,0,-0.5f},{0,0.5f,0},{0,-0.5f,0},{-0.5f,0,0},{0.5f,0,0},};

		std::array<glm::vec2, 6> distances = {};
		for (int i = 0; i < 6; i++)
		{
			auto diff = glm::vec3(playerPosition - glm::ivec3{x, y, z} - glm::ivec3{chunkPosX, 0, chunkPosZ}) - displace[i];
			distances[i].x = glm::dot(diff, diff);
			distances[i].y = i;
		}

		std::sort(distances.begin(), distances.end(), [](glm::vec2 &a,
			glm::vec2 &b)
		{
			return a.x > b.x;
		});


		Block *sides[26] = {};
		getNeighboursLogic(x, y, z, sides);

		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y,
				z + this->data.z * CHUNK_SIZE};

		for (int index = 0; index < 6; index++)
		{
			int i = distances[index].y;

			bool isWater = b.getType() == BlockTypes::water;

			if ((sides[i] != nullptr
				&& (!(sides[i])->isOpaque() && sides[i]->getType() != b.getType())
				)||
				(
					isWater && i == 2 //display water if there is a block on top
				)
				|| (
				//(i == 3 && y == 0) ||		//display the bottom face
				(i == 2 && y == CHUNK_HEIGHT - 1)
				)
				)
			{

				//no faces in between water
				if (isWater && sides[i] && sides[i]->getType() == BlockTypes::water) { continue; }

				//no faces in between same types
				if (sides[i] && sides[i]->getType() == b.getType()) { continue; }


				if (isWater)
				{
					//front back top bottom left right
					if (i == 0 || i == 1 || i == 4 || i == 5)
					{
						int currentIndex = i;

						//bootom variant
						bool bottomVariant = 0;

						auto doABottomCheck = [&](int checkDown)
						{
							if (sides[BOTTOM])
							{
								auto blockBottom = *sides[BOTTOM];
								if (blockBottom.getType() == BlockTypes::water)
								{
									if (sides[checkDown]
										&& (sides[checkDown]->getType() == BlockTypes::water)
										)
									{
										bottomVariant = true;
									}
								}
							}
						};

						if (i == FRONT)
						{
							doABottomCheck(DOWN_FRONT);
						}
						else if (i == BACK)
						{
							doABottomCheck(DOWN_BACK);
						}
						else if (i == LEFT)
						{
							doABottomCheck(DOWN_LEFT);
						}
						else if (i == RIGHT)
						{
							doABottomCheck(DOWN_RIGHT);
						}

						bool topVariant = 1;
						if (y < CHUNK_HEIGHT - 1)
						{
							auto blockTop = unsafeGet(x, y + 1, z);
							if (blockTop.getType() != BlockTypes::water)
							{
								currentIndex += 22;
								topVariant = false;
							}
						}

						if (topVariant && !bottomVariant)
						{
							currentIndex = i; //normal block
						}
						else if(topVariant && bottomVariant)
						{
							if (i == 0) { currentIndex = 32; } //front
							if (i == 1) { currentIndex = 33; } //back
							if (i == 4) { currentIndex = 34; } //front
							if (i == 5) { currentIndex = 35; } //front
						}
						else if (!topVariant && !bottomVariant)
						{
							//normal water
							currentIndex = i + 22;
						}
						else if (!topVariant && bottomVariant)
						{
							//bottom water;
							if (i == 0) { currentIndex = 28; } //front
							if (i == 1) { currentIndex = 29; } //back
							if (i == 4) { currentIndex = 30; } //front
							if (i == 5) { currentIndex = 31; } //front
						}

						currentVector->push_back(mergeShorts(currentIndex, getGpuIdIndexForBlock(b.getType(), i)));
					}
					else
					{
						currentVector->push_back(mergeShorts(i + 22, getGpuIdIndexForBlock(b.getType(), i)));
					}

					//if (!sides[2] || sides[2]->type != BlockTypes::water)
					//{
					//	currentVector->push_back(mergeShorts(i + 22, getGpuIdIndexForBlock(b.type, i)));
					//}

				}
				else
				{
					currentVector->push_back(mergeShorts(i, getGpuIdIndexForBlock(b.getType(), i)));
				}

				int aoShape = determineAOShape(i, sides);
			
				bool isInWater = (sides[i] != nullptr) && sides[i]->getType() == BlockTypes::water;

				if (dontUpdateLightSystem)
				{
					pushFlagsLightAndPosition(*currentVector, position, isWater, isInWater,
						15, 15, aoShape);
				}
				else
					if (sides[i] == nullptr && i == 2)
					{
						pushFlagsLightAndPosition(*currentVector, position, isWater, isInWater,
							15, b.getLight(), aoShape);
					}
					else if (sides[i] == nullptr && i == 3)
					{
						pushFlagsLightAndPosition(*currentVector, position, isWater, isInWater,
							5, b.getLight(), aoShape);
						//bottom of the world
					}
					else if (sides[i] != nullptr)
					{
						int val = merge4bits(
							std::max(b.getSkyLight(),sides[i]->getSkyLight()), 
							std::max(b.getLight(),sides[i]->getLight())
						);

						pushFlagsLightAndPosition(*currentVector, position, isWater, isInWater,
							std::max(b.getSkyLight(), sides[i]->getSkyLight()),
							std::max(b.getLight(), sides[i]->getLight()), aoShape);
					}
					else
					{
						pushFlagsLightAndPosition(*currentVector, position, 
							isWater, isInWater,
							0, 0, aoShape);
					}

			}
		}
	};

	auto blockBakeLogicForGrassMesh = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b)
	{
		Block *sides[26] = {};
		getNeighboursLogic(x, y, z, sides);

		bool ocluded = 1;
		for (int i = 0; i < 6; i++)
		{
			if (sides[i] != nullptr && !sides[i]->isOpaque())
			{
				ocluded = 0;
				break;
			}
		}

		if (ocluded)return;

		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y,
			z + this->data.z * CHUNK_SIZE};

		for (int i = 6; i <= 9; i++)
		{
			//opaqueGeometry.push_back(mergeShorts(i, b.getType()));
			currentVector->push_back(mergeShorts(i, getGpuIdIndexForBlock(b.getType(), 0)));

			if (dontUpdateLightSystem)
			{
				pushFlagsLightAndPosition(*currentVector, position, 0, 0, 15, 15, 0);
			}
			else
			{
				pushFlagsLightAndPosition(*currentVector, position,
					0, 0, b.getSkyLight(), b.getLight(), 0);
			}

		}

	};

	auto blockBakeLogicForTorches = [&](int x, int y, int z,
		std::vector<int> *currentVector, Block &b)
	{
		glm::ivec3 position = {x + this->data.x * CHUNK_SIZE, y,
				z + this->data.z * CHUNK_SIZE};

		for (int i = 0; i < 6; i++)
		{
		
			if (i == 3)
			{
				auto bbottom = safeGet(x, y - 1, z);
				if (bbottom && bbottom->isOpaque()) { continue; }
			}

			currentVector->push_back(mergeShorts(i + 16, getGpuIdIndexForBlock(b.getType(), i)));

			if (dontUpdateLightSystem)
			{
				pushFlagsLightAndPosition(*currentVector, position, 0, 0, 15, 15, 0);
			}
			else
			{
				pushFlagsLightAndPosition(*currentVector, position,
					0, 0, b.getSkyLight(), b.getLight(), 0);
			}
		}

	};

	opaqueGeometry.clear();
	transparentGeometry.clear();
	transparentCandidates.clear();
	lights.clear();

#pragma endregion

	if (updateGeometry)
	{
		setDirty(0);
		setNeighbourToLeft(left != nullptr);
		setNeighbourToRight(right != nullptr);
		setNeighbourToFront(front != nullptr);
		setNeighbourToBack(back != nullptr);

		for (int x = 0; x < CHUNK_SIZE; x++)
			for (int z = 0; z < CHUNK_SIZE; z++)
				for (int y = 0; y < CHUNK_HEIGHT; y++)
				{
					auto &b = unsafeGet(x, y, z);
					if (!b.air())
					{
						if (b.isStairsMesh())
						{

							blockBakeLogicForStairs(x, y, z, &opaqueGeometry, b);

						}
						else if (b.isSlabMesh())
						{
							blockBakeLogicForSlabs(x, y, z, &opaqueGeometry, b);
						}else
						if (b.isGrassMesh())
						{
							blockBakeLogicForGrassMesh(x, y, z, &opaqueGeometry, b);
						}
						else if (b.getType() == BlockTypes::torch)
						{
							blockBakeLogicForTorches(x, y, z, &opaqueGeometry, b);
						}else
						{
							if (!b.isTransparentGeometry())
							{
								blockBakeLogicForSolidBlocks(x, y, z, &opaqueGeometry, b, b.isAnimatedBlock());
							}
						}

						if (b.isLightEmitor())
						{
							lights.push_back({x + data.x * CHUNK_SIZE, y, z + data.z * CHUNK_SIZE,0});
						}
					}
				}

		//trying to place the data in a better way for the gpu
		arangeData(opaqueGeometry);
	}

	if (updateTransparency)
	{
		setDirtyTransparency(0);

		int chunkPosX = data.x * CHUNK_SIZE;
		int chunkPosZ = data.z * CHUNK_SIZE;

		for (int x = 0; x < CHUNK_SIZE; x++)
			for (int z = 0; z < CHUNK_SIZE; z++)
				for (int y = 0; y < CHUNK_HEIGHT; y++)
				{
					auto &b = unsafeGet(x, y, z);

					//transparent geometry doesn't include air
					if (b.isTransparentGeometry())
					{
						glm::vec3 difference = playerPosition - glm::ivec3{x, y, z} - glm::ivec3{chunkPosX, 0, chunkPosZ};
						float distance = glm::dot(difference, difference);
						transparentCandidates.push_back({{x,y,z}, distance});
					}

				}

		std::sort(transparentCandidates.begin(), transparentCandidates.end(), [](TransparentCandidate &a,
			TransparentCandidate &b)
		{
			return a.distance > b.distance;
		});

		for (auto &c : transparentCandidates)
		{
			auto &b = unsafeGet(c.position.x, c.position.y, c.position.z);
			blockBakeLogicForTransparentBlocks(c.position.x, c.position.y, c.position.z, &transparentGeometry, b,
				b.isAnimatedBlock());
		}

	};

	if (updateGeometry)
	{
		glBindBuffer(GL_ARRAY_BUFFER, opaqueGeometryBuffer);

		//glBufferStorage(GL_ARRAY_BUFFER, opaqueGeometry.size() * sizeof(opaqueGeometry[0]),
		//	opaqueGeometry.data(), GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT);
		glBufferData(GL_ARRAY_BUFFER, opaqueGeometry.size() * sizeof(opaqueGeometry[0]),
			opaqueGeometry.data(), GL_STATIC_DRAW);

		elementCountSize = opaqueGeometry.size() / 4; //todo magic number

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, lights.size() * sizeof(lights[0]),
			lights.data(), GL_STREAM_READ);
		lightsElementCountSize = lights.size();

		gpuBuffer.addChunk({data.x, data.z}, opaqueGeometry);
	}

	if (updateTransparency)
	{
		glBindBuffer(GL_ARRAY_BUFFER, transparentGeometryBuffer);
		glBufferData(GL_ARRAY_BUFFER, transparentGeometry.size() * sizeof(transparentGeometry[0]),
			transparentGeometry.data(), GL_STATIC_DRAW);
		//glBufferStorage(GL_ARRAY_BUFFER, transparentGeometry.size() * sizeof(transparentGeometry[0]),
		//	transparentGeometry.data(), GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT);

		transparentElementCountSize = transparentGeometry.size() / 4; //todo magic number
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	if ((updateTransparency && transparentElementCountSize > 0) || updateGeometry)
	{
		return true;
	}
	else
	{
		return false;
	}

}

bool Chunk::shouldBakeOnlyBecauseOfTransparency(Chunk *left, Chunk *right, Chunk *front, Chunk *back)
{
	if (
		isDirty()
		|| (!isNeighbourToLeft() && left != nullptr)
		|| (!isNeighbourToRight() && right != nullptr)
		|| (!isNeighbourToFront() && front != nullptr)
		|| (!isNeighbourToBack() && back != nullptr)
		)
	{
		return false;
	}

	return isDirtyTransparency();
}



void Chunk::createGpuData()
{
	//unsigned char winding[4] = {0,1,2,4};
	unsigned char winding[4] = {0,1,2,3};

	glGenBuffers(1, &opaqueGeometryBuffer);
	glGenBuffers(1, &opaqueGeometryIndex);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, opaqueGeometryBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, opaqueGeometryIndex);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(winding), winding, GL_STATIC_DRAW);
	setupVertexAttributes();

	glGenBuffers(1, &transparentGeometryBuffer);
	glGenBuffers(1, &transparentGeometryIndex);
	glGenVertexArrays(1, &transparentVao);
	glBindVertexArray(transparentVao);
	glBindBuffer(GL_ARRAY_BUFFER, transparentGeometryBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, transparentGeometryIndex);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(winding), winding, GL_STATIC_DRAW);
	setupVertexAttributes();

	glBindVertexArray(0);

	glGenBuffers(1, &lightsBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

}

void Chunk::clearGpuData(BigGpuBuffer *gpuBuffer)
{
	glDeleteBuffers(1, &opaqueGeometryBuffer);
	glDeleteBuffers(1, &opaqueGeometryIndex);
	glDeleteBuffers(1, &transparentGeometryBuffer);
	glDeleteBuffers(1, &transparentGeometryIndex);
	glDeleteBuffers(1, &lightsBuffer);
	glDeleteVertexArrays(1, &vao);
	glDeleteVertexArrays(1, &transparentVao);

	if (gpuBuffer)
	{
		gpuBuffer->removeChunk({data.x, data.z});
	}
}

void ChunkData::clearLightLevels()
{

	for (int x = 0; x < CHUNK_SIZE; x++)
		for (int z = 0; z < CHUNK_SIZE; z++)
			for (int y = 0; y < CHUNK_HEIGHT; y++)
			{
				unsafeGet(x, y, z).lightLevel = 0;
				unsafeGet(x, y, z).notUsed = 0;
			}

}



int modBlockToChunk(int x)
{
	if (x < 0)
	{
		x = -x;
		x--;
		return CHUNK_SIZE - (x % CHUNK_SIZE) - 1;
	}
	else
	{
		return x % CHUNK_SIZE;
	}
}

glm::ivec2 modBlockToChunk(glm::ivec2 x)
{
	return glm::ivec2(modBlockToChunk(x.x), modBlockToChunk(x.y));
}
