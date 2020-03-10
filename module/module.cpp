#include <string.h>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <thread>
#include <functional>

#include <v8.h>
#include <nan.h>

#define JS_STR(...) Nan::New<v8::String>(__VA_ARGS__).ToLocalChecked()

using namespace v8;

namespace exokit {

NAN_METHOD(Log) {
  Local<Uint8Array> array = Local<Uint8Array>::Cast(info[0]);
  char *data = (char *)array->Buffer()->GetContents().Data() + array->ByteOffset();
  size_t length = array->ByteLength();
  fwrite(data, length, 1, stdout);
}

Local<Object> makeHandler() {
  Nan::EscapableHandleScope scope;

  Local<Object> result = Nan::New<Object>();
  
  Local<FunctionTemplate> logFnTemplate = Nan::New<FunctionTemplate>(Log);
  Local<Function> logFn = Nan::GetFunction(logFnTemplate).ToLocalChecked();
  result->Set(Isolate::GetCurrent()->GetCurrentContext(), JS_STR("Log"), logFn);

  return scope.Escape(result);
}

NAN_MODULE_INIT(InitAll) {
  Local<Value> handler = makeHandler();
  target->Set(Isolate::GetCurrent()->GetCurrentContext(), JS_STR("handler"), handler);
}

}

NODE_MODULE(exokit, exokit::InitAll)