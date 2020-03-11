#include <string.h>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <thread>
#include <functional>

#include <v8.h>
#include <nan.h>
#include "renderer.h"

#define JS_STR(...) Nan::New<v8::String>(__VA_ARGS__).ToLocalChecked()

using namespace v8;

namespace exokit {

/* NAN_METHOD(Log) {
  Local<Uint8Array> array = Local<Uint8Array>::Cast(info[0]);
  char *data = (char *)array->Buffer()->GetContents().Data() + array->ByteOffset();
  size_t length = array->ByteLength();
  fwrite(data, length, 1, stdout);
} */

NAN_MODULE_INIT(InitAll) {
  initAsync();
  
  Local<FunctionTemplate> setEventHandlerFnTemplate = Nan::New<FunctionTemplate>(setEventHandler);
  Local<Function> setEventHandlerFn = Nan::GetFunction(setEventHandlerFnTemplate).ToLocalChecked();
  target->Set(Isolate::GetCurrent()->GetCurrentContext(), JS_STR("setEventHandler"), setEventHandlerFn);
  
  Local<FunctionTemplate> handleMessageFnTemplate = Nan::New<FunctionTemplate>(handleMessage);
  Local<Function> handleMessageFn = Nan::GetFunction(handleMessageFnTemplate).ToLocalChecked();
  target->Set(Isolate::GetCurrent()->GetCurrentContext(), JS_STR("handleMessage"), handleMessageFn);
}

}

NODE_MODULE(exokit, exokit::InitAll)