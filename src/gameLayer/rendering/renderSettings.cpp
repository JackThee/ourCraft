#include <rendering/renderSettings.h>
#include <gamePlayLogic.h>
#include <filesystem>
#include <iostream>
#include <platform/platformInput.h>
#include "multyPlayer/createConnection.h"

void displayRenderSettingsMenuButton(ProgramData &programData)
{
	programData.ui.menuRenderer.BeginMenu("Rendering", Colors_Gray, programData.ui.buttonTexture);

	displayRenderSettingsMenu(programData);

	programData.ui.menuRenderer.EndMenu();
}

void displayRenderSettingsMenu(ProgramData &programData)
{


	programData.ui.menuRenderer.Text("Rendering Settings...", Colors_White);

	programData.ui.menuRenderer.sliderInt("View Distance", &programData.otherSettings.viewDistance,
		1, 40, Colors_White, programData.ui.buttonTexture, Colors_Gray,
		programData.ui.buttonTexture, Colors_White);

#pragma region water
{
	programData.ui.menuRenderer.BeginMenu("Water", Colors_Gray, programData.ui.buttonTexture);

	programData.ui.menuRenderer.Text("Water settings...", Colors_White);


	programData.ui.menuRenderer.colorPicker("Water color",
		&programData.renderer.defaultShader.shadingSettings.waterColor[0],
		programData.ui.buttonTexture, programData.ui.buttonTexture, Colors_Gray, Colors_Gray);

	programData.ui.menuRenderer.colorPicker("Under water color",
		&programData.renderer.defaultShader.shadingSettings.underWaterColor[0],
		programData.ui.buttonTexture, programData.ui.buttonTexture, Colors_Gray, Colors_Gray);

	programData.ui.menuRenderer.sliderFloat("Underwater Fog strength",
		&programData.renderer.defaultShader.shadingSettings.underwaterDarkenStrength,
		0, 1, Colors_White, programData.ui.buttonTexture, Colors_Gray, 
		programData.ui.buttonTexture, Colors_White
		);

	programData.ui.menuRenderer.sliderFloat("Underwater Fog Distance",
		&programData.renderer.defaultShader.shadingSettings.underwaterDarkenDistance,
		0, 40, Colors_White, programData.ui.buttonTexture, Colors_Gray,
		programData.ui.buttonTexture, Colors_White);

	programData.ui.menuRenderer.sliderFloat("Underwater Fog Gradient",
		&programData.renderer.defaultShader.shadingSettings.fogGradientUnderWater,
		0, 32, Colors_White, programData.ui.buttonTexture, Colors_Gray,
		programData.ui.buttonTexture, Colors_White);

	static glm::vec4 colors[] = {{0.6,0.9,0.6,1}, Colors_Red};

	programData.ui.menuRenderer.toggleOptions("Water type: ", "cheap|fancy",
		&programData.renderer.waterRefraction, true, Colors_White, colors, programData.ui.buttonTexture,
		Colors_Gray,
		"How the water should be rendered\n-Cheap: \
good performance.\n-Fancy: significant performance cost but looks very nice.");

	programData.ui.menuRenderer.EndMenu();
}
#pragma endregion

	static glm::vec4 colorsTonemapper[] = {{0.6,0.9,0.6,1}, {0.6,0.9,0.6,1}, {0.7,0.8,0.6,1}};
	programData.ui.menuRenderer.toggleOptions("Tonemapper: ",
		"ACES|AgX|ZCAM", &programData.renderer.defaultShader.shadingSettings.tonemapper,
		true, Colors_White, colorsTonemapper, programData.ui.buttonTexture,
		Colors_Gray, 
"The tonemapper is the thing that displays the final color\n\
-Aces: a filmic look.\n-AgX: a more dull neutral look.\n-ZCAM a verey neutral and vanila look\n   preserves colors, slightly more expensive.");

	
	programData.ui.menuRenderer.newColum(2);

	programData.ui.menuRenderer.Text("", {});
	
	displayTexturePacksSettingsMenuButton(programData);

	//programData.menuRenderer.BeginMenu("Volumetric", Colors_Gray, programData.buttonTexture);
	//programData.menuRenderer.Text("Volumetric Settings...", Colors_White);
	programData.ui.menuRenderer.sliderFloat("Fog gradient (O to disable it)",
		&programData.renderer.defaultShader.shadingSettings.fogCloseGradient,
		0, 64, Colors_White, programData.ui.buttonTexture, Colors_Gray,
		programData.ui.buttonTexture, Colors_White);
	//programData.menuRenderer.EndMenu();

	static glm::vec4 colorsShadows[] = {{0.0,1,0.0,1}, {0.8,0.6,0.6,1}, {0.9,0.3,0.3,1}};
	programData.ui.menuRenderer.toggleOptions("Shadows: ", "Off|Hard|Soft",
		&programData.renderer.defaultShader.shadingSettings.shadows, true,
		Colors_White, colorsShadows, programData.ui.buttonTexture,
		Colors_Gray, "Shadows can affect the performance significantly."
	);

}

