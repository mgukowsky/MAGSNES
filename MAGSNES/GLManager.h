#pragma once

#include "Core.h"

namespace MAGSNES {

//Handles all OpenGL backend. Expected to be created in the video thread. 
//Note that ONLY the thread which owns the GLManager instance will be able to draw to the window;
class GLManager {
public:
	GLManager(Core &refCore);
	~GLManager();

	//Create an OpenGL context from our window; GL boilerplate init goes here
	const bool hook_up_gl();

	void draw_pixel(const word x, const word y, dword color);

	//Flip buffers
	void update_screen();

private:
	Core &refCore;
  const LARGE_INTEGER &CPU_FREQ;
	HWND hwnd;
	HDC hdc;
	HGLRC hglrc;

	bool bufferToggle;

	//Initially had a vbufferA and vbufferB for software (read: redundant) double buffering, thinking that we don't want the 
	//exec thread to start drawing into the top of the buffer before it makes it to the screen, which would cause random garbage on the top
	//portion of the screen. This is not a concern however, as A) the rendering operation is fairly quick and B) there are unseen scanlines that need
	//to be drawn/omitted during VBLANK that ensure that during the critical time between when the frame is completed and it is rendered to the screen, the execution
	//thread will not yet be drawing to the buffer until it is presented to the user.
	dword vbufferA[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT];

	/*dword (&_vbufferA)[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT];
	dword (&_vbufferB)[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT];*/

	GLuint textureID;

	void resize_gl();
	void init_texture();
};

} /* namespace MAGSNES */