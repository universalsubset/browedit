#include "BrowEdit.h"
#include "Camera.h"
#include "windows/FileOpenWindow.h"
#include "windows/TextureWindow.h"

#include <BroLib/GrfFileSystemHandler.h>
#include <BroLib/Map.h>
#include <BroLib/Gnd.h>

#include <blib/Renderer.h>
#include <blib/SpriteBatch.h>
#include <blib/BackgroundTask.h>

#include <blib/config.h>
#include <blib/wm/WM.h>
#include <blib/wm/Menu.h>
#include <blib/util/FileSystem.h>
#include <blib/Window.h>
#include <blib/Color.h>
#include <blib/Math.h>
#include <blib/Util.h>
#include <blib/ResourceManager.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef BLIB_WIN
#include "resource.h"
#endif


BrowEdit::BrowEdit(const Json::Value &config)
{
	this->config = config;

	appSetup.width = config["resolution"][0u].asInt();
	appSetup.height = config["resolution"][1u].asInt();
	appSetup.vsync = false;
	appSetup.icon = 0;
	appSetup.renderer = blib::AppSetup::GlRenderer;
	appSetup.border = true;
	



	appSetup.title = "BrowEdit 2.0";

#ifdef BLIB_WIN

	appSetup.icon = IDI_ICON1;

	HINSTANCE hInst = GetModuleHandle(0);
	HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(IDR_DATA1), "data");
	HGLOBAL hMem = LoadResource(NULL, hRes);
	DWORD size = SizeofResource(NULL, hRes);
	char* resText = (char*)LockResource(hMem);
	char* text = (char*)malloc(size + 1);
	memcpy(text, resText, size);
	text[size] = 0;
	FreeResource(hMem);

	appSetup.title += " Version ";
	appSetup.title += text;
	free(text);
#endif



	editMode = EditMode::TextureEdit;

	textureTargetSize = glm::ivec2(2, 2);
	textureRot = 0;
	textureFlipH = false;
	textureFlipV = false;


	appSetup.threaded = config["threadedrendering"].asBool();
	appSetup.backgroundTasks = config["backgroundworkers"].asBool();
	appSetup.vsync = config["vsync"].asBool();


	map = NULL;
	wm = blib::wm::WM::getInstance();
}


BrowEdit::~BrowEdit(void)
{
}

void BrowEdit::init()
{
	std::list<blib::BackgroundTask<int>*> tasks;
	for (Json::ArrayIndex i = 0; i < config["data"]["grfs"].size(); i++)
		tasks.push_back(new blib::BackgroundTask<int>(NULL, [this, i]() { blib::util::FileSystem::registerHandler(new GrfFileSystemHandler(config["data"]["grfs"][i].asString())); return 0; }));
	for (blib::BackgroundTask<int>* task : tasks)
		task->waitForTermination();
	//TODO: make sure registerHandle is threadsafe!, make sure the background tasks are cleaned up


	wm->setSkin("assets/skins/ro.json", resourceManager);
	wm->setRadialMenu(rootMenu = wm->loadMenu("assets/menu.json"));
	addMouseListener(wm);
	addKeyListener(wm);


	addMouseListener(this);
	



//	loadMap("data/c_tower1");
	loadMap("data/" + config["defaultmap"].asString());
//	loadMap("data/yuno");

	mapRenderer.init(resourceManager, this);
	mapRenderer.fov = config["fov"].asFloat();
	camera = new Camera();


	highlightRenderState.activeShader = resourceManager->getResource<blib::Shader>("assets/shaders/highlight");
	highlightRenderState.activeShader->bindAttributeLocation("a_position", 0);
	highlightRenderState.activeShader->bindAttributeLocation("a_texcoord", 1);
	highlightRenderState.activeShader->setUniformName(HighlightShaderUniforms::s_texture, "s_texture", blib::Shader::Int);
	highlightRenderState.activeShader->setUniformName(HighlightShaderUniforms::color, "color", blib::Shader::Vec4);
	highlightRenderState.activeShader->setUniformName(HighlightShaderUniforms::texMult, "texMult", blib::Shader::Vec4);
	highlightRenderState.activeShader->setUniformName(HighlightShaderUniforms::modelviewMatrix, "modelviewMatrix", blib::Shader::Mat4);
	highlightRenderState.activeShader->setUniformName(HighlightShaderUniforms::projectionMatrix, "projectionMatrix", blib::Shader::Mat4);
	highlightRenderState.activeShader->finishUniformSetup();
	highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::s_texture, 0);
	highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::texMult, glm::vec4(0, 0, 0, 0));

	highlightRenderState.depthTest = true;
	highlightRenderState.blendEnabled = true;
	highlightRenderState.srcBlendColor = blib::RenderState::SRC_ALPHA;
	highlightRenderState.srcBlendAlpha = blib::RenderState::SRC_ALPHA;
	highlightRenderState.dstBlendColor = blib::RenderState::ONE_MINUS_SRC_ALPHA;
	highlightRenderState.dstBlendAlpha = blib::RenderState::ONE_MINUS_SRC_ALPHA;


	textureWindow = new TextureWindow(resourceManager, this);
	textureWindow->setPosition(window->getWidth() - textureWindow->getWidth(), 10);



	rootMenu->setAction("file/open", [this](){
		new FileOpenWindow(resourceManager, this);
	});



	rootMenu->linkToggle("display/objects", &mapRenderer.drawObjects);
	rootMenu->linkToggle("display/shadows", &mapRenderer.drawShadows);

}

