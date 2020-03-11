#include "renderer.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"

// #include <tools/logging.h>

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

#include <psapi.h>

// #include "device/vr/detours/detours.h"
#include "json.hpp"

// #include "device/vr/openvr/test/out.h"
// #include "third_party/openvr/src/src/vrcommon/sharedlibtools_public.h"
// #include "device/vr/openvr/test/fake_openvr_impl_api.h"
#include "base64.h"

#include "javascript_renderer.h"
#include <glm/gtc/type_ptr.hpp>

#include "out.h"
#include "file_io.h"

using json = nlohmann::json;
using Base64 = macaron::Base64;
using namespace v8;

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

void terminateProcesses(const std::vector<const char *> &candidateFilenames) {
  char cwdBuf[MAX_PATH];
  if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
    getOut() << "failed to get current directory" << std::endl;
    abort();
  }
  std::string cwdBufString(cwdBuf);
  
  DWORD aProcesses[1024], cbNeeded, cProcesses;
  if (EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) {
    cProcesses = cbNeeded / sizeof(DWORD);

    bool matchedSelf = false;
    for (DWORD i = 0; i < cProcesses; i++) {
      DWORD pid = aProcesses[i];
      if (pid != 0) {
        HANDLE h = OpenProcess(
          PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE,
          FALSE,
          pid
        );
        if (h) {
          char p[MAX_PATH];
          if (GetModuleFileNameExA(h, 0, p, MAX_PATH)) {
            std::string pString(p);
            if (pString.rfind(cwdBufString, 0) == 0) {
              std::string filenameString(p + cwdBufString.length() + 1); // cwd slash
              bool match = false;
              for (auto candidateFileName : candidateFilenames) {
                if (filenameString == candidateFileName) {
                  if (pid != GetCurrentProcessId()) {
                    match = true;
                  } else {
                    matchedSelf = true;
                  }
                  break;
                }
              }
              if (match) {
                getOut() << "terminate process " << p << " " << pid << std::endl;
                if (!TerminateProcess(h, 0)) {
                  getOut() << "failed to terminate process " << p << " " << pid << " " << (void *)GetLastError() << std::endl;
                }
              }
            }
          } else {
            getOut() << "failed to get process file name: " << (void *)GetLastError() << std::endl;
          }
          CloseHandle(h);
        }
      }
    }
    if (matchedSelf) {
      getOut() << "terminating self" << std::endl;
      ExitProcess(0);
    }
  } else {
    getOut() << "failed to enum chrome processes" << std::endl;
  }
}
void terminateKnownProcesses() {
  terminateProcesses(std::vector<const char *>{
    "Chrome-bin\\chrome.exe",
  });
  terminateProcesses(std::vector<const char *>{
    "avrenderer.exe",
  });
  /* terminateProcesses(std::vector<const char *>{
    "av.exe",
  }); */
}

