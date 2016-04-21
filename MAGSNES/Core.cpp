#include "Core.h"

#include <ShObjIdl.h> //For file dialog

//Many of these are cached variables that are used in WndProc
__FILESCOPE__{
	const wchar_t * const APP_NAME = L"MAGSNES";
	const wchar_t * const WINDOW_TITLE = L"MAGSNES - SNES Emulator by Matt Gukowsky";
	const wchar_t * const APP_ABOUT = L"Thanks for using MAGSNES \xA9 2016, an SNES emulator by Matt Gukowsky."\
																		" Check it out on GitHub at www.github.com/mgukowsky/magsnes";

	MAGSNES::Core &sysCore = MAGSNES::Core::get_sys_core(); //The static sysCore inside the function will have been initialized before this is called
	const DWORD desiredWindowStyles = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	const int	windowHOffset = 400, windowVOffset = 200;

	bool fileSelectionResult;

	HMENU hmenuCached = nullptr; //Init to nullptr, but it is only used in WndProc, so we don't need to manage it once it is assigned in init_window
}

using namespace MAGSNES;

Core::Core()
	: hwnd(NULL), shouldRun(true), shouldHalt(false), shouldDrawFrame(false), shouldEmulate(false), isExecRunning(false),
		threadManager(nullptr), framesDrawn(0),
		bootProc(nullptr) { 

	std::memset(&audioRegs, 0, sizeof(AudioRegs));
	//Silence squares until the CPU writes a valid period to them
	audioRegs.square0ImplicitOff = true;
	audioRegs.square1ImplicitOff = true;
	audioRegs.triangleImplicitOff = true;
	audioRegs.baseAmp = 0.01; //Adjust for mixing purposes

	threadManager = new ThreadManager(GetCurrentThreadId());
	QueryPerformanceFrequency(&CPU_FREQ);
	QueryPerformanceCounter(&PROGRAM_START);
	for (int i = 0; i < 256; i++) {
		activeKeys[i] = false;
	}
}

Core::~Core() {
	delete threadManager;

	//Windows cleanup
	if (hwnd != NULL) {
		DestroyWindow(hwnd);
	}
}

Core & Core::get_sys_core() {
	static Core sysCore;
	return sysCore;
}

LRESULT CALLBACK Core::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_KEYDOWN:
		sysCore.activeKeys[wParam] = true;
		break;
	case WM_KEYUP:
		if (sysCore.activeKeys[VK_CONTROL]) {
			sysCore.execute_keyboard_shortcut(wParam);
		}
		sysCore.activeKeys[wParam] = false;
		break;
	case WM_COMMAND:
		switch (wParam) {
		case IDM_MENU_OPEN:
			fileSelectionResult = sysCore.select_file();
			if (fileSelectionResult && (sysCore.bootProc != nullptr)) {
				sysCore.bootProc();
			}
			EnableMenuItem(hmenuCached, IDM_MENU_OPEN, MF_DISABLED);
			EnableMenuItem(hmenuCached, IDM_MENU_CLOSE, MF_ENABLED);
			break;
		case IDM_MENU_CLOSE:
			sysCore.shouldEmulate = false;
			EnableMenuItem(hmenuCached, IDM_MENU_OPEN, MF_ENABLED);
			EnableMenuItem(hmenuCached, IDM_MENU_CLOSE, MF_DISABLED);
			break;
		case IDM_MENU_EXIT:
			PostQuitMessage(0);
			sysCore.shouldEmulate = false;
			sysCore.shouldRun = false;
			break;
		case IDM_MENU_EMULATION_PAUSE:
			sysCore.shouldHalt = true;
			break;
		case IDM_MENU_EMULATION_RESUME:
			sysCore.shouldHalt = false;
			break;
		case IDM_MENU_ABOUT:
			sysCore.shouldHalt = true;
			MessageBoxW(NULL, APP_ABOUT, L"MAGSNES - About", MB_ICONINFORMATION | MB_OK | MB_TASKMODAL);
			sysCore.shouldHalt = false;
			break;
		}
		break;
	case WM_CLOSE:
		PostQuitMessage(0);
		sysCore.shouldEmulate = false;
		sysCore.shouldRun = false;
		break;
	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	return 0;
}

//Note that much of the GL code is adapted from: https://bobobobo.wordpress.com/2008/02/11/opengl-in-a-proper-windows-app-no-glut/