glm::ivec4 shrinkPercentage(glm::ivec4 dimensions, glm::vec2 p)
{
	glm::vec4 b = dimensions;

	b.x += (b.z * p.x) / 2.f;
	b.y += (b.w * p.y) / 2.f;

	b.z *= (1.f - p.x);
	b.w *= (1.f - p.y);

	dimensions = b;
	return dimensions;
}

bool texturePackDirty = 0;
int leftAdvance = 0;
int rightAdvance = 0;
std::vector<std::filesystem::path> loadedTexturePacks;
std::unordered_map<std::string, gl2d::Texture> logoTextures;

std::vector<std::filesystem::path> usedTexturePacks;

bool shouldReloadTexturePacks()
{
	return texturePackDirty;
}

std::vector<std::filesystem::path> getUsedTexturePacksAndResetFlag()
{
	texturePackDirty = false;
	return usedTexturePacks;
}

bool isTexturePackUsed(std::string t)
{
	for (auto &t2 : usedTexturePacks)
	{
		if (t == t2) { return true; }
	}
	return false;
}

void useTexturePack(std::string t)
{
	if (!isTexturePackUsed(t))
	{
		usedTexturePacks.push_back(t);
		texturePackDirty = true;
	}
}

void unuseTexturePack(std::string t)
{
	auto f = std::find(usedTexturePacks.begin(), usedTexturePacks.end(), t);

	if (f != usedTexturePacks.end())
	{
		usedTexturePacks.erase(f);
		texturePackDirty = true;
	}
}

void displayTexturePacksSettingsMenuButton(ProgramData &programData)
{
	programData.ui.menuRenderer.BeginMenu("Textures Packs", Colors_Gray, programData.ui.buttonTexture);

	displayTexturePacksSettingsMenu(programData);

	programData.ui.menuRenderer.EndMenu();
}


void openFolder(const char *path)
{
	//TODO!
#if defined(_WIN32) || defined(_WIN64)
	std::string command = "explorer ";
	command += path;
	system(command.c_str());
#elif defined(__APPLE__) || defined(__MACH__)
	std::string command = "open ";
	command += path;
	system(command.c_str());
#elif defined(__linux__)
	std::string command = "xdg-open ";
	command += path;
	system(command.c_str());
#else
	
#endif
}

inline void stubErrorFunc(const char *msg, void *userDefinedData)
{
	
}


