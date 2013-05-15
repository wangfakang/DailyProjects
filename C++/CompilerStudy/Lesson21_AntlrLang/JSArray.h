
#ifndef JS_ARRAY_H
#define JS_ARRAY_H

#include "GCObject.h"
#include "JSValue.h"

struct JSArray:
    public GCObject {
    vector<JSValue> array;

    JSArray(): GCObject(GCT_Array){}
};

#endif
