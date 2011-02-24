#include "JSContext.hpp"
#include <v8.h>
#include "../JSObjectScript.hpp"
#include "../JSSerializer.hpp"
#include "JSObjectsUtils.hpp"
#include "JSHandler.hpp"
#include "JSVec3.hpp"
#include "../JSUtil.hpp"
#include "../JSObjectStructs/JSContextStruct.hpp"
#include <math.h>
#include <stdlib.h>
#include <float.h>

namespace Sirikata{
namespace JS{
namespace JSContext{



v8::Handle<v8::Value> ScriptSuspend(const v8::Arguments& args)
{
    if (args.Length() != 0)
        return v8::ThrowException( v8::Exception::Error(v8::String::New("Invalid arguments to ScriptSuspend of JSContext.cpp through context.  Suspension takes zero arguments")));

    String errorMessage = "Error decoding the context struct while calling suspend in JScontext.cpp.  ";
    JSContextStruct* jscont = JSContextStruct::decodeContextStruct(args.This(),errorMessage);
    if (jscont == NULL)
        return v8::ThrowException(v8::Exception::Error(v8::String::New(errorMessage.c_str(), errorMessage.length())));

    return jscont->suspend();
}


v8::Handle<v8::Value> ScriptResume(const v8::Arguments& args)
{
    if (args.Length() != 0)
        return v8::ThrowException( v8::Exception::Error(v8::String::New("Invalid arguments to ScriptResume of JSContext.cpp through context.  Resume takes zero arguments")));

    String errorMessage = "Error decoding the context struct while calling resume in JScontext.cpp.  ";
    JSContextStruct* jscont = JSContextStruct::decodeContextStruct(args.This(),errorMessage);
    if (jscont == NULL)
        return v8::ThrowException(v8::Exception::Error(v8::String::New(errorMessage.c_str(), errorMessage.length())));

    return jscont->resume();
}
    




v8::Handle<v8::Value> ScriptExecute(const v8::Arguments& args)
{
    if (args.Length() == 0)
        return v8::ThrowException( v8::Exception::Error(v8::String::New("Invalid arguments to ScriptExecute through context.  Require first argument to be a function.  Later arguments should be arguments for that function.")));

    //ensure first argument is function.
    v8::Handle<v8::Value> func_args = args[0];
    if (! func_args->IsFunction())
        return v8::ThrowException( v8::Exception::Error(v8::String::New("Invalid arguments to ScriptExecute through context.  Require first argument to be a function")));

    //FIXME: need to check if the arguments passed on the scriptexecute call match the arguments required by the function;


    String errorMessage = "Error in ScriptExecute of context.  ";
    JSContextStruct* jscontstruct = JSContextStruct::decodeContextStruct(args.This(), errorMessage);
    if (jscontstruct == NULL)
        return v8::ThrowException( v8::Exception::Error(v8::String::New(errorMessage.c_str(),errorMessage.length())));

    //first argument is a function
    //subsequent arguments are the arguments to that function

    v8::Handle<v8::Function> exec_func = v8::Handle<v8::Function>::Cast(args[0]);
    return jscontstruct->struct_executeScript(exec_func,args);
}


}//jscontext namespace
}//js namespace
}//sirikata namespace
