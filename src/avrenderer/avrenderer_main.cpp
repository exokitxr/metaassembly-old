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
// HWND g_hWnd = NULL;
// CHAR s_szDllPath[MAX_PATH] = "vrclient_x64.dll";
std::string dllDir;

size_t ids = 0;

inline uint32_t divCeil(uint32_t x, uint32_t y) {
  return (x + y - 1) / y;
}
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
  
  getOut() << "Start" << std::endl;
  
  std::unique_ptr<CAardvarkCefApp> app(new CAardvarkCefApp());
  /* std::thread renderThread([&]() -> void {
    while (!app->wantsToQuit()) {
      app->runFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }); */

  {  
    char cwdBuf[MAX_PATH];
    if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
      getOut() << "failed to get current directory" << std::endl;
      abort();
    }
    dllDir = cwdBuf;
    dllDir += "\\";
    {
      std::string manifestTemplateFilePath = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(..\..\..\avrenderer\native-manifest-template.json)EOF"))).string();
      std::string manifestFilePath = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(\native-manifest.json)EOF"))).string();

      std::string s;
      {
        std::ifstream inFile(manifestTemplateFilePath);
        s = std::string((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
      }
      {
        json j = json::parse(s);
        j["path"] = std::filesystem::weakly_canonical(std::filesystem::path(dllDir + std::string(R"EOF(\avrenderer.exe)EOF"))).string();
        s = j.dump(2);
      }
      {    
        std::ofstream outFile(manifestFilePath);
        outFile << s;
        outFile.close();
      }
      
      HKEY hKey;
      LPCTSTR sk = R"EOF(Software\Google\Chrome\NativeMessagingHosts\com.exokit.xrchrome)EOF";
      LONG openRes = RegOpenKeyEx(HKEY_CURRENT_USER, sk, 0, KEY_ALL_ACCESS , &hKey);
      if (openRes == ERROR_FILE_NOT_FOUND) {
        openRes = RegCreateKeyExA(HKEY_CURRENT_USER, sk, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
        
        if (openRes != ERROR_SUCCESS) {
          getOut() << "failed to create registry key: " << (void*)openRes << std::endl;
          abort();
        }
      } else if (openRes != ERROR_SUCCESS) {
        getOut() << "failed to open registry key: " << (void*)openRes << std::endl;
        abort();
      }

      LPCTSTR value = "";
      LPCTSTR data = manifestFilePath.c_str();
      LONG setRes = RegSetValueEx(hKey, value, 0, REG_SZ, (LPBYTE)data, strlen(data)+1);
      if (setRes != ERROR_SUCCESS) {
        getOut() << "failed to set registry key: " << (void*)setRes << std::endl;
        abort();
      }

      LONG closeRes = RegCloseKey(hKey);
      if (closeRes != ERROR_SUCCESS) {
        getOut() << "failed to close registry key: " << (void*)closeRes << std::endl;
        abort();
      }
    }
  }
  {  
    app->startRenderer();
    Sleep(1000);
    std::string name("objectTest");
    std::vector<float> positions{
      -0.1, 0.5, 0,
      0.1, 0.5, 0,
      -0.1, -0.5, 0,
      0.1, -0.5, 0,
    };
    std::vector<float> normals{
      0, 0, 1,
      0, 0, 1,
      0, 0, 1,
      0, 0, 1,
    };
    std::vector<float> colors{
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
    };
    std::vector<float> uvs{
      0, 1,
      1, 1,
      0, 0,
      1, 0,
    };
    std::vector<uint16_t> indices{
      0, 2, 1,
      2, 3, 1,
    };
    auto model = app->renderer->m_renderer->createModelInstance(name, positions, normals, colors, uvs, indices);
    app->renderer->m_renderer->addToRenderList(model.release());
  }
  getOut() << "part 2" << std::endl;
  {
    setmode(fileno(stdout), O_BINARY);
    setmode(fileno(stdin), O_BINARY);

    freopen(NULL, "rb", stdin);
    freopen(NULL, "wb", stdout);

    char cwdBuf[MAX_PATH];
    if (!GetCurrentDirectory(
      sizeof(cwdBuf),
      cwdBuf
    )) {
      getOut() << "failed to get current directory" << std::endl;
      abort();
    }

    getOut() << "start native host" << std::endl;
    for (;;) {
      uint32_t size;
      std::cin.read((char *)&size, sizeof(uint32_t));
      if (std::cin.good()) {
        std::vector<uint8_t> readbuf(size);
        std::cin.read((char *)readbuf.data(), readbuf.size());
        if (std::cin.good()) {
          json req = json::parse(readbuf);
          json method;
          json args;
          for (json::iterator it = req.begin(); it != req.end(); ++it) {
            if (it.key() == "method") {
              method = it.value();
            } else if (it.key() == "args") {
              args = it.value();
            }
          }
          
          if (method.is_string() && args.is_array()) {
            const std::string methodString = method.get<std::string>();
            getOut() << "method: " << methodString << std::endl;

            /* int i = 0;
            for (json::iterator it = args.begin(); it != args.end(); ++it) {
              const std::string argString = it->get<std::string>();
              std::cout << "arg " << i << ": " << argString << std::endl;
              i++;
            } */
            if (methodString == "startRenderer") {
              app->startRenderer();

              json result = {
                // {"processId", processId}
              };
              json res = {
                {"error", nullptr},
                {"result", result}
              };
              respond(res);
            } else if (
              methodString == "addObject" &&
              args.size() >= 5 &&
              args[0].is_string() &&
              args[1].is_string() &&
              args[2].is_string() &&
              args[3].is_string() &&
              args[4].is_string()
            ) {
              std::vector<float> positions = Base64::Decode<float>(args[0].get<std::string>());
              std::vector<float> normals = Base64::Decode<float>(args[1].get<std::string>());
              std::vector<float> colors = Base64::Decode<float>(args[2].get<std::string>());
              std::vector<float> uvs = Base64::Decode<float>(args[3].get<std::string>());
              std::vector<uint16_t> indices = Base64::Decode<uint16_t>(args[4].get<std::string>());

              std::string name("object");
              name += std::to_string(++ids);

              models[name] = app->renderer->m_renderer->createDefaultModelInstance(name);
              app->renderer->m_renderer->addToRenderList(models[name].get());
              // std::shared_ptr<vkglTF::Model> VulkanExample::findOrLoadModel( std::string modelUri, std::string *psError)
              
              json result = {
                {"id", name}
              };
              json res = {
                {"error", nullptr},
                {"result", result}
              };
              respond(res);
            } else if (
              methodString == "updateObjectTransform" &&
              args.size() >= 4 &&
              args[0].is_string() &&
              args[1].is_string() &&
              args[2].is_string() &&
              args[3].is_string()
            ) {
              std::string name = args[0].get<std::string>();
              std::vector<float> position = Base64::Decode<float>(args[1].get<std::string>());
              std::vector<float> quaternion = Base64::Decode<float>(args[2].get<std::string>());
              std::vector<float> scale = Base64::Decode<float>(args[3].get<std::string>());

              auto model = models[name].get();
              app->renderer->m_renderer->setModelTransform(models[name].get(), position, quaternion, scale);
              // XXX update geometry
              
              json result = {
                // {"processId", processId}
              };
              json res = {
                {"error", nullptr},
                {"result", result}
              };
              respond(res);
            } else if (
              methodString == "updateObjectGeometry" &&
              args.size() >= 6 &&
              args[0].is_string() &&
              args[1].is_string() &&
              args[2].is_string() &&
              args[3].is_string() &&
              args[4].is_string() &&
              args[5].is_string()
            ) {
              std::string name = args[0].get<std::string>();
              std::vector<float> positions = Base64::Decode<float>(args[1].get<std::string>());
              std::vector<float> normals = Base64::Decode<float>(args[2].get<std::string>());
              std::vector<float> colors = Base64::Decode<float>(args[3].get<std::string>());
              std::vector<float> uvs = Base64::Decode<float>(args[4].get<std::string>());
              std::vector<uint16_t> indices = Base64::Decode<uint16_t>(args[5].get<std::string>());

              auto model = models[name].get();
              // XXX update geometry
              
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
  }

	return 0;
}
