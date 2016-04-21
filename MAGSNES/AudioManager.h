#pragma once

#include <Windows.h>
#include <Audioclient.h>
#include <mmdeviceapi.h>

#include "Core.h"

namespace MAGSNES {

//Takes care of low level audio (i.e. writing to buffer); basically a shell over WASAPI
class AudioManager {
public:
	AudioManager();
	~AudioManager();

	void begin_audio();
	void main_audio_loop();
	void end_audio();

private:
	Core &sysCore;

	//State variables needed by WASAPI
	WAVEFORMATEX *pwfx;
	IMMDeviceEnumerator *pEnumerator;
	IMMDevice *pDevice;
	IAudioClient *pAudioClient;
	IAudioRenderClient *pRenderClient;
	UINT32 bufferFrameCount;
	BYTE *pData;

	void safe_stop_audio(const char * const msg);

	dword oscTimer;
};

} /* namespace MAGSNES */