const bool Core::init_window(HINSTANCE hInstance, int nCmdShow, Callback pBootProc) {
	bootProc = pBootProc;

	//Standard Win32 boilerplate

	WNDCLASSEXW wc;

	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_OWNDC; //Magic for GL; basically seems to prevent Windows from prematurely releasing the hdc stored in the sysCore
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAGSNES));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszMenuName = (LPCWSTR)MAKEINTRESOURCE(IDM_MAINMENU);
	wc.lpszClassName = APP_NAME;
	wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAGSNES));

	if (!RegisterClassExW(&wc)) {
		alert_error("Unable to register window class! This is likely a problem with the application itself. Shutting down.");
		return false;
	}

	//GL needs to know EXACTLY where to draw on the screen (?)
	glRect.left = 0;
	glRect.top = 0;
	glRect.right = (NES_SCREEN_WIDTH * DEFAULT_SCREEN_SIZE_FACTOR);
	glRect.bottom = (NES_SCREEN_HEIGHT * DEFAULT_SCREEN_SIZE_FACTOR);

	//Fit our drawable area to the window (i.e. account for the presence of a menu)
	AdjustWindowRect(&glRect, desiredWindowStyles, true);

	contextWidth = glRect.right - glRect.left;
	contextHeight = glRect.bottom - glRect.top;

	//Note we use the glRect coords to determine the position AND size of the window
	hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, APP_NAME, WINDOW_TITLE, desiredWindowStyles,
		CW_USEDEFAULT, CW_USEDEFAULT,
		glRect.right, glRect.bottom, 
		NULL, NULL, hInstance, NULL);

	if (hwnd == NULL) {
		alert_error("Unable to create window! This is likely a problem with the application itself. Shutting down.");
		return false;
	}

	ShowWindow(hwnd, nCmdShow);

	hmenuCached = GetMenu(hwnd);

	return true;
}

void Core::alert_message(const char * const msg) {
	shouldHalt = true;
	MessageBox(NULL, msg, "Message", MB_ICONASTERISK | MB_OK | MB_TASKMODAL);
	logmsg(msg);
	shouldHalt = false;
}

void Core::alert_message(const wchar_t * const msg) {
	shouldHalt = true;
	MessageBoxW(NULL, msg, L"Message", MB_ICONASTERISK | MB_OK | MB_TASKMODAL);
	logmsg(msg);
	shouldHalt = false;
}

void Core::alert_error(const char * const msg) {
	shouldHalt = true;
	MessageBox(NULL, msg, "Error!", MB_ICONERROR | MB_OK | MB_TASKMODAL);
	logerr(msg);
	shouldHalt = false;
}

void Core::alert_error(const wchar_t * const msg) {
	shouldHalt = true;
	MessageBoxW(NULL, msg, L"Error!", MB_ICONERROR | MB_OK | MB_TASKMODAL);
	logerr(msg);
	shouldHalt = false;
}

const bool Core::select_file() {
	//Open file dialog, straight from https://msdn.microsoft.com/en-us/library/windows/desktop/ff485843(v=vs.85).aspx
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	PWSTR pszFilePath = nullptr;

	if (SUCCEEDED(hr)) {
		IFileOpenDialog *pFileOpen;

		// Create the FileOpenDialog object.
		hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

		if (SUCCEEDED(hr)) {
			//Boilerplate for only showing *.nes files in the dialog. See the MSDN docs more info.
			COMDLG_FILTERSPEC fileFilter;
			fileFilter.pszName = L"iNES";
			fileFilter.pszSpec = L"*.nes";

			pFileOpen->SetFileTypes(1, &fileFilter);
			pFileOpen->SetFileTypeIndex(1);
			pFileOpen->SetDefaultExtension(L"nes");

			// Show the Open dialog box.
			hr = pFileOpen->Show(NULL);

			// Get the file name from the dialog box.
			if (SUCCEEDED(hr)) {
				IShellItem *pItem;
				hr = pFileOpen->GetResult(&pItem);

				if (SUCCEEDED(hr)) {
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

					if (SUCCEEDED(hr)) {
						logmsg("Opening file");
					} else {
						pszFilePath = nullptr;
					}
					pItem->Release();
				}
			}
			pFileOpen->Release();
		}
		CoUninitialize();
	}

	if (pszFilePath == nullptr) {
		alert_error("Unable to open file! File must have the extension \".nes\"");
		return false;
	}

	//Convert wchar_t string to char string
	std::size_t i;
	wcstombs_s(&i, fileSelection, pszFilePath, MAX_PATH);

	return true;
}

void Core::execute_keyboard_shortcut(const WPARAM wParam) {
	switch (wParam) {
	case 'P':
		PostMessage(hwnd, WM_COMMAND, IDM_MENU_EMULATION_PAUSE, NULL);
		break;
	case 'R':
		PostMessage(hwnd, WM_COMMAND, IDM_MENU_EMULATION_RESUME, NULL);
		break;
	}
}