void BrowEdit::update( double elapsedTime )
{
	if(keyState.isPressed(blib::KEY_ESC))
		running = false;

	mapRenderer.cameraMatrix = camera->getMatrix();

	if(mouseState.middleButton)
	{
		if(keyState.isPressed(blib::KEY_SHIFT))
		{
			camera->direction += (mouseState.x - lastMouseState.x) / 2.0f;
			camera->angle = glm::clamp(camera->angle + (mouseState.y - lastMouseState.y) / 2.0f, 0.0f, 90.0f);
		}
		else
			camera->position -= glm::vec2(glm::vec4(mouseState.x - lastMouseState.x, mouseState.y - lastMouseState.y,0,0) * 0.003f * (camera->distance+10) * glm::rotate(glm::mat4(), -camera->direction, glm::vec3(0,0,1)));
	}
	if (map)
	{

		if (editMode == EditMode::TextureEdit)
		{
			if (keyState.isPressed('R') && !lastKeyState.isPressed('R'))
				textureRot = (textureRot + 1) % 4;
			if (keyState.isPressed('H') && !lastKeyState.isPressed('H'))
				textureFlipH = !textureFlipH;
			if (keyState.isPressed('V') && !lastKeyState.isPressed('V'))
				textureFlipV = !textureFlipV;
			if (keyState.isPressed(blib::KEY_PLUS) && !lastKeyState.isPressed(blib::KEY_PLUS))
				textureTargetSize += glm::ivec2(1, 1);
			if (keyState.isPressed(blib::KEY_MINUS) && !lastKeyState.isPressed(blib::KEY_MINUS))
				textureTargetSize = glm::max(glm::ivec2(1, 1), textureTargetSize + glm::ivec2(-1, -1));
			if (keyState.isPressed(blib::KEY_LEFT) && !lastKeyState.isPressed(blib::KEY_LEFT))
				textureTargetSize = glm::max(glm::ivec2(1, 1), textureTargetSize + glm::ivec2(-1, 0));
			if (keyState.isPressed(blib::KEY_RIGHT) && !lastKeyState.isPressed(blib::KEY_RIGHT))
				textureTargetSize += glm::ivec2(1, 0);
			if (keyState.isPressed(blib::KEY_UP) && !lastKeyState.isPressed(blib::KEY_UP))
				textureTargetSize += glm::ivec2(0, 1);
			if (keyState.isPressed(blib::KEY_DOWN) && !lastKeyState.isPressed(blib::KEY_DOWN))
				textureTargetSize = glm::max(glm::ivec2(1, 1), textureTargetSize + glm::ivec2(0, -1));

			int cursorX = (int)glm::floor(mapRenderer.mouse3d.x / 10);
			int cursorY = map->getGnd()->height + 1 - (int)glm::floor(mapRenderer.mouse3d.z / 10);
			int mapHeight = map->getGnd()->height;


			if (mouseState.leftButton && !lastMouseState.leftButton && !wm->inWindow(mouseState.x, mouseState.y) && textureWindow->selectedImage != -1)
			{
				int cursorWidth = textureTargetSize.x;
				int cursorHeight = textureTargetSize.y;

				glm::ivec2 cursorTopLeft = glm::ivec2(cursorX, cursorY) - textureTargetSize / 2;

				glm::vec2 texStart = glm::vec2(textureWindow->tx1.x, textureWindow->tx2.y);
				glm::vec2 texInc = (textureWindow->tx2 - textureWindow->tx1) / glm::vec2(cursorWidth, -cursorHeight);
				glm::vec2 texCenter = (textureWindow->tx1 + textureWindow->tx2) / 2.0f;

				glm::mat4 rot;
				rot = glm::translate(rot, glm::vec3(texCenter, 0));
				rot = glm::rotate(rot, 90.0f * textureRot, glm::vec3(0, 0, 1));
				rot = glm::scale(rot, glm::vec3(textureFlipH ? -1 : 1, textureFlipV ? -1 : 1, 1));
				rot = glm::translate(rot, glm::vec3(-texCenter, 0));

				for (int x = 0; x < cursorWidth; x++)
				{
					for (int y = 0; y < cursorHeight; y++)
					{
						int xx = x + cursorTopLeft.x;
						int yy = y + cursorTopLeft.y;
						if (xx < 0 || yy < 0 || xx >= map->getGnd()->width || yy >= map->getGnd()->height)
							continue;

						glm::vec2 t1 = texStart + glm::vec2(x, y) * texInc;
						glm::vec2 t2 = t1 + texInc;

						Gnd::Cube* cube = map->getGnd()->cubes[xx][yy];
						Gnd::Tile* tile = NULL;
						if (cube->tileUp != -1)
							tile = new Gnd::Tile(*map->getGnd()->tiles[cube->tileUp]);
						else
						{
							tile = new Gnd::Tile();
							tile->lightmapIndex = 0;
						}
						cube->tileUp = map->getGnd()->tiles.size();
						map->getGnd()->tiles.push_back(tile);
						tile->textureIndex = textureWindow->selectedImage;
						tile->v1 = glm::vec2(rot * glm::vec4(t1.x, t1.y, 0, 1));
						tile->v2 = glm::vec2(rot * glm::vec4(t2.x, t1.y, 0, 1));
						tile->v3 = glm::vec2(rot * glm::vec4(t1.x, t2.y, 0, 1));
						tile->v4 = glm::vec2(rot * glm::vec4(t2.x, t2.y, 0, 1));
						mapRenderer.setTileDirty(xx, yy);
					}
				}
			}

		}
	}
	lastKeyState = keyState;
	lastMouseState = mouseState;
}

