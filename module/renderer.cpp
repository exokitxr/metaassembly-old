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
#include <mutex>

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

class MessageStruct {
public:
  float hmd[16];
  float left[16];
  float right[16];
};

uv_async_t eventAsync;
std::mutex mutex;
Nan::Persistent<Function> eventCbFn;
std::vector<MessageStruct> messages;
void RunAsync(uv_async_t *handle) {
  Nan::HandleScope scope;

  {
    std::lock_guard<std::mutex> lock(mutex);

    if (!eventCbFn.IsEmpty()) {
      Local<Function> localEventCbFn = Nan::New(eventCbFn);

      for (size_t i = 0; i < messages.size(); i++) {
        const MessageStruct &message = messages[i];

        Local<Object> event = Nan::New<Object>();
        Local<ArrayBuffer> hmdBuffer = ArrayBuffer::New(Isolate::GetCurrent(), sizeof(message.hmd));
        memcpy(hmdBuffer->GetContents().Data(), message.hmd, sizeof(message.hmd));
        Local<Float32Array> hmd = Float32Array::New(hmdBuffer, 0, ARRAYSIZE(message.hmd));

        Local<ArrayBuffer> leftBuffer = ArrayBuffer::New(Isolate::GetCurrent(), sizeof(message.left));
        memcpy(leftBuffer->GetContents().Data(), message.left, sizeof(message.left));
        Local<Float32Array> left = Float32Array::New(leftBuffer, 0, ARRAYSIZE(message.left));

        Local<ArrayBuffer> rightBuffer = ArrayBuffer::New(Isolate::GetCurrent(), sizeof(message.right));
        memcpy(rightBuffer->GetContents().Data(), message.right, sizeof(message.right));
        Local<Float32Array> right = Float32Array::New(rightBuffer, 0, ARRAYSIZE(message.right));

        event->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("hmd").ToLocalChecked(), hmd);
        event->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("left").ToLocalChecked(), left);
        event->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("right").ToLocalChecked(), right);

        /* json event = {
          {"event", "pose"},
          {"data", {
            {"hmd", hmdArray},
            {"left", leftArray},
            {"right", rightArray},
          }},
        };
        respond(event); */

        Local<Value> argv[] = {
          event,
        };
        localEventCbFn->Call(Isolate::GetCurrent()->GetCurrentContext(), Nan::Null(), sizeof(argv)/sizeof(argv[0]), argv);
      }

      messages.clear();
    }
  }
}
void initAsync() {
  uv_loop_t *loop = node::GetCurrentEventLoop(Isolate::GetCurrent());
  uv_async_init(loop, &eventAsync, RunAsync);
}

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

