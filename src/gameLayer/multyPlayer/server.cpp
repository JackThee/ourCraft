#include "multyPlayer/server.h"
#include <glm/vec3.hpp>
#include "chunkSystem.h"
#include "threadStuff.h"
#include <thread>
#include <mutex>
#include <queue>
#include "worldGenerator.h"
#include <thread>
#include <unordered_map>
#include <iostream>
#include <atomic>
#include <enet/enet.h>
#include "multyPlayer/packet.h"
#include "multyPlayer/enetServerFunction.h"
#include <platformTools.h>
#include <iostream>

struct ListNode;

struct SavedChunk
{
	//std::mutex mu;
	ChunkData chunk;
	ListNode *node = nullptr;
};

//https://www.geeksforgeeks.org/how-to-create-an-unordered_map-of-user-defined-class-in-cpp/
struct ChunkHash
{
	size_t operator()(const glm::ivec2 &in) const
	{
		int x = in.x;
		int z = in.y;

		size_t ret = 0;
		ret += x;
		ret += (z < 32);

		return ret;
	}
};


struct ListNode
{
	ListNode *prev = nullptr;
	glm::ivec2 chunkPos = {};
	ListNode *next = nullptr;
};


static std::atomic<bool> serverRunning = false;
std::thread serverThread;

bool serverStartupStuff();

bool isServerRunning()
{
	return serverRunning;
}

bool startServer()
{
	bool expected = 0;
	if (serverRunning.compare_exchange_strong(expected, 1))
	{
		if (!serverStartupStuff())
		{
			serverRunning = false;
			return 0;
		}

		serverThread = std::move(std::thread(serverWorkerFunction));
		return 1;
	}
	else
	{
		return 0;
	}
}

const int maxSavedChunks = 2000;

struct ChunkPriorityCache
{
	std::unordered_map<glm::ivec2, SavedChunk *, ChunkHash> savedChunks = {};
	ListNode *first = nullptr;
	ListNode *last = nullptr;

	ChunkData *getOrCreateChunk(int posX, int posZ, WorldGenerator &wg);

};

std::mutex serverSettingsMutex;

struct ServerData
{
	ChunkPriorityCache chunkCache = {};
	std::vector<ServerTask> waitingTasks = {};
	ENetHost *server = nullptr;
	ServerSettings settings = {};

}sd;

void closeServer()
{
	//toto cleanup stuff
	if (serverRunning)
	{
		closeEnetListener();

		//close loop
		serverRunning = false;

		//then signal the barier from the task waiting
		signalWaitingFromServer();

		//then wait for the server to close
		serverThread.join();

		enet_host_destroy(sd.server);
		//todo clear othher stuff
		sd = {};
	}

	serverSettingsMutex.unlock();
}

bool serverStartupStuff()
{
	//reset data
	sd = ServerData{};


	//start enet server
	ENetAddress adress;
	adress.host = ENET_HOST_ANY;
	adress.port = 7771;
	ENetEvent event;

	//first param adress, players limit, channels, bandwith limit
	sd.server = enet_host_create(&adress, 32, SERVER_CHANNELS, 0, 0);

	if (!sd.server)
	{
		//todo some king of error reporting to the player
		return 0;
	}

	if (!startEnetListener(sd.server))
	{
		enet_host_destroy(sd.server);
		sd.server = 0;
		return 0;
	}

	return true;
}

void serverWorkerFunction()
{
	WorldGenerator wg;
	wg.init();

	while (serverRunning)
	{
		auto settings = getServerSettingsCopy();

		std::vector<ServerTask> tasks;
		if (sd.waitingTasks.empty())
		{
			tasks = waitForTasksServer(); //nothing to do we can wait.
		}
		else
		{
			tasks = tryForTasksServer(); //already things to do, we just grab more if ready and wating.
		}

		for (auto i : tasks)
		{
			sd.waitingTasks.push_back(i);
		}

		std::sort(sd.waitingTasks.begin(), sd.waitingTasks.end(),
			[](const ServerTask &a, const ServerTask &b) { return a.t.type < b.t.type; });

		int count = sd.waitingTasks.size();
		for (int taskIndex = 0; taskIndex < std::min(count, 3); taskIndex++)
		{
			auto &i = sd.waitingTasks.front();

			if (i.t.type == Task::generateChunk)
			{
				auto rez = sd.chunkCache.getOrCreateChunk(i.t.pos.x, i.t.pos.z, wg);
				
				Packet packet;
				packet.cid = i.cid;
				packet.header = headerRecieveChunk;

				Packet_RecieveChunk *packetData = new Packet_RecieveChunk(); //todo ring buffer here

				packetData->chunk = *rez;

				auto client = getClient(i.cid); //todo this could fail when players leave so return pointer and check

				sendPacket(client.peer, packet, (char *)packetData, sizeof(Packet_RecieveChunk), true, 0);

				delete packetData;
			}
			else
			if (i.t.type == Task::placeBlock)
			{
				//std::cout << "server recieved place block\n";
				//auto chunk = sd.chunkCache.getOrCreateChunk(i.t.pos.x / 16, i.t.pos.z / 16);
				auto chunk = sd.chunkCache.getOrCreateChunk(divideChunk(i.t.pos.x), divideChunk(i.t.pos.z), wg);
				int convertedX = modBlockToChunk(i.t.pos.x);
				int convertedZ = modBlockToChunk(i.t.pos.z);
				
				//todo check if place is legal
				bool legal = 1;
				bool noNeedToNotifyUndo = 0;
				auto client = getClient(i.cid);

				if (client.revisionNumber > i.t.eventId.revision)
				{
					//if the revision number is increased it means that we already undoed all those moes
					legal = false;
					noNeedToNotifyUndo = true;
					std::cout << "Server revision number ignore: " << client.revisionNumber << " "
						<< i.t.eventId.revision << "\n";
				}

				{
					auto f = settings.perClientSettings.find(i.cid);
					if (f != settings.perClientSettings.end())
					{
						if (!f->second.validateStuff)
						{
							legal = false;
						}

					}
				}

				auto b = chunk->safeGet(convertedX, i.t.pos.y, convertedZ);
				if (b && legal)
				{
					b->type = i.t.blockType;
					//todo mark this chunk dirty if needed for saving

					//todo enum for chanels
					{
						Packet packet;
						packet.cid = i.cid;
						packet.header = headerPlaceBlock;

						Packet_PlaceBlock packetData;
						packetData.blockPos = i.t.pos;
						packetData.blockType = i.t.blockType;


						broadCast(packet, &packetData, sizeof(Packet_PlaceBlock), client.peer, true, 0);
						//sendPacket(client.peer, packet, (char *)&packetData, sizeof(Packet_PlaceBlock), true, 0);
					}

					{
						Packet packet;
						packet.cid = i.cid;
						packet.header = headerValidateEvent;


						Packet_ValidateEvent packetData;
						packetData.eventId = i.t.eventId;

						sendPacket(client.peer, packet, (char *)&packetData, sizeof(Packet_ValidateEvent), true, 0);
					}


				}
				else if(!noNeedToNotifyUndo)
				{
					Packet packet;
					packet.cid = i.cid;
					packet.header = headerInValidateEvent;

					Packet_InValidateEvent packetData;
					packetData.eventId = i.t.eventId;

					client.revisionNumber++;

					sendPacket(client.peer, packet, (char *)&packetData, sizeof(Packet_ValidateEvent), true, 0);
				}

			}

			sd.waitingTasks.erase(sd.waitingTasks.begin());

			if (i.t.type == Task::generateChunk)
			{
				break;
			}
		}


	}

	wg.clear();
}