bool renderButton(gl2d::Renderer2D &renderer,
	gl2d::Texture buttonTexture,
	glm::ivec4 box, const std::string &text = "", gl2d::Font *f = 0)
{
	bool hovered = 0;
	bool held = 0;
	bool released = 0;
	auto cursorPos = platform::getRelMousePosition();

	if (glui::aabb(box, cursorPos))
	{
		hovered = 1;

		if (platform::isLMouseHeld())
		{
			held = 1;
		}
		else if (platform::isLMouseReleased())
		{
			released = 1;
		}
	}

	if (held)
	{
		box.y += 10;
	}

	if (buttonTexture.id)
	{

		auto color = Colors_Gray;

		if (hovered)
		{
			color += glm::vec4(0.1, 0.1, 0.1, 0);
		}

		renderer.render9Patch(box,
			20, color, {}, 0, buttonTexture,
			GL2D_DefaultTextureCoords, {0.2,0.8,0.8,0.2});
	}

	if (text.size() && f)
	{
		glui::renderText(renderer, text, *f, box, Colors_White, 0, true, true);
	}

	return released;
};


void displayTexturePacksSettingsMenu(ProgramData &programData)
{
	std::error_code err;


	programData.ui.menuRenderer.Text("Texture packs...", Colors_White);

	//todo
	//if (programData.ui.menuRenderer.Button("Open Folder", Colors_Gray))
	//{
	//	if (!std::filesystem::exists(RESOURCES_PATH "texturePacks"))
	//	{
	//		std::filesystem::create_directories(RESOURCES_PATH "texturePacks", err);
	//	}
	//	
	//	openFolder(RESOURCES_PATH "texturePacks");
	//}


	glm::vec4 customWidgetTransform = {};
	programData.ui.menuRenderer.CustomWidget(169, &customWidgetTransform);



	//programData.ui.menuRenderer.cu
	auto &renderer = programData.ui.renderer2d;


	auto renderBox = [&](glm::vec4 c)
	{
		renderer.render9Patch(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f)(),
			20, c, {}, 0, programData.ui.buttonTexture, GL2D_DefaultTextureCoords, {0.2,0.8,0.8,0.2});
	};

	if (programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].size()
		&& programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].back() == "Textures Packs"
		)
	{

		std::vector<std::filesystem::path> allTexturePacks;

		if (!std::filesystem::exists(RESOURCES_PATH "texturePacks"))
		{
			std::filesystem::create_directories(RESOURCES_PATH "texturePacks", err);
		}

		for (auto const &d : std::filesystem::directory_iterator{RESOURCES_PATH "texturePacks", err})
		{
			if (d.is_directory())
			{
				allTexturePacks.push_back(d.path().filename());
			}
		}

		//load logo textures
		gl2d::setErrorFuncCallback(stubErrorFunc);
		for (auto &pack : allTexturePacks)
		{
			auto file = pack;
			file = (RESOURCES_PATH "texturePacks") / file;
			file /= "logo.png";

			if (logoTextures.find(pack.string()) == logoTextures.end())
			{
				gl2d::Texture t;
				t.loadFromFile(file.string().c_str());

				if (!t.id)
				{
					file = pack;
					file = (RESOURCES_PATH "texturePacks") / file;
					file /= "pack.png";
					t.loadFromFile(file.string().c_str(), true);

				}

				logoTextures[pack.string()] = t;
			}
		}
		gl2d::setErrorFuncCallback(gl2d::defaultErrorFunc);
		//TODO delete unused entries


		auto listImplementation = [&](bool left)
		{
			std::vector<std::filesystem::path> *listPacks = 0;
			int *advance = 0;
			//buttons
			glm::ivec4 b;
			if (left)
			{
				b = glui::Box().xLeft().yTop().
					xDimensionPercentage(0.5).yDimensionPercentage(1).shrinkPercentage({0.05,0.05});
				listPacks = &allTexturePacks;
				advance = &leftAdvance;
			}
			else
			{
				b = glui::Box().xRight().yTop().
					xDimensionPercentage(0.5).yDimensionPercentage(1).shrinkPercentage({0.05,0.05});
				listPacks = &usedTexturePacks;
				advance = &rightAdvance;
			}

			glui::Frame f(b);

			float buttonSize = glui::Box().xLeft().yTop().xDimensionPercentage(0.1)().z;

			auto currentUpperBox = glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f)();

			//center
			{

				glui::Frame f(glui::Box().xCenter().yCenter().
					xDimensionPercentage(1).
					yDimensionPixels(currentUpperBox.w - buttonSize * 2)());

				auto currentBox = glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f)();

				//renderer.renderRectangle(,
				//	{0,1,0,0.2});

				renderBox({0.6,0.6,0.6,0.5});

				int height = currentBox.w - 10;

				int maxH = std::min(height, int(currentBox.z * 0.2));

				auto defaultB = glui::Box().xLeft().yTop().xDimensionPercentage(1).yDimensionPixels(maxH)();
				defaultB.y += 10;

				auto renderOneTeturePack = [&](glm::ivec4 box,
					std::string name)
				{
					box = shrinkPercentage(box, {0.1,0.05});

					renderer.render9Patch(box,
						20, {0.6,0.6,0.6,0.9}, {}, 0, programData.ui.buttonTexture,
						GL2D_DefaultTextureCoords, {0.2,0.8,0.8,0.2});

					{
						glui::Frame f(box);

						auto button = glui::Box().xRight().yTop().yDimensionPercentage(1.f).xDimensionPixels(buttonSize)();
						if (renderButton(renderer, programData.ui.buttonTexture, button))
						{
							if (left)
							{
								useTexturePack(name);
							}
							else
							{
								unuseTexturePack(name);
							}
						}

						auto icon = glui::Box().xLeft().yTop().yDimensionPercentage(1.f).xDimensionPixels(box.w)();

						if (logoTextures[name].id)
						{
							glui::renderTexture(renderer, shrinkPercentage(icon, {0.1,0.1}),
								logoTextures[name], Colors_White, GL2D_DefaultTextureCoords);
						}
						else
						{
							glui::renderTexture(renderer, shrinkPercentage(icon, {0.1,0.1}),
								programData.defaultCover, Colors_White, GL2D_DefaultTextureCoords);
						}


						auto text = icon;
						text.x += icon.z;
						text.z = button.x - text.x;

						glui::renderText(renderer, name, programData.ui.font, text, Colors_White, true);
					}


				};

				int canRenderCount = height / defaultB.w;

				int overflow = listPacks->size() - canRenderCount;
				if (overflow < 0) { overflow = 0; }
				if (*advance > overflow) { *advance = overflow; }

				for (int i = 0; i < listPacks->size(); i++)
				{
					if (i >= canRenderCount) { break; }

					renderOneTeturePack(defaultB, (*listPacks)[i + *advance].filename().string());
					defaultB.y += defaultB.w;
				}


			}

			//bottom button
			{
				auto currentDownBox = glui::Box().xLeft().yBottom().xDimensionPercentage(1).yDimensionPixels(buttonSize)();

				if (renderButton(renderer, programData.ui.buttonTexture, currentDownBox))
				{
					(*advance)++;
				}
			}

			//top button
			{
				auto currentUpperBox = glui::Box().xLeft().yTop().xDimensionPercentage(1).yDimensionPixels(buttonSize)();

				if (renderButton(renderer, programData.ui.buttonTexture, currentUpperBox))
				{
					(*advance)--;
				}
			}

			if (*advance < 0) { *advance = 0; }

		};
		

		glui::Frame f({0,0,renderer.windowW, renderer.windowH});

		{
			float ySize = renderer.windowH - customWidgetTransform.y - customWidgetTransform.w/2.f;

			glui::Frame f(glui::Box().xCenter().yTop(customWidgetTransform.y).
				yDimensionPixels(ySize).xDimensionPercentage(0.9)());

			//renderer.renderRectangle(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f),
			//	{1,0,0,0.2});
			renderBox({0.2,0.2,0.2,0.4});

			//left
			{
				listImplementation(true);
			}

			//rioght
			{
				listImplementation(false);
			}

		}

	}
}

