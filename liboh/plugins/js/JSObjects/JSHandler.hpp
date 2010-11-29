

#include "../JSUtil.hpp"
#include "../JSObjectScript.hpp"
#include <v8.h>


namespace Sirikata {
namespace JS {
namespace JSHandler{


v8::Handle<v8::Value> __printContents(const v8::Arguments& args);
v8::Handle<v8::Value> __suspend(const v8::Arguments& args);
v8::Handle<v8::Value> __resume(const v8::Arguments& args);
v8::Handle<v8::Value> __isSuspended(const v8::Arguments& args);
v8::Handle<v8::Value> __clear(const v8::Arguments& args);


void readHandler(const v8::Arguments& args, JSObjectScript*& caller, JSEventHandler*& hand);
v8::Handle<v8::Value> makeEventHandler(JSObjectScript* target_script, JSEventHandler* evHand);
void setNullHandler(const v8::Arguments& args);


}}}//end namespaces
