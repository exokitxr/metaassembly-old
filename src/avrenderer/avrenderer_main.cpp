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
#include "io.h"

using json = nlohmann::json;
using Base64 = macaron::Base64;

std::string logSuffix = "_native_host";
// HWND g_hWnd = NULL;
// CHAR s_szDllPath[MAX_PATH] = "vrclient_x64.dll";
std::string dllDir;

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
int main(int argc, char **argv, char **envp) {
  tools::initLogs();
  
  getOut() << "Start" << std::endl;
  
  SetEnvironmentVariable("VR_OVERRIDE", nullptr);
  
  /* for (char **env = envp; *env != 0; env++) {
    char *thisEnv = *env;
    getOut() << thisEnv << std::endl; 
  }

  {
    char cwdBuf[MAX_PATH];
    if (!GetCurrentDirectory(
      sizeof(cwdBuf),
      cwdBuf
    )) {
      getOut() << "failed to get current directory" << std::endl;
      abort();
    }

    getOut() << "start native host " << cwdBuf << std::endl;
  } */

  std::unique_ptr<CAardvarkCefApp> app(new CAardvarkCefApp());
  /* std::thread renderThread([&]() -> void {
    while (!app->wantsToQuit()) {
      app->runFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }); */
  {  
    /* app->startRenderer();
    Sleep(2000);
    {
      std::string name("objectTest1");
      std::vector<char> data = readFile("data/avatar.glb");
      auto model = app->renderer->m_renderer->loadModelInstance(name, std::move(data));
      std::vector<float> boneTexture(128*16);
      glm::mat4 jointMat = glm::translate(glm::mat4{1}, glm::vec3(0, 0.2, 0));
      for (size_t i = 0; i < boneTexture.size(); i += 16) {
        memcpy(&boneTexture[i], &jointMat, sizeof(float)*16);
      }
      app->renderer->m_renderer->setBoneTexture(model.get(), boneTexture);
      app->renderer->m_renderer->addToRenderList(model.release());
    }
    {
      std::string name("objectTest2");
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
      auto model = app->renderer->m_renderer->createDefaultModelInstance(name);
      model = app->renderer->m_renderer->setModelGeometry(std::move(model), positions, normals, colors, uvs, indices);
      std::vector<unsigned char> image = {
        255,
        0,
        0,
        255,
      };
      model = app->renderer->m_renderer->setModelTexture(std::move(model), 1, 1, std::move(image));
      app->renderer->m_renderer->addToRenderList(model.release());
    } */
  }
  getOut() << "part 2" << std::endl;
  size_t ids = 0;
  std::map<std::string, std::unique_ptr<IModelInstance>> models;
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
            /* } else if (
              methodString == "addModel" &&
              args.size() >= 1 &&
              args[0].is_string()
            ) {
              std::vector<unsigned char> data = Base64::Decode<unsigned char>(args[0].get<std::string>());

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
              respond(res); */
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

              models[name] = app->renderer->m_renderer->setModelGeometry(std::move(models[name]), positions, normals, colors, uvs, indices);
              
              json result = {
                // {"processId", processId}
              };
              json res = {
                {"error", nullptr},
                {"result", result}
              };
              respond(res);
            } else if (
              methodString == "updateObjectTexture" &&
              args.size() >= 4 &&
              args[0].is_string() &&
              args[1].is_number() &&
              args[2].is_number() &&
              args[3].is_string()
            ) {
              std::string name = args[0].get<std::string>();
              int width = args[1].get<int>();
              int height = args[2].get<int>();
              std::vector<uint8_t> data = Base64::Decode<uint8_t>(args[3].get<std::string>());

              models[name] = app->renderer->m_renderer->setModelTexture(std::move(models[name]), width, height, std::move(data));
              
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
