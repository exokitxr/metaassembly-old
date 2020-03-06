#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"

#include <tools/logging.h>

#include "av_cef_app.h"
#include "avserver.h"

#include <chrono>
#include <thread>
#include <tools/systools.h>
#include <tools/pathtools.h>
#include <tools/stringtools.h>

// OS specific macros for the example main entry points
// int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
int main()
{
  std::cout << "Start" << std::endl;
  
	tools::initLogs();

	std::unique_ptr<CAardvarkCefApp> app( new CAardvarkCefApp( ) );

	while ( !app->wantsToQuit() )
	{
		app->runFrame();
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	}

	return 0;
}