void BrowEdit::draw()
{
	renderer->clear(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f), blib::Renderer::Color | blib::Renderer::Depth);

	if (map)
	{
		mapRenderer.render(renderer, glm::vec2(mouseState.x, mouseState.y));

		int cursorX = (int)glm::floor(mapRenderer.mouse3d.x / 10);
		int cursorY = map->getGnd()->height - (int)glm::floor(mapRenderer.mouse3d.z / 10);
		int mapHeight = map->getGnd()->height;

		highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::modelviewMatrix, camera->getMatrix());
		highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::projectionMatrix, mapRenderer.projectionMatrix);

		if (editMode == EditMode::TextureEdit && mapRenderer.mouse3d.w < 1 && map && cursorX >= 0 && cursorX < map->getGnd()->width && cursorY >= 0 && cursorY < mapHeight && textureWindow->selectedImage != -1)
		{
			highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::color, glm::vec4(0, 0, 0, 0));
			highlightRenderState.activeShader->setUniform(HighlightShaderUniforms::texMult, glm::vec4(1, 1, 1, 0.75f));
			highlightRenderState.activeTexture[0] = map->getGnd()->textures[textureWindow->selectedImage]->texture;
			std::vector<blib::VertexP3T2> verts;

			int cursorWidth = textureTargetSize.x;
			int cursorHeight = textureTargetSize.y;

			glm::ivec2 cursorTopLeft = glm::ivec2(cursorX, cursorY) - textureTargetSize / 2;

			glm::vec2 texStart = glm::vec2(textureWindow->tx1.x, textureWindow->tx2.y);
			glm::vec2 texInc = (textureWindow->tx2 - textureWindow->tx1) / glm::vec2(cursorWidth, -cursorHeight);
			glm::vec2 texCenter = (textureWindow->tx1 + textureWindow->tx2) / 2.0f;

			glm::mat4 rot;
			rot = glm::translate(rot, glm::vec3(texCenter, 0));
			rot = glm::rotate(rot, 90.0f * textureRot, glm::vec3(0, 0, 1));
			rot = glm::scale(rot, glm::vec3(textureFlipH ? -1 : 1, textureFlipV ? -1 : 1, 1));
			rot = glm::translate(rot, glm::vec3(-texCenter, 0));

			for (int x = 0; x < cursorWidth; x++)
			{
				for (int y = 0; y < cursorHeight; y++)
				{
					int xx = x + cursorTopLeft.x;
					int yy = y + cursorTopLeft.y;

					if (xx < 0 || yy < 0 || xx >= map->getGnd()->width || yy >= map->getGnd()->height)
						continue;

					glm::vec2 t1 = texStart + glm::vec2(x, y) * texInc;
					glm::vec2 t2 = t1 + texInc;

					Gnd::Cube* cube = map->getGnd()->cubes[xx][yy];

					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx,			-cube->h3 + 0.1f, 10 * (mapHeight - yy)),		glm::vec2(rot * glm::vec4(t1.x,t1.y,0,1))));
					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx,			-cube->h1 + 0.1f, 10 * (mapHeight - yy)-10),	glm::vec2(rot * glm::vec4(t1.x,t2.y,0,1))));
					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx + 10,	-cube->h4 + 0.1f, 10 * (mapHeight - yy)),		glm::vec2(rot * glm::vec4(t2.x,t1.y,0,1))));

					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx + 10,	-cube->h4 + 0.1f, 10 * (mapHeight - yy)),		glm::vec2(rot * glm::vec4(t2.x,t1.y,0,1))));
					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx + 10,	-cube->h2 + 0.1f, 10 * (mapHeight - yy)-10),	glm::vec2(rot * glm::vec4(t2.x,t2.y,0,1))));
					verts.push_back(blib::VertexP3T2(glm::vec3(10 * xx,			-cube->h1 + 0.1f, 10 * (mapHeight - yy)-10),	glm::vec2(rot * glm::vec4(t1.x,t2.y,0,1))));
				}
			}


			renderer->drawTriangles(verts, highlightRenderState);




		}



		spriteBatch->begin();


		if (mapRenderer.mouse3d.w < 1 && map)
		{
			char statusText[256];
			sprintf(statusText, "Mouse: %3i, %3i,   -> %3.2f,%3.2f,%3.2f", cursorX, cursorY, mapRenderer.mouse3d.x, mapRenderer.mouse3d.y, mapRenderer.mouse3d.z);

			spriteBatch->draw(wm->font, statusText, blib::math::easyMatrix(glm::vec2(5, window->getHeight() - 18)), blib::Color::black);
			spriteBatch->draw(wm->font, statusText, blib::math::easyMatrix(glm::vec2(3, window->getHeight() - 20)), blib::Color::black);
			spriteBatch->draw(wm->font, statusText, blib::math::easyMatrix(glm::vec2(4, window->getHeight() - 19)), blib::Color::white);
		}
		std::string editModeString = "";
		if (editMode == EditMode::TextureEdit)
			editModeString = "Texture Edit";
		else if (editMode == EditMode::ObjectEdit)
			editModeString = "Object Edit";
		else if (editMode == EditMode::GatEdit)
			editModeString = "GAT Edit";
		else
			editModeString = "Unknown editmode: " + blib::util::toString((int)editMode);

		spriteBatch->draw(wm->font, editModeString, blib::math::easyMatrix(glm::vec2(301, window->getHeight() - 18)), blib::Color::black);
		spriteBatch->draw(wm->font, editModeString, blib::math::easyMatrix(glm::vec2(299, window->getHeight() - 20)), blib::Color::black);
		spriteBatch->draw(wm->font, editModeString, blib::math::easyMatrix(glm::vec2(300, window->getHeight() - 19)), blib::Color::white);
	}
	else
		spriteBatch->begin();

	wm->draw(*spriteBatch);
	spriteBatch->end();

}

void BrowEdit::loadMap(std::string fileName)
{
	if(map)
		delete map;
	map = NULL;

	runBackground<Map*>(	[fileName] () { return new Map(fileName); }, 
							[this] (Map* param) { map = param;
										camera->position = glm::vec2(map->getGnd()->width*5, map->getGnd()->height*5);
										mapRenderer.setMap(map);
										textureWindow->updateTextures(map); //TODO: textures aren't loaded here yet!
	} );
}

bool BrowEdit::onScroll( int delta )
{
	camera->distance -= delta/10.0f;
	if (camera->distance < 0)
		camera->distance = 0;
	return true;
}