std::string currentSkinSelected = "";

gl2d::Texture currentTextureLoaded;
std::string currentTextureLoadedName = "";

void loadTexture()
{

	if (currentTextureLoadedName != currentSkinSelected || !currentTextureLoaded.id)
	{
		currentTextureLoaded.cleanup();
	}

	if(!currentTextureLoaded.id)
	{
		currentTextureLoadedName = currentSkinSelected;

		if (currentTextureLoadedName == "")
		{
			currentTextureLoaded
				= loadPlayerSkin(RESOURCES_PATH "assets/models/steve.png");
		}
		else
		{
			currentTextureLoaded
				= loadPlayerSkin((RESOURCES_PATH "skins/" + currentTextureLoadedName + ".png").c_str());
		}
	}

}

std::string getSkinName()
{
	return currentSkinSelected;
}

void displaySkinSelectorMenu(ProgramData &programData)
{
	std::error_code err;

	programData.ui.menuRenderer.Text("Change Skin", Colors_White);

	//todo
	//if (programData.ui.menuRenderer.Button("Open Folder", Colors_Gray))
	//{
	//	if (!std::filesystem::exists(RESOURCES_PATH "texturePacks"))
	//	{
	//		std::filesystem::create_directories(RESOURCES_PATH "texturePacks", err);
	//	}
	//	
	//	openFolder(RESOURCES_PATH "texturePacks");
	//}

	glm::vec4 customWidgetTransform = {};
	programData.ui.menuRenderer.CustomWidget(170, &customWidgetTransform);

	//programData.ui.menuRenderer.cu
	auto &renderer = programData.ui.renderer2d;
	
	auto renderBox = [&](glm::vec4 c)
	{
		renderer.render9Patch(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f)(),
			20, c, {}, 0, programData.ui.buttonTexture, GL2D_DefaultTextureCoords, {0.2,0.8,0.8,0.2});
	};

	if (programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].size()
		&& programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].back() == "Change Skin"
		)
	{

		if (!std::filesystem::exists(RESOURCES_PATH "skins"))
		{
			std::filesystem::create_directories(RESOURCES_PATH "skins", err);
		}

		std::vector<std::string> skins;

		for (auto const &d : std::filesystem::directory_iterator{RESOURCES_PATH "skins", err})
		{
			if (!d.is_directory())
			{
				if (d.path().filename().extension() == ".png")
				{
					skins.push_back(d.path().filename().stem().string());
				}
			}
		}

		int posInVect = -1;
		if (currentSkinSelected == "")
		{
			posInVect = -1;
		}
		else
		{
			int index = 0;
			for(auto &s : skins)
			{
				if (s == currentSkinSelected)
				{
					currentSkinSelected = s;
					posInVect = index;
					break;
				}
				index++;
			}
		}

		

		glui::Frame f({0,0,renderer.windowW, renderer.windowH});
		{
			float ySize = renderer.windowH - customWidgetTransform.y - customWidgetTransform.w / 2.f;

			glui::Frame f(glui::Box().xCenter().yTop(customWidgetTransform.y).
				yDimensionPixels(ySize).xDimensionPercentage(0.9)());

			//renderer.renderRectangle(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f),
			//	{1,0,0,1.0});
			renderBox({0.4,0.4,0.4,0.5});


			auto textBox = glui::Box().xCenter().yBottom().xDimensionPercentage(1).yDimensionPercentage(0.2)();
			if (currentSkinSelected == "")
			{
				glui::renderText(renderer, "Default", programData.ui.font, textBox, Colors_White, true);
			}
			else
			{
				glui::renderText(renderer, currentSkinSelected, programData.ui.font, textBox, Colors_White, true);
			}

			auto center = glui::Box().xCenter().yCenter().yDimensionPercentage(0.5).xDimensionPercentage(0.5)();
			center.z = std::min(center.z, center.w);
			center = glui::Box().xCenter().yCenter().yDimensionPixels(center.z).xDimensionPixels(center.z)();

			loadTexture();
			if (currentTextureLoaded.id)
			{
				renderer.renderRectangle(center, currentTextureLoaded);
			}

			auto left = glui::Box().xLeft().yCenter().yDimensionPercentage(0.2).xAspectRatio(1.0)();
			auto right = glui::Box().xRight().yCenter().yDimensionPercentage(0.2).xAspectRatio(1.0)();

			//move cursor
			{
				if (renderButton(renderer, programData.ui.buttonTexture, left))
				{
					posInVect--;
				}

				if (renderButton(renderer, programData.ui.buttonTexture, right))
				{
					posInVect++;
				}

				if (posInVect < -1)
				{
					posInVect = skins.size() - 1;
				}

				if (posInVect >= skins.size())
				{
					posInVect = -1;
				}

				if (posInVect == -1)
				{
					currentSkinSelected = "";
				}
				else
				{
					currentSkinSelected = skins[posInVect];
				}
			}

		
		}
	}

}

