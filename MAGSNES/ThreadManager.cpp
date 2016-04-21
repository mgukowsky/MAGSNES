#include "ThreadManager.h"

using namespace MAGSNES;

ThreadManager::ThreadManager(DWORD mainThreadID)
	: mainThreadID(mainThreadID), execThread(nullptr), videoThread(nullptr), audioThread(nullptr) {}

ThreadManager::~ThreadManager() {
	
	//Just in case any threads are still running, take this chance to kill them
	kill_threads();

}

const bool ThreadManager::start_thread(const int id, ThreadProc threadProc, HANDLE arg) {
	switch (id) {
	case THREAD_ID_EXEC:
		if (execThread == nullptr) {
			execThread = new std::thread(threadProc, arg);
		} else {
			return false;
		}
		break;
	case THREAD_ID_VIDEO:
		if (videoThread == nullptr) {
			videoThread = new std::thread(threadProc, arg);
		} else {
			return false;
		}
		break;
	case THREAD_ID_AUDIO:
		if (audioThread == nullptr) {
			audioThread = new std::thread(threadProc, arg);
		} else {
			return false;
		}
		break;
	default:
		return false;
	}

	return true;
}

void ThreadManager::kill_threads() {
	if (execThread != nullptr) {
		if (execThread->joinable()) {
			execThread->join();
		}
		delete execThread;
		execThread = nullptr;
	}

	if (videoThread != nullptr) {
		if (videoThread->joinable()) {
			videoThread->join();
		}
		delete videoThread;
		videoThread = nullptr;
	}

	if (audioThread != nullptr) {
		if (audioThread->joinable()) {
			audioThread->join();
		}
		delete audioThread;
		audioThread = nullptr;
	}

}