ServerSettings getServerSettingsCopy()
{
	serverSettingsMutex.lock();
	auto copy = sd.settings;
	serverSettingsMutex.unlock();
	return copy;
}

void setServerSettings(ServerSettings settings)
{
	serverSettingsMutex.lock();
	for (auto &s : sd.settings.perClientSettings)
	{
		auto it = settings.perClientSettings.find(s.first);
		if (it != settings.perClientSettings.end())
		{
			s.second = it->second;
		}
	}
	serverSettingsMutex.unlock();
}

void addCidToServerSettings(CID cid)
{
	serverSettingsMutex.lock();
	sd.settings.perClientSettings.insert({cid, {}});
	serverSettingsMutex.unlock();
}

void removeCidFromServerSettings(CID cid)
{
	serverSettingsMutex.lock();
	sd.settings.perClientSettings.erase(cid);
	serverSettingsMutex.unlock();
}



ChunkData *ChunkPriorityCache::getOrCreateChunk(int posX, int posZ, WorldGenerator &wg)
{
	glm::ivec2 pos = {posX, posZ};
	auto foundPos = savedChunks.find(pos);
	if (foundPos != savedChunks.end())
	{
		auto nod = (*foundPos).second->node;

		auto left = nod->prev;
		auto right = nod->next;

		if (left == nullptr)
		{
			//nothing, this is the first element
		}
		else if (right == nullptr)
		{
			left->next = nullptr;
			last = left;

			nod->next = first;
			if (first)
			{
				first->prev = nod;
			}

			nod->prev = nullptr;
			first = nod;
		}
		else
		{
			//link left right:
			left->next = right;
			right->prev = left;

			nod->next = first;
			if (first)
			{
				first->prev = nod;
			}

			nod->prev = nullptr;
			first = nod;
		}

		//submitChunk(new Chunk((*foundPos).second->chunk));
		return &((*foundPos).second->chunk);
	}
	else
	{

		if (savedChunks.size() >= maxSavedChunks)
		{
			//std::cout << "bad";
			glm::ivec2 oldPos = last->chunkPos;
			auto foundPos = savedChunks.find(oldPos);
			assert(foundPos != savedChunks.end());//unlink of the 2 data structures

			SavedChunk *chunkToRecycle = foundPos->second;

			//here we can save the chunk to a file

			savedChunks.erase(foundPos);

			chunkToRecycle->chunk.x = pos.x;
			chunkToRecycle->chunk.z = pos.y;

			generateChunk(1234, chunkToRecycle->chunk, wg);
			auto rez = &chunkToRecycle->chunk;
			//submitChunk(new Chunk(chunkToRecycle->chunk));

			assert(chunkToRecycle->node == last);
			last->chunkPos = pos;

			auto prev = last->prev;
			prev->next = nullptr;

			auto newFirst = last;
			last = prev;

			first->prev = newFirst;
			newFirst->prev = nullptr;
			newFirst->next = first;
			first = newFirst;

			savedChunks[pos] = chunkToRecycle;

			return rez;
		}
		else
		{

			SavedChunk *c = new SavedChunk;
			c->chunk.x = posX;
			c->chunk.z = posZ;

			generateChunk(1234, c->chunk, wg);
			//std::this_thread::sleep_for(std::chrono::milliseconds(1));
			//submitChunk(new Chunk(c->chunk));
			auto rez = &c->chunk;

			ListNode *node = new ListNode;
			if (last == nullptr) { last = node; }

			node->prev = nullptr;
			node->next = first;
			node->chunkPos = {posX, posZ};

			if (first)
			{
				first->prev = node;
			}

			first = node;

			c->node = node;
			savedChunks[{posX, posZ}] = c;

			return rez;
		}
	}
}