void displaySkinSelectorMenuButton(ProgramData &programData)
{
	programData.ui.menuRenderer.BeginMenu("Change Skin", Colors_Gray, programData.ui.buttonTexture);

	displaySkinSelectorMenu(programData);

	programData.ui.menuRenderer.EndMenu();
}


void displayWorldSelectorMenuButton(ProgramData &programData)
{
	programData.ui.menuRenderer.BeginMenu("Play", Colors_Gray, programData.ui.buttonTexture);

	displayWorldSelectorMenu(programData);

	programData.ui.menuRenderer.EndMenu();
}


void displayWorldSelectorMenu(ProgramData &programData)
{
	auto &renderer = programData.ui.renderer2d;



	programData.ui.menuRenderer.Text("Select world", Colors_White);

	//programData.ui.menuRenderer.Button("Create new world", Colors_Gray, programData.ui.buttonTexture);
	programData.ui.menuRenderer.Text("", Colors_White);

	glm::vec4 customWidgetTransform = {};
	programData.ui.menuRenderer.CustomWidget(171, &customWidgetTransform);

	glui::Frame f({0,0,renderer.windowW, renderer.windowH});


	auto drawButton = [&](glm::vec4 transform, glm::vec4 color,
		const std::string &s)
	{
		return glui::drawButton(renderer, transform, color, s, programData.ui.font, programData.ui.buttonTexture,
			platform::getRelMousePosition(), platform::isLMouseHeld(), platform::isLMouseReleased());
	};

	if (programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].size()
		&& programData.ui.menuRenderer.internal.allMenuStacks
		[programData.ui.menuRenderer.internal.currentId].back() == "Play"
		)
	{

		//background
		{
			float rezolution = 256;
			glm::vec2 size{renderer.windowW, renderer.windowH};
			size /= 256.f;

			renderer.renderRectangle({0,0, renderer.windowW, renderer.windowH},
				programData.blocksLoader.backgroundTexture, {0.6,0.6,0.6,1}, {}, 0,
				{0,size.y, size.x, 0}
				);
		}

		//folder logic

		if (!std::filesystem::exists(RESOURCES_PATH "worlds"))
		{
			std::filesystem::create_directories(RESOURCES_PATH "worlds");
		}
			
		std::vector<std::filesystem::path> allWorlds;
		for (auto const &d : std::filesystem::directory_iterator{RESOURCES_PATH "worlds"})
		{
			if (d.is_directory())
			{
				allWorlds.push_back(d.path().filename());
			}
		}

		static std::string selected = "";

		//center
		{
			float ySize = renderer.windowH - customWidgetTransform.y * 2;
			
			if (ySize > 50)
			{
				static int advance = 0;

				glui::Frame f(glui::Box().xCenter().yTop(customWidgetTransform.y).
					yDimensionPixels(ySize).xDimensionPercentage(1)());
				auto fullBox = glui::Box().xLeft().yTop().xDimensionPercentage(1.f).yDimensionPercentage(1.f)();

				float buttonH = customWidgetTransform.w;
				int maxElements = fullBox.w / buttonH;

				advance = std::max(advance, 0);
				int overflow = allWorlds.size() - maxElements;
				if (overflow < 0) { overflow = 0; }
				advance = std::min(advance, overflow);

				//background
				{
					float rezolution = 256;
					glm::vec2 size{fullBox.z, fullBox.w};
					size /= 256.f;

					float padding = advance * 0.4;

					renderer.renderRectangle(fullBox,
						programData.blocksLoader.backgroundTexture, {0.4,0.4,0.4,1}, {}, 0,
						{padding,size.y, size.x + padding, 0}
					);
				}

				//renderer.renderRectangle(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f),
				//	{1,0,0,1.0});

				glm::vec4 worldBox = fullBox;
				worldBox.w = buttonH;
				worldBox.z -= buttonH;

				for (int i = 0; i < maxElements; i++)
				{
					if (allWorlds.size() <= i + advance) { break; }

					auto s = allWorlds[i + advance].string();

					if (s == selected)
					{
						renderer.render9Patch(worldBox,
							20, {0.3,0.3,0.3,0.7}, {}, 0, programData.ui.buttonTexture,
							GL2D_DefaultTextureCoords, {0.2,0.8,0.8,0.2});
					}

					if (renderButton(renderer, {}, worldBox, s, &programData.ui.font))
					{
						selected = s;
					}

					worldBox.y += buttonH;
				}

				auto topButton = glui::Box().yTop().xRight().yDimensionPixels(buttonH).xDimensionPixels(buttonH)();
				auto bottomButton = glui::Box().yBottom().xRight().yDimensionPixels(buttonH).xDimensionPixels(buttonH)();

				if (renderButton(renderer, programData.ui.buttonTexture, topButton)) { advance--; }
				if (renderButton(renderer, programData.ui.buttonTexture, bottomButton)) { advance++; }

			}

		}

		//bottom
		{
			float ySize = customWidgetTransform.y;
			glui::Frame f(glui::Box().xCenter().yBottom().
				yDimensionPixels(ySize).xDimensionPercentage(1)());

			//renderer.renderRectangle(glui::Box().xCenter().yCenter().xDimensionPercentage(1).yDimensionPercentage(1.f),
			//	{0,1,0,1.0});


			//top
			{
				glui::Frame f(glui::Box().xCenter().yTop().
					yDimensionPercentage(0.5).xDimensionPercentage(1)());

				{
					auto leftButton = glui::Box().xLeft().yCenter().xDimensionPercentage(0.5).yDimensionPercentage(1)();
					if (drawButton(shrinkPercentage(leftButton, {0.1,0.05}), Colors_Gray, "Play Selected World"))
					{

						hostServer(selected);

					}

					auto rightButton = glui::Box().xRight().yCenter().xDimensionPercentage(0.5).yDimensionPercentage(1)();
					drawButton(shrinkPercentage(rightButton, {0.1,0.05}), Colors_Gray, "Settings");
				}

			}

			//bottom
			{
				glui::Frame f(glui::Box().xCenter().yBottom().
					yDimensionPercentage(0.5).xDimensionPercentage(1)());

				{
					auto leftButton = glui::Box().xLeft().yCenter().xDimensionPercentage(0.5).yDimensionPercentage(1)();
					drawButton(shrinkPercentage(leftButton, {0.1,0.05}), Colors_Gray, "Create world");

					auto rightButton = glui::Box().xRight().yCenter().xDimensionPercentage(0.5).yDimensionPercentage(1)();
					drawButton(shrinkPercentage(rightButton, {0.1,0.05}), Colors_Gray, "Delete world");
				}


			}

		}


	}


}
