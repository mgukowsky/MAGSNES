#include "AudioManager.h"

__FILESCOPE__{
	const MAGSNES::qword REFTIMES_PER_SEC = 1000000; //100ns units

	//8 step sequences for the square waves
	const float squareSequenceDutyCycleHalf[8] =		{ -0.1, 0.1, 0.1, 0.1, 0.1, -0.1, -0.1, -0.1 },
							squareSequenceDutyCycleQuarter[8] = { -0.1, 0.1, 0.1, -0.1, -0.1, -0.1, -0.1, -0.1 },
							squareSequenceDutyCycleEighth[8] =	{ -0.1, 0.1, -0.1, -0.1, -0.1, -0.1, -0.1, -0.1 };

	const float PI = 3.14159265;

	/*float triangleSequence[32] = {
		0.1,  0.0875,  , 0.08125, 0.075, 0.06875, 0.0625, 0.05625,
		7 6 5 4 3 2 1 0 0 1 2 3 4 5 6 7 8 9 A B C D E F
	}*/
	float triangleSequence[32] = {
		0.1, 0.0875, 0.075, 0.0625,
		0.05, 0.0375, 0.025, 0.0125,
		0.0, -0.0125, -0.025, -0.0375,
		-0.05, -0.0625, -0.075, -0.0875,
		-1.0, -0.0875, -0.075, -0.0625, 
		-0.05, -0.0375, -0.025, -0.0125,
		0.0, 0.0125, 0.025, 0.0375, 
		0.05, 0.0625, 0.075, 0.0875
	};

	const float filterCoefficientLP = 0.815686f, filterCoefficientHP90 = 0.996039f, filterCoefficientHP14 = 0.999835f;
}

using namespace MAGSNES;

AudioManager::AudioManager()
	: sysCore(Core::get_sys_core()), pwfx(NULL), pEnumerator(NULL), pDevice(NULL),
		pAudioClient(NULL), pRenderClient(NULL), oscTimer(0) {}

AudioManager::~AudioManager() {

}

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

void AudioManager::begin_audio() {
	//All of this init code is adapted from MSDN: https://msdn.microsoft.com/en-us/library/windows/desktop/dd316756(v=vs.85).aspx

	//We need a lot of vars...
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	UINT32 numFramesPadding;

	//First step in audio init, starts up the IMMDeviceEnumerator, which we will need for interfacing with hardware
	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);

	if (FAILED(hr)) {
		safe_stop_audio("Call to CoCreateInstance failed!");
		return;
	}

	//Figure out where we will be sending audio
	hr = pEnumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &pDevice);

	if (FAILED(hr)) {
		safe_stop_audio("Unable to get audio endpoint!");
		return;
	}

	//Create an IAudioClient interface to talk to the hardware
	hr = pDevice->Activate(
		IID_IAudioClient, CLSCTX_ALL,
		NULL, (void**)&pAudioClient);

	if (FAILED(hr)) {
		safe_stop_audio("Unable to activate audio device!");
		return;
	}

	//How we will be formatting our audio so the hardware understands it
	hr = pAudioClient->GetMixFormat(&pwfx);

	if (FAILED(hr)) {
		safe_stop_audio("Unable to get mix format!");
		return;
	}

	sysCore.audioRegs.SAMPLE_FREQUENCY = pwfx->nSamplesPerSec;

	//Initialize the audio stream
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		0,
		REFTIMES_PER_SEC, //Audio device reserves the right to adjust this value up to the minimum buffer size required by the driver
		0,
		pwfx,
		NULL);
	
	if (FAILED(hr)) {
		safe_stop_audio("Unable to initialize audio stream!");
		return;
	}

	// MSDN says we can change the format here?
	/*hr = pMySource->SetFormat(pwfx);
	EXIT_ON_ERROR(hr)*/

	// Get the actual size of the allocated buffer.
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	
	if (FAILED(hr)) {
		safe_stop_audio("Unable toget size of allocated buffer!");
		return;
	}

	// Set up our render client
	hr = pAudioClient->GetService(
		IID_IAudioRenderClient,
		(void**)&pRenderClient);

	if (FAILED(hr)) {
		safe_stop_audio("Unable initialize render client!");
		return;
	}

	/* Output a silence buffer to avoid a 'pop' when the audio starts */

	// Grab the entire buffer for the initial fill operation.
	hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
	
	if (FAILED(hr)) {
		safe_stop_audio("Unable to get buffer from render client!");
		return;
	}

	//The last flag tells the hardware to output silence for the duration of the buffer regardless of its contents
	hr = pRenderClient->ReleaseBuffer(bufferFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
	
	if (FAILED(hr)) {
		safe_stop_audio("Render client failed to release buffer!");
		return;
	}

	// Calculate the actual duration of the allocated buffer.
	hnsActualDuration = (double)REFTIMES_PER_SEC *
		bufferFrameCount / pwfx->nSamplesPerSec;

	//Let the madness begin!
	hr = pAudioClient->Start();
	
	if (FAILED(hr)) {
		safe_stop_audio("Audio client was unable to start playing audio!");
		return;
	}

}

