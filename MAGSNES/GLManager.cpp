#include "GLManager.h"

__FILESCOPE__{
	const MAGSNES::dword SCREEN_DWORD_SIZE = MAGSNES::NES_SCREEN_WIDTH * MAGSNES::NES_SCREEN_HEIGHT;
}

using namespace MAGSNES;

GLManager::GLManager(Core &refCore)
	: refCore(refCore), CPU_FREQ(this->refCore.get_cpu_freq()), hwnd(this->refCore.get_main_window()),
		hdc(NULL), hglrc(NULL), bufferToggle(true) {
	
}

GLManager::~GLManager() {
	glDeleteTextures(1, &textureID);
	wglMakeCurrent(NULL, NULL);

	if (hglrc != NULL) {
		wglDeleteContext(hglrc);
	}

	if (hdc != NULL) {
		ReleaseDC(hwnd, hdc);
	}
}

const bool GLManager::hook_up_gl() {
	PIXELFORMATDESCRIPTOR pfd;

	hdc = GetDC(hwnd);

	//PIXELFORMATDESCRIPTOR has many fields, but many are unused and remain 0
	std::memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));

	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 0;
	pfd.iLayerType = PFD_MAIN_PLANE;

	//System chooses most practical pixel format given the pfd
	int chosenPixelFormat = ChoosePixelFormat(hdc, &pfd);
	if (chosenPixelFormat == NULL) {
		refCore.alert_error("Unable to create pixel format. This is likely a problem with the application itself. Shutting down.");
		return false;
	}

	char pixelMsg[32];
	sprintf_s(pixelMsg, "Pixel format %d was selected", chosenPixelFormat);
	refCore.logmsg(pixelMsg);

	int pixelResult = SetPixelFormat(hdc, chosenPixelFormat, &pfd);
	if (pixelResult == NULL) {
		refCore.alert_error("Unable to set pixel format. This is likely a problem with the application itself. Shutting down.");
		return false;
	}

	//Create the context from the window
	hglrc = wglCreateContext(hdc);

	//Connect the current thread so that it renders to our context
	wglMakeCurrent(hdc, hglrc);

	resize_gl();

	//With retro graphics, modern video quality is of little importance
	glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_GENERATE_MIPMAP_HINT, GL_FASTEST);

	init_texture();

#ifdef _DEBUG

	//Log GL env info

	const GLubyte *tmp;
	char *s;
	int l;

	refCore.logmsg("*******************GL_VENDOR********************\n\n");
	tmp = glGetString(GL_VENDOR);
	refCore.logmsg((char *)tmp);
	refCore.logmsg("\n\n****************************************************\n\n");

	refCore.logmsg("*******************GL_RENDERER********************\n\n");
	tmp = glGetString(GL_RENDERER);
	refCore.logmsg((char *)tmp);
	refCore.logmsg("\n\n****************************************************\n\n");

	refCore.logmsg("*******************GL_VERSION********************\n\n");
	tmp = glGetString(GL_VERSION);
	refCore.logmsg((char *)tmp);
	refCore.logmsg("\n\n****************************************************\n\n");

	refCore.logmsg("*******************GL_EXTENSIONS********************\n\n");
	tmp = glGetString(GL_EXTENSIONS);
	s = (char *)tmp;
	l = strlen(s);
	for (int i = 0; i < l; i++) {
		if (*(s + i) == ' ') {
			*(s + i) = '\n';
		}
	}
	refCore.logmsg(s);
	refCore.logmsg("\n\n****************************************************\n\n");

#endif

	return true;
}

void GLManager::update_screen() {
	
	//Don't bother clearing screen, since we're always rendering to the whole screen

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 240, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, vbufferA);

	//TODO: try more efficient OpenGL methods (mipmaps et al), although the app seems to be running OK...

	glBegin(GL_QUADS);
		//Note we start at the top right to match the coord system used in the software NES implementation (GL usually starts at bottom left); 
		//the picture would be flipped otherwise.
		glTexCoord2d(0.0f, 0.0f); glVertex2d(-1.0f, +1.0f);
		glTexCoord2d(1.0f, 0.0f); glVertex2d(+1.0f, +1.0f);
		glTexCoord2d(1.0f, 1.0f); glVertex2d(+1.0f, -1.0f);
		glTexCoord2d(0.0f, 1.0f); glVertex2d(-1.0f, -1.0f);
	glEnd();

	SwapBuffers(hdc); //HDC takes care of double buffering magic
}

void GLManager::draw_pixel(const word x, const word y, dword color) {

#ifdef X86_BUILD //MSVC only supports inline asm on x86 arch
	//Assembly to quickly reverse endianess and OR in opacity
	__asm {
		push eax
		mov eax, [color]
		rol ax, 8
		rol eax, 16
		rol ax, 8
		or eax, 0FF000000h //OR away PPU meta information in the color value
		mov [color], eax
		pop eax
	}
#else
	color = _byteswap_ulong(color | 0xFF);
#endif
	vbufferA[x + (y * NES_SCREEN_WIDTH)] = color;

}

void GLManager::resize_gl() {

	//// Make the viewport cover the entire window
	//TODO: this call throws off the viewport from what is expected; figure out why...
	//glViewport(0, 0, refCore.contextWidth, refCore.contextHeight);

	//Ensure nothing is clipped from our window
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(-1.0, 1.0, -1.0, 1.0);

}

void GLManager::init_texture() {
	glEnable(GL_TEXTURE_2D);

	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		//GL_NEAREST_MIPMAP_NEAREST);
		GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		//GL_NEAREST_MIPMAP_NEAREST);
		GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
}