template<typename T>
inline std::pair<T, size_t> getArrayData(Local<Value> arg) {
  Local<Array> dataArray = Local<Array>::Cast(arg);
  Local<ArrayBuffer> dataValue = Local<ArrayBuffer>::Cast(dataArray->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
  unsigned char *data = (unsigned char *)dataValue->GetContents().Data();
  uintptr_t byteOffset = dataArray->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->Uint32Value(Isolate::GetCurrent()->GetCurrentContext()).ToChecked();
  data += byteOffset;
  size_t size = dataArray->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked()->Uint32Value(Isolate::GetCurrent()->GetCurrentContext()).ToChecked();
  T datas = (T)data;
  size_t numDatas = size/sizeof(datas[0]);
  return std::pair<T, size_t>{
    datas,
    numDatas,
  };
}

NAN_METHOD(setEventHandler) {
  std::lock_guard<std::mutex> lock(mutex);
  
  eventCbFn.Reset(Local<Function>::Cast(info[0]));
}

std::unique_ptr<CAardvarkCefApp> app;
size_t ids = 0;
std::map<std::string, std::unique_ptr<IModelInstance>> models;
void infoQueueLog() {
  ID3D11InfoQueue *infoQueue = nullptr;
  auto hr = app->m_pD3D11Device->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&infoQueue);
  if (FAILED(hr)) {
    getOut() << "failed to get info queue" << std::endl;
  }
  UINT64 numStoredMessages = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
  for (UINT64 i = 0; i < numStoredMessages; i++) {
    size_t messageSize = 0;
    hr = infoQueue->GetMessage(
      i,
      nullptr,
      &messageSize
    );
    if (SUCCEEDED(hr)) {
      D3D11_MESSAGE *message = (D3D11_MESSAGE *)malloc(messageSize);
      
      hr = infoQueue->GetMessage(
        i,
        message,
        &messageSize
      );
      if (SUCCEEDED(hr)) {
        // if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
          getOut() << "info: " << message->Severity << " " << std::string(message->pDescription, message->DescriptionByteLength) << std::endl;
        // }
      } else {
        getOut() << "failed to get info queue message size: " << (void *)hr << std::endl;
      }
      
      free(message);
    } else {
      getOut() << "failed to get info queue message size: " << (void *)hr << std::endl;
    }
  }
  infoQueue->ClearStoredMessages();
  infoQueue->Release();
  
  getOut() << "info queue done" << std::endl;
}
NAN_METHOD(handleMessage) {
  Nan::Utf8String methodUtf8(info[0]);
  std::string methodString = *methodUtf8;
  Local<Array> args = Local<Array>::Cast(info[1]);
  
  // std::cout << "method: " << methodString << " " << args->Length() << std::endl;
  if (
    methodString == "startRenderer"
  ) {
    app.reset(new CAardvarkCefApp());
    app->startRenderer();
    auto appPtr = app.get();
    std::thread([appPtr]() {
      while (appPtr->tickRenderer()) {
        MessageStruct message;
        appPtr->getPoses(message.hmd, message.left, message.right);

        // getOut() << "emit event" << hmd[0] << " " << hmd[1] << " " << hmd[2] << " " << hmd[3] << std::endl;
        
        {
          std::lock_guard<std::mutex> lock(mutex);
          messages.push_back(message);
        }
        uv_async_send(&eventAsync);

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      getOut() << "quitting" << std::endl;
      ExitProcess(0);
    }).detach();
    
    // getOut() << "respond 1" << std::endl;

    // Sleep(2000);

    // getOut() << "respond 2" << std::endl;

    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("ok").ToLocalChecked(), Nan::New<Boolean>(true));
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
    
    // getOut() << "respond 3" << std::endl;
  } else if (
    methodString == "addModel" &&
    args->Length() >= 2 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    auto data = getArrayData<unsigned char *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());

    models[name] = app->renderer->m_renderer->loadModelInstance(name, data.first, data.second);

    std::vector<float> boneTexture(128*16);
    glm::mat4 jointMat = glm::translate(glm::mat4{1}, glm::vec3(0, 0.2, 0));
    for (size_t i = 0; i < boneTexture.size(); i += 16) {
      memcpy(&boneTexture[i], &jointMat, sizeof(float)*16);
    }
    app->renderer->m_renderer->setBoneTexture(models[name].get(), boneTexture.data(), boneTexture.size());
    
    app->renderer->m_renderer->addToRenderList(models[name].get());

    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "addObject" &&
    args->Length() >= 5 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 4).ToLocalChecked()->IsArray()
  ) {
    auto positions = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    auto normals = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());
    auto colors = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked());
    auto uvs = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked());
    auto indices = getArrayData<uint16_t *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 4).ToLocalChecked());

    std::string name("object");
    name += std::to_string(++ids);

    models[name] = app->renderer->m_renderer->createDefaultModelInstance(name);
    models[name] = app->renderer->m_renderer->setModelGeometry(std::move(models[name]), positions.first, positions.second, normals.first, normals.second, colors.first, colors.second, uvs.first, uvs.second, indices.first, indices.second);
    std::vector<unsigned char> image = {
      255,
      0,
      0,
      255,
    };
    models[name] = app->renderer->m_renderer->setModelTexture(std::move(models[name]), 1, 1, image.data(), image.size());

    app->renderer->m_renderer->addToRenderList(models[name].get());

    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "updateObjectTransform" &&
    args->Length() >= 4 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    auto positions = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());
    auto quaternions = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked());
    auto scales = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked());

    app->renderer->m_renderer->setModelTransform(models[name].get(), positions.first, positions.second, quaternions.first, quaternions.second, scales.first, scales.second);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "updateObjectMatrix" &&
    args->Length() >= 2 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    auto matrix = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());

    app->renderer->m_renderer->setModelMatrix(models[name].get(), matrix.first, matrix.second);

    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "updateObjectBoneTexture" &&
    args->Length() >= 2 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    auto boneTexture = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());

    app->renderer->m_renderer->setBoneTexture(models[name].get(), boneTexture.first, boneTexture.second);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "updateObjectGeometry" &&
    args->Length() >= 6 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 4).ToLocalChecked()->IsArray() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 5).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    auto positions = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());
    auto normals = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked());
    auto colors = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked());
    auto uvs = getArrayData<float *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 4).ToLocalChecked());
    auto indices = getArrayData<uint16_t *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 5).ToLocalChecked());

    models[name] = app->renderer->m_renderer->setModelGeometry(std::move(models[name]), positions.first, positions.second, normals.first, normals.second, colors.first, colors.second, uvs.first, uvs.second, indices.first, indices.second);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "updateObjectTexture" &&
    args->Length() >= 4 &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked()->IsString() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked()->IsNumber() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked()->IsNumber() &&
    args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked()->IsArray()
  ) {
    Nan::Utf8String nameUtf8(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 0).ToLocalChecked());
    std::string name = *nameUtf8;
    Local<Number> widthValue = Local<Number>::Cast(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 1).ToLocalChecked());
    int width = widthValue->Int32Value(Isolate::GetCurrent()->GetCurrentContext()).ToChecked();
    Local<Number> heightValue = Local<Number>::Cast(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 2).ToLocalChecked());
    int height = heightValue->Int32Value(Isolate::GetCurrent()->GetCurrentContext()).ToChecked();
    auto data = getArrayData<unsigned char *>(args->Get(Isolate::GetCurrent()->GetCurrentContext(), 3).ToLocalChecked());

    models[name] = app->renderer->m_renderer->setModelTexture(std::move(models[name]), width, height, data.first, data.second);
    
    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("id").ToLocalChecked(), Nan::New<String>(name).ToLocalChecked());
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "getMirrorTexture"
  ) {
    getOut() << "get mirror texture " << (void *)app->m_pD3D11Device << std::endl;

    uint32_t width = 0;
    uint32_t height = 0;
    Local<ArrayBuffer> arrayBuffer;

    ID3D11ShaderResourceView *resourceView;
    vr::VRCompositor()->GetMirrorTextureD3D11(vr::Eye_Left, app->m_pD3D11Device, (void **)&resourceView);

    ID3D11Resource *resource = nullptr;
    resourceView->GetResource(&resource);
    if (resource) {
      ID3D11Texture2D *tex = nullptr;
      if (SUCCEEDED(resource->QueryInterface(&tex))) {
        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc); //Correct data gets filled out
        // D3D11_RESOURCE_DIMENSION dim;
        // resource->GetType(&dim); //value gets set as Texture2D which it should

        getOut() << "got tex desc " <<
          desc.Width << " " << desc.Height << " " <<
          desc.MipLevels << " " << desc.ArraySize << " " <<
          desc.SampleDesc.Count << " " << desc.SampleDesc.Quality << " " <<
          desc.Format << " " <<
          desc.Usage << " " << desc.BindFlags << " " << desc.CPUAccessFlags << " " << desc.MiscFlags <<
          std::endl;
        
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        
        ID3D11Texture2D *tex2;
        auto hr = app->m_pD3D11Device->CreateTexture2D(
          &desc,
          NULL,
          &tex2
        );
        if (FAILED(hr)) {
          getOut() << "create texture failed " << (void *)hr << std::endl;
          
          infoQueueLog();
        }
        app->m_pD3D11ImmediateContext->CopyResource(tex2, tex);
        
        getOut() << "map tex " << (void *)tex2 << std::endl;
        D3D11_MAPPED_SUBRESOURCE mappedResource{};
        hr = app->m_pD3D11ImmediateContext->Map(
          tex2,
          0,
          D3D11_MAP_READ,
          0,
          &mappedResource
        );
        if (SUCCEEDED(hr)) {
          getOut() << "map ok" << std::endl;
          
          width = desc.Width;
          height = desc.Height;
          arrayBuffer = ArrayBuffer::New(Isolate::GetCurrent(), desc.Width*desc.Height*4);
          void *data = arrayBuffer->GetContents().Data();
          
          UINT lBmpRowPitch = desc.Width * 4;
          BYTE *sptr = (BYTE *)mappedResource.pData;
          BYTE *dptr = (BYTE *)data;
          for (size_t h = 0; h < desc.Height; ++h) {
            memcpy(dptr, sptr, lBmpRowPitch);
            sptr += mappedResource.RowPitch;
            // dptr -= lBmpRowPitch;
            dptr += lBmpRowPitch;
          }
          // memcpy(, message.hmd, sizeof(message.hmd));
          // Local<Float32Array> hmd = Float32Array::New(hmdBuffer, 0, ARRAYSIZE(message.hmd));
          
          app->m_pD3D11ImmediateContext->Unmap(
            resource,
            0
          );
        } else {
          getOut() << "failed to map resource " << (void *)hr << std::endl;
          
          infoQueueLog();
        }

        tex->Release();
        tex2->Release();
      } else {
        getOut() << "failed to get tex" << std::endl;
      }
      resource->Release();
    } else {
      getOut() << "failed to get resource" << std::endl;
    }

    vr::VRCompositor()->ReleaseMirrorTextureD3D11(resourceView);

    Local<Object> res = Nan::New<Object>();
    if (!arrayBuffer.IsEmpty()) {
      getOut() << "result ok" << std::endl;
      Local<Object> result = Nan::New<Object>();
      result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("width").ToLocalChecked(), Nan::New<Number>(width));
      result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("height").ToLocalChecked(), Nan::New<Number>(height));
      result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("data").ToLocalChecked(), arrayBuffer);
      res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    } else {
      getOut() << "result empty" << std::endl;
      res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), Nan::Null());
    }
    info.GetReturnValue().Set(res);
  } else if (
    methodString == "terminate"
  ) {
    getOut() << "call terminate 1" << std::endl;
    terminateKnownProcesses();
    getOut() << "call terminate 2" << std::endl;

    Local<Object> result = Nan::New<Object>();
    result->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("ok").ToLocalChecked(), Nan::New<Boolean>(true));
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("result").ToLocalChecked(), result);
    info.GetReturnValue().Set(res);
  } else {
    getOut() << "unknown method: " << methodString << std::endl;
    std::string errorMsg = std::string("unknown method: ") + methodString + " " + std::to_string(args->Length());
    Local<String> error = Nan::New<String>(errorMsg).ToLocalChecked();
    Local<Object> res = Nan::New<Object>();
    res->Set(Isolate::GetCurrent()->GetCurrentContext(), Nan::New<String>("error").ToLocalChecked(), error);
    info.GetReturnValue().Set(res);
  }
}