void AudioManager::main_audio_loop() {

	HRESULT hr;
	UINT32 numFramesAvailable;
	UINT32 numFramesPadding;
	float *pf(nullptr), square0Result(0), square1Result(0), triangleResult(0), lastTriangleResult(0), beforeLastTriangleResult(0), tmp, inI, tmpI;
	bool waitForTriangleAlignment(false);

	const UINT8 PITCH_CHANGE_RESOLUTION = 1000 / 60;

	while (sysCore.shouldRun) {
		Sleep(PITCH_CHANGE_RESOLUTION); //5ms is arbitrary; different values may have different effects on the quality of the synthesizer, but not sleeping GREATLY increases the CPU drain
		// See how much buffer space is available.
		hr = pAudioClient->GetCurrentPadding(&numFramesPadding);

		if (FAILED(hr)) {
			safe_stop_audio("Audio client was unable to get the padding of the buffer!");
			return;
		}

		numFramesAvailable = bufferFrameCount - numFramesPadding;

		// Grab all the available space in the shared buffer.
		hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);

		if (FAILED(hr)) {
			safe_stop_audio("Audio client was unable to get the audio buffer!");
			return;
		}

		// Get next 1/2-second of data from the audio source.
		/*hr = pMySource->LoadData(numFramesAvailable, pData, &flags);
		EXIT_ON_ERROR(hr)*/
		dword limit = numFramesAvailable * 2;
		pf = (float *)pData;

		float thresholdSquare0, thresholdSquare1;

		switch (sysCore.audioRegs.square0DutyCycle) {
		case DutyCycle::DUTY_CYCLE_HALF:
			thresholdSquare0 = sysCore.audioRegs.square0Period / 2.0;
			break;
		case DutyCycle::DUTY_CYCLE_QUARTER:
			thresholdSquare0 = sysCore.audioRegs.square0Period / 4.0;
			break;
		case DutyCycle::DUTY_CYCLE_EIGHTH:
			thresholdSquare0 = sysCore.audioRegs.square0Period / 8.0;
			break;
		}

		switch (sysCore.audioRegs.square1DutyCycle) {
		case DutyCycle::DUTY_CYCLE_HALF:
			thresholdSquare1 = sysCore.audioRegs.square1Period / 2.0;
			break;
		case DutyCycle::DUTY_CYCLE_QUARTER:
			thresholdSquare1 = sysCore.audioRegs.square1Period / 4.0;
			break;
		case DutyCycle::DUTY_CYCLE_EIGHTH:
			thresholdSquare1 = sysCore.audioRegs.square1Period / 8.0;
			break;
		}

		for (dword i = 0; i < limit; i += 2) {
			//This was a HUGE pain, as there is no direct way to tell (that I can find) if the buffer wants values
			//in the range -1.0 and +1.0: from an amazing MSDN user at https://msdn.microsoft.com/en-us/library/windows/desktop/dd316756(v=vs.85).aspx
			/*
				"It's possible that the format your device requests is actually FLOAT32 (values between -1 and 1).
				I found my devicewanting this, and if I didn't send it those values, I ended up with silence."

				-user 'Russ Schultz'
			*/

			/*

						////////////////////////////////
						///Wave Equation Explanations///
						////////////////////////////////

						All equations are adapted from: http://stackoverflow.com/questions/1073606/is-there-a-one-line-function-that-generates-a-triangle-wave

						'*period' refers to the period of a given channel, derived from the sample frequency divided by the frequency of the tone.

						All equations assume 2 channel audio, which is why the sample is written to the specific location in the buffer (*(pf+i)) and the index one above it.

						SINE:
										*(pf + i + 1) = *(pf + i) = std::sin(oscTimer / ((sysCore.audioRegs.square1Period/PI)/2.0f)) * amplitudeFactor;

						SAW:
										*(pf + i + 1) = *(pf + i) = (std::fmod(oscTimer, sysCore.audioRegs.square1Period) - sysCore.audioRegs.square1Period) * amplitudeFactor;

						SQUARE:
										*(pf + i + 1) = *(pf + i) = (std::fmod(oscTimer, sysCore.audioRegs.square1Period) < threshold)
																								? sysCore.audioRegs.negativeAmp
																								: sysCore.audioRegs.positiveAmp;

						TRIANGLE:
										(I'm not thrilled with the timbre this one generates, but it's close enough)
										*(pf + i + 1) = *(pf + i) = std::fabs((std::fmod(2*oscTimer, 2*sysCore.audioRegs.square1Period) - sysCore.audioRegs.square1Period)) * amplitudeFactor;

			*/

			//Synthesize a sample for each channel

			if (sysCore.audioRegs.square0ImplicitOff) {
				square0Result = 0;
			} else {
				square0Result = (std::fmod((float)oscTimer, sysCore.audioRegs.square0Period) < thresholdSquare0)
					? sysCore.audioRegs.square0negativeAmp
					: sysCore.audioRegs.square0positiveAmp;
			}

			if (sysCore.audioRegs.square1ImplicitOff) {
				square1Result = 0;
			} else {
				square1Result = (std::fmod((float)oscTimer, sysCore.audioRegs.square1Period) < thresholdSquare1)
					? sysCore.audioRegs.square1negativeAmp
					: sysCore.audioRegs.square1positiveAmp;
			}

			if (sysCore.audioRegs.triangleImplicitOff) {
				/*waitForTriangleAlignment = true;
				if (lastTriangleResult == 0.9f) {
					triangleResult = 0.9f;
				} else {
					triangleResult = (std::fmod(oscTimer, sysCore.audioRegs.trianglePeriod) < sysCore.audioRegs.trianglePeriod/2.0f)
						? 9.0f
						: 0.0f;

				}*/
				triangleResult = triangleResult;

			} else {
				/*if (waitForTriangleAlignment) {
					oscTimer = 0;
					waitForTriangleAlignment = false;
				}*/

				triangleResult = (std::fmod(oscTimer, sysCore.audioRegs.trianglePeriod) < sysCore.audioRegs.trianglePeriod / 2.0f)
					? -0.05f
					: 0.05f;

			}

			/***********MIXER*************/
			////Left channel
			//*(pf + i) = square0Result / 2.0f;

			////Right channel
			//*(pf + i + 1) = (square1Result + triangleResult) / 2.0f;
			*(pf + i + 1) = *(pf + i) = (square0Result + square1Result + triangleResult) / 3.0f;

			oscTimer++;

			beforeLastTriangleResult = lastTriangleResult;
			lastTriangleResult = triangleResult;
		}

		/*************FILTER**************/
		/*for (dword i = 2; i < limit; i++) {
			*(pf + i) = (*(pf + i) - *(pf + i - 2)) * filterCoefficientLP;
		}*/
		//High pass, low pass, then high pass for each channel
		/*if (limit > 2) {
			inI = *(pf);*/
			//L channel
			/*for (dword i = 2; i < limit; i += 2) {
				tmpI = *(pf + i);
				*(pf + i) = (*(pf + i - 2) * filterCoefficientHP90) + *(pf + i) - inI;
				inI = tmpI;
			}*/

			/*for (dword i = 2; i < limit; i += 2) {
				*(pf + i) = (*(pf + i) - *(pf + i - 2)) * filterCoefficientLP;
			}*/

			/*for (dword i = 2; i < limit; i += 2) {
				tmpI = *(pf + i);
				*(pf + i) = (*(pf + i - 2) * filterCoefficientHP14) + *(pf + i) - inI;
				inI = tmpI;
			}*/


			//R channel
			//inI = *(pf + 1);
			/*for (dword i = 3; i < limit; i += 2) {
				tmpI = *(pf + i);
				*(pf + i) = (*(pf + i - 2) * filterCoefficientHP90) + *(pf + i) - inI;
				inI = tmpI;
			}*/

			/*for (dword i = 3; i < limit; i += 2) {
				*(pf + i) = (*(pf + i) - *(pf + i - 2)) * filterCoefficientLP;
			}*/

			/*for (dword i = 3; i < limit; i += 2) {
				tmpI = *(pf + i);
				*(pf + i) = (*(pf + i - 2) * filterCoefficientHP14) + *(pf + i) - inI;
				inI = tmpI;
			}*/
		//}


		/*********************************/

		hr = pRenderClient->ReleaseBuffer(numFramesAvailable, 0);

		if (FAILED(hr)) {
			safe_stop_audio("Audio client was unable to release the audio buffer!");
			return;
		}
	}
}

void AudioManager::end_audio() {
	HRESULT hr;

	hr = pAudioClient->Stop();

	if (FAILED(hr)) {
		safe_stop_audio("Audio client was unable to stop playing audio!");
		return;
	}

	safe_stop_audio(nullptr);
}

void AudioManager::safe_stop_audio(const char * const msg) {
	if (msg != nullptr) {
		sysCore.alert_error(msg);
	}

	CoTaskMemFree(pwfx);
	if (pEnumerator != NULL) { pEnumerator->Release(); pEnumerator = NULL; }
	if (pDevice != NULL) { pDevice->Release(); pDevice = NULL; }
	if (pAudioClient != NULL) { pAudioClient->Release(); pAudioClient = NULL; }
	if (pRenderClient != NULL) { pRenderClient->Release(); pRenderClient = NULL; }
}