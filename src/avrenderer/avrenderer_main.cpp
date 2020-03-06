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

// XXX

#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <filesystem>

// #define CINTERFACE
// #define D3D11_NO_HELPERS
#include <windows.h>
// #include <D3D11_4.h>
// #include <DXGI1_4.h>
// #include <wrl.h>

// #include "device/vr/detours/detours.h"
#include "json.hpp"

// #include "device/vr/openvr/test/out.h"
// #include "third_party/openvr/src/src/vrcommon/sharedlibtools_public.h"
// #include "device/vr/openvr/test/fake_openvr_impl_api.h"
#include "base64.h"

#include "javascript_renderer.h"

#include "out.h"

using json = nlohmann::json;
using Base64 = macaron::Base64;

std::string logSuffix = "_native";
HWND g_hWnd = NULL;
// CHAR s_szDllPath[MAX_PATH] = "vrclient_x64.dll";
extern std::string dllDir;

DWORD chromePid = 0;
HWND chromeHwnd = NULL;

char kProcess_SetIsVr[] = "IVRCompositor::kIVRCompositor_SetIsVr";
char kProcess_SetTransform[] = "IVRCompositor::kIVRCompositor_SetTransform";
char kProcess_PrepareBindSurface[] = "IVRCompositor::kIVRCompositor_PrepareBindSurface";
char kProcess_SetDepthRenderEnabled[] = "IVRCompositor::kIVRCompositor_SetDepthRenderEnabled";
char kProcess_SetQrEngineEnabled[] = "IVRCompositor::kIVRCompositor_SetQrEngineEnabled";
char kProcess_GetQrCodes[] = "IVRCompositor::GetQrCodes";
char kProcess_SetCvEngineEnabled[] = "IVRCompositor::kIVRCompositor_SetCvEngineEnabled";
char kProcess_GetCvFeature[] = "IVRCompositor::GetCvFeature";
char kProcess_AddCvFeature[] = "IVRCompositor::AddCvFeature";
char kProcess_Terminate[] = "Process::Terminate";

class HwndSearchStruct {
public:
  const std::string &s;
  HWND hwnd;
};
BOOL CALLBACK enumWindowsProc(
  __in HWND hwnd,
  __in LPARAM lParam
) {
  HwndSearchStruct &o = *((HwndSearchStruct *)lParam);

  CHAR windowTitle[1024];
  GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
  
  std::cout << "get window title: " << windowTitle << std::endl;

  if (o.s == windowTitle) {
    o.hwnd = hwnd;
    return false;
  } else {
    return true;
  }
}
/* HWND getHwndFromPid(DWORD pid) {
  HwndSearchStruct o{
    pid,
    (HWND)NULL
  };
  EnumWindows(enumWindowsProc, (LPARAM)&o);
  return o.hwnd;
} */
HWND getHwndFromTitle(const std::string &s) {
  HwndSearchStruct o{
    s,
    (HWND)NULL
  };
  EnumWindows(enumWindowsProc, (LPARAM)&o);
  return o.hwnd;
}

inline uint32_t divCeil(uint32_t x, uint32_t y) {
  return (x + y - 1) / y;
}

size_t ids = 0;

constexpr uint32_t chunkSize = 1000*1000;
void respond(const json &j) {
  std::string outString = j.dump();
  uint32_t outSize = (uint32_t)outString.size();
  if (outSize < chunkSize) {
    std::cout.write((char *)&outSize, sizeof(outSize));
    std::cout.write(outString.data(), outString.size());
  } else {
    uint32_t numChunks = divCeil(outSize, chunkSize);
    // std::cout << "write chunks " << outSize << " " << chunkSize << " " << numChunks << std::endl;
    for (uint32_t i = 0; i < numChunks; i++) {
      // std::cout << "sending " << i << " " << numChunks << " " << outString.substr(i*chunkSize, chunkSize).size() << std::endl;
      json j2 = {
        {"index", i},
        {"total", numChunks},
        {"continuation", outString.substr(i*chunkSize, chunkSize)},
      };
      std::string outString2 = j2.dump();
      uint32_t outSize2 = (uint32_t)outString2.size();
      std::cout.write((char *)&outSize2, sizeof(outSize2));
      std::cout.write(outString2.data(), outString2.size());
    }
    // std::cout << "done sending" << std::endl;
  }
}

// OS specific macros for the example main entry points
// int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
int main(int argc, char **argv) {
  tools::initLogs();
  
  std::cout << "Start" << std::endl;
  
  std::unique_ptr<CAardvarkCefApp> app(new CAardvarkCefApp());

  std::thread renderThread([&]() -> void {
    while (!app->wantsToQuit()) {
      app->runFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  
  setmode(fileno(stdout), O_BINARY);
  setmode(fileno(stdin), O_BINARY);

  freopen(NULL, "rb", stdin);
  freopen(NULL, "wb", stdout);

  char cwdBuf[MAX_PATH];
  if (!GetCurrentDirectory(
    sizeof(cwdBuf),
    cwdBuf
  )) {
    std::cout << "failed to get current directory" << std::endl;
    abort();
  }

  std::cerr << "start native host" << std::endl;
  // std::cout << "start app" << std::endl;
  for (;;) {
    uint32_t size;
    std::cin.read((char *)&size, sizeof(uint32_t));
    // std::cout << "start app 2 " << size << " " << std::cin.good() << " " << std::cin.bad() << " " << std::cin.fail() << " " << std::cin.eof() << std::endl;
    if (std::cin.good()) {
      // std::cout << "start app 3 " << size << std::endl;
      std::vector<uint8_t> readbuf(size);
      std::cin.read((char *)readbuf.data(), readbuf.size());
      // std::cout << "start app 4" << std::endl;
      if (std::cin.good()) {
        // std::cout << "start app 5" << std::endl;
        json req = json::parse(readbuf);
        // std::cout << "start app 6 " << req.size() << std::endl;
        json method;
        json args;
        for (json::iterator it = req.begin(); it != req.end(); ++it) {
          // std::cout << it.key() << " " << it.value() << std::endl;
          if (it.key() == "method") {
            method = it.value();
          } else if (it.key() == "args") {
            args = it.value();
          }
        }
        
        if (method.is_string() && args.is_array()) {
          const std::string methodString = method.get<std::string>();
          std::cout << "method: " << methodString << std::endl;

          /* int i = 0;
          for (json::iterator it = args.begin(); it != args.end(); ++it) {
            const std::string argString = it->get<std::string>();
            std::cout << "arg " << i << ": " << argString << std::endl;
            i++;
          } */
           if (methodString == "addObject" && args.size() > 0 && args[0].is_string()) {
            std::string argString = args[0].get<std::string>();

            std::string name("object");
            name += std::to_string(++ids);

            auto model = app->renderer->m_renderer->createModelInstance(name, argString.data(), argString.size());
            // std::shared_ptr<vkglTF::Model> VulkanExample::findOrLoadModel( std::string modelUri, std::string *psError)
            
            json result = {
              // {"processId", processId}
            };
            json res = {
              {"error", nullptr},
              {"result", result}
            };
            respond(res);
          }
        }
      }
    }
  }

	return 0;
}