NAN_METHOD(handleMessage) {
  Nan::Utf8String methodUtf8(info[0]);
  std::string methodString = *methodUtf8;
  Local<Array> args = Local<Array>::Cast(info[1]);
  
  std::cout << "method: " << method << " " << args.Length();
  if (
    methodString == "startRenderer"
  ) {
    app.reset(new CAardvarkCefApp());
    app->startRenderer();
    auto appPtr = app.get();
    std::thread([appPtr]() {
      while (appPtr->tickRenderer()) {
        // XXX
        /* float hmd[16];
        float left[16];
        float right[16];
        appPtr->getPoses(hmd, left, right);

        json hmdArray = json::array();
        json leftArray = json::array();
        json rightArray = json::array();
        for (size_t i = 0; i < ARRAYSIZE(hmd); i++) {
          hmdArray.push_back(hmd[i]);
          leftArray.push_back(left[i]);
          rightArray.push_back(right[i]);
        }

        // getOut() << "emit event" << hmd[0] << " " << hmd[1] << " " << hmd[2] << " " << hmd[3] << std::endl;

        json event = {
          {"event", "pose"},
          {"data", {
            {"hmd", hmdArray},
            {"left", leftArray},
            {"right", rightArray},
          }},
        };
        respond(event); */

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      getOut() << "quitting" << std::endl;
      ExitProcess(0);
    }).detach();
    
    getOut() << "respond 1" << std::endl;

    Sleep(2000);

    getOut() << "respond 2" << std::endl;

    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("ok"), Nan::New<Boolea>(true));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "addModel" &&
    args.Length() >= 2 &&
    args[0]->IsString() &&
    args[1]->IsString()
  ) {
    // getOut() << "add model 1" << std::endl;
    std::string name = args[0].get<std::string>();
    std::vector<char> data = Base64::Decode<char>(args[1].get<std::string>());
    // getOut() << "add model 2 " << name << " " << data.size() << std::endl;

    models[name] = app->renderer->m_renderer->loadModelInstance(name, std::move(data));

    std::vector<float> boneTexture(128*16);
    glm::mat4 jointMat = glm::translate(glm::mat4{1}, glm::vec3(0, 0.2, 0));
    for (size_t i = 0; i < boneTexture.size(); i += 16) {
      memcpy(&boneTexture[i], &jointMat, sizeof(float)*16);
    }
    app->renderer->m_renderer->setBoneTexture(models[name].get(), boneTexture);
    
    app->renderer->m_renderer->addToRenderList(models[name].get());

    // getOut() << "add model 3" << std::endl;

    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "addObject" &&
    args.Length() >= 5 &&
    args[0]->IsString() &&
    args[1]->IsString() &&
    args[2]->IsString() &&
    args[3]->IsString() &&
    args[4]->IsString()
  ) {
    std::vector<float> positions = Base64::Decode<float>(args[0].get<std::string>());
    std::vector<float> normals = Base64::Decode<float>(args[1].get<std::string>());
    std::vector<float> colors = Base64::Decode<float>(args[2].get<std::string>());
    std::vector<float> uvs = Base64::Decode<float>(args[3].get<std::string>());
    std::vector<uint16_t> indices = Base64::Decode<uint16_t>(args[4].get<std::string>());

    std::string name("object");
    name += std::to_string(++ids);

    models[name] = app->renderer->m_renderer->createDefaultModelInstance(name);
    models[name] = app->renderer->m_renderer->setModelGeometry(std::move(models[name]), positions, normals, colors, uvs, indices);
    std::vector<unsigned char> image = {
      255,
      0,
      0,
      255,
    };
    models[name] = app->renderer->m_renderer->setModelTexture(std::move(models[name]), 1, 1, std::move(image));
    
    app->renderer->m_renderer->addToRenderList(models[name].get());

    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "updateObjectTransform" &&
    args.Length() >= 4 &&
    args[0]->IsString() &&
    args[1]->IsString() &&
    args[2]->IsString() &&
    args[3]->IsString()
  ) {
    std::string name = args[0].get<std::string>();
    std::vector<float> position = Base64::Decode<float>(args[1].get<std::string>());
    std::vector<float> quaternion = Base64::Decode<float>(args[2].get<std::string>());
    std::vector<float> scale = Base64::Decode<float>(args[3].get<std::string>());

    app->renderer->m_renderer->setModelTransform(models[name].get(), position, quaternion, scale);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "updateObjectMatrix" &&
    args.Length() >= 2 &&
    args[0]->IsString() &&
    args[1]->IsString()
  ) {
    std::string name = args[0].get<std::string>();
    std::vector<float> updateObjectMatrix = Base64::Decode<float>(args[1].get<std::string>());

    app->renderer->m_renderer->setModelMatrix(models[name].get(), updateObjectMatrix);

    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "updateObjectBoneTexture" &&
    args.Length() >= 2 &&
    args[0]->IsString() &&
    args[1]->IsString()
  ) {
    std::string name = args[0].get<std::string>();
    std::vector<float> boneTexture = Base64::Decode<float>(args[1].get<std::string>());

    app->renderer->m_renderer->setBoneTexture(models[name].get(), boneTexture);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "updateObjectGeometry" &&
    args.Length() >= 6 &&
    args[0]->IsString() &&
    args[1]->IsString() &&
    args[2]->IsString() &&
    args[3]->IsString() &&
    args[4]->IsString() &&
    args[5]->IsString()
  ) {
    std::string name = args[0].get<std::string>();
    std::vector<float> positions = Base64::Decode<float>(args[1].get<std::string>());
    std::vector<float> normals = Base64::Decode<float>(args[2].get<std::string>());
    std::vector<float> colors = Base64::Decode<float>(args[3].get<std::string>());
    std::vector<float> uvs = Base64::Decode<float>(args[4].get<std::string>());
    std::vector<uint16_t> indices = Base64::Decode<uint16_t>(args[5].get<std::string>());

    models[name] = app->renderer->m_renderer->setModelGeometry(std::move(models[name]), positions, normals, colors, uvs, indices);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "updateObjectTexture" &&
    args.Length() >= 4 &&
    args[0]->IsString() &&
    args[1]->IsNumber() &&
    args[2]->IsNumber() &&
    args[3]->IsString()
  ) {
    std::string name = args[0].get<std::string>();
    int width = args[1].get<int>();
    int height = args[2].get<int>();
    std::vector<uint8_t> data = Base64::Decode<uint8_t>(args[3].get<std::string>());

    models[name] = app->renderer->m_renderer->setModelTexture(std::move(models[name]), width, height, std::move(data));
    
    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("id"), Nan::New<String>(name));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else if (
    methodString == "terminate"
  ) {
    getOut() << "call terminate 1" << std::endl;
    terminateKnownProcesses();
    getOut() << "call terminate 2" << std::endl;

    Local<Object> result = Nan::New<Object>();
    result->Set(JS_STR("ok"), Nan::New<Boolean>(true));
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("result"), result);
    info.GetReturnValue().Set(result);
  } else {
    std::string errorMsg = std::string("unknown method: ") + methodString + " " + std::to_string(args.size());
    Local<Object> error = Nan::New<String>(errorMsg);
    Local<Object> res = Nan::New<Object>();
    result->Set(JS_STR("error"), error);
    info.GetReturnValue().Set(result);
  }
}