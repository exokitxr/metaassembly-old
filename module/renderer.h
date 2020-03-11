#ifndef _renderer_h_
#define _renderer_h_

#include <nan.h>

void initAsync();
NAN_METHOD(setEventHandler);
NAN_METHOD(handleMessage);

#endif