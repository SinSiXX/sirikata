// Copyright (c) 2009 Sirikata Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "OgreSystemInputHandler.hpp"

#include "OgreSystem.hpp"
#include <sirikata/ogre/Camera.hpp>
#include <sirikata/ogre/Lights.hpp>
#include <sirikata/ogre/Entity.hpp>
#include <sirikata/proxyobject/ProxyManager.hpp>
#include <sirikata/proxyobject/ProxyObject.hpp>
#include <sirikata/ogre/input/InputEvents.hpp>

#include <sirikata/ogre/input/SDLInputDevice.hpp>
#include <sirikata/ogre/input/SDLInputManager.hpp>
#include <sirikata/ogre/input/InputManager.hpp>

#include <sirikata/core/task/Time.hpp>
#include <set>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include <sirikata/ogre/OgreConversions.hpp>

#include "ProxyEntity.hpp"

namespace Sirikata {
namespace Graphics {
using namespace Input;
using namespace Task;
using namespace std;


// == WebViewInputListener ==

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onKeyEvent(Input::ButtonEventPtr ev) {
    return WebViewManager::getSingleton().onButton(ev);
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onAxisEvent(Input::AxisEventPtr axisev) {
    float multiplier = mParent->mParent->getInputManager()->wheelToAxis();

    if (axisev->mAxis == SDLMouse::WHEELY) {
        bool used = WebViewManager::getSingleton().injectMouseWheel(WebViewCoord(0, axisev->mValue.getCentered()/multiplier));
        if (used)
            return EventResponse::cancel();
    }
    if (axisev->mAxis == SDLMouse::WHEELX) {
        bool used = WebViewManager::getSingleton().injectMouseWheel(WebViewCoord(axisev->mValue.getCentered()/multiplier, 0));
        if (used)
            return EventResponse::cancel();
    }

    return EventResponse::nop();
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onTextInputEvent(Input::TextInputEventPtr textev) {
    return WebViewManager::getSingleton().onKeyTextInput(textev);
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onMouseHoverEvent(Input::MouseHoverEventPtr mouseev) {
    return WebViewManager::getSingleton().onMouseHover(mouseev);
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onMousePressedEvent(Input::MousePressedEventPtr mouseev) {
    EventResponse browser_resp = WebViewManager::getSingleton().onMousePressed(mouseev);
    if (browser_resp == EventResponse::cancel())
        mWebViewActiveButtons.insert(mouseev->mButton);
    return browser_resp;
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onMouseReleasedEvent(Input::MouseReleasedEventPtr ev) {
    return WebViewManager::getSingleton().onMouseReleased(ev);
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onMouseClickEvent(Input::MouseClickEventPtr mouseev) {
    EventResponse browser_resp = WebViewManager::getSingleton().onMouseClick(mouseev);
    if (mWebViewActiveButtons.find(mouseev->mButton) != mWebViewActiveButtons.end()) {
        mWebViewActiveButtons.erase(mouseev->mButton);
        return EventResponse::cancel();
    }
    return browser_resp;
}

Input::EventResponse OgreSystemInputHandler::WebViewInputListener::onMouseDragEvent(Input::MouseDragEventPtr ev) {
    std::set<int>::iterator iter = mWebViewActiveButtons.find(ev->mButton);
    if (iter == mWebViewActiveButtons.end()) return EventResponse::nop();

    // Give the browser a chance to use this input
    EventResponse browser_resp = WebViewManager::getSingleton().onMouseDrag(ev);

    if (ev->mType == Input::DRAG_END)
        mWebViewActiveButtons.erase(iter);

    return browser_resp;
}


// == DelegateInputListener ==


namespace {

// Fills in modifier fields
void fillModifiers(Invokable::Dict& event_data, Input::Modifier m) {
    Invokable::Dict mods;
    mods["shift"] = Invokable::asAny((bool)(m & MOD_SHIFT));
    mods["ctrl"] = Invokable::asAny((bool)(m & MOD_CTRL));
    mods["alt"] = Invokable::asAny((bool)(m & MOD_ALT));
    mods["super"] = Invokable::asAny((bool)(m & MOD_GUI));
    event_data["modifier"] = Invokable::asAny(mods);
}

}

void OgreSystemInputHandler::DelegateInputListener::delegateEvent(InputEventPtr inputev) {
    if (mDelegates.empty())
        return;

    Invokable::Dict event_data;
    {
        ButtonPressedEventPtr button_pressed_ev (std::tr1::dynamic_pointer_cast<ButtonPressed>(inputev));
        if (button_pressed_ev) {

            event_data["msg"] = Invokable::asAny(String("button-pressed"));
            event_data["button"] = Invokable::asAny(keyButtonString(button_pressed_ev->mButton));
            event_data["keycode"] = Invokable::asAny((int32)button_pressed_ev->mButton);
            fillModifiers(event_data, button_pressed_ev->mModifier);
        }
    }

    {
        ButtonRepeatedEventPtr button_pressed_ev (std::tr1::dynamic_pointer_cast<ButtonRepeated>(inputev));
        if (button_pressed_ev) {

            event_data["msg"] = Invokable::asAny(String("button-repeat"));
            event_data["button"] = Invokable::asAny(keyButtonString(button_pressed_ev->mButton));
            event_data["keycode"] = Invokable::asAny((int32)button_pressed_ev->mButton);
            fillModifiers(event_data, button_pressed_ev->mModifier);
        }
    }

    {
        ButtonReleasedEventPtr button_released_ev (std::tr1::dynamic_pointer_cast<ButtonReleased>(inputev));
        if (button_released_ev) {

            event_data["msg"] = Invokable::asAny(String("button-up"));
            event_data["button"] = Invokable::asAny(keyButtonString(button_released_ev->mButton));
            event_data["keycode"] = Invokable::asAny((int32)button_released_ev->mButton);
            fillModifiers(event_data, button_released_ev->mModifier);
        }
    }

    {
        ButtonDownEventPtr button_down_ev (std::tr1::dynamic_pointer_cast<ButtonDown>(inputev));
        if (button_down_ev) {
            event_data["msg"] = Invokable::asAny(String("button-down"));
            event_data["button"] = Invokable::asAny(keyButtonString(button_down_ev->mButton));
            event_data["keycode"] = Invokable::asAny((int32)button_down_ev->mButton);
            fillModifiers(event_data, button_down_ev->mModifier);
        }
    }

    {
        AxisEventPtr axis_ev (std::tr1::dynamic_pointer_cast<AxisEvent>(inputev));
        if (axis_ev) {
            event_data["msg"] = Invokable::asAny(String("axis"));
            event_data["axis"] = Invokable::asAny((int32)axis_ev->mAxis);
            event_data["value"] = Invokable::asAny(axis_ev->mValue.value);
        }
    }

    {
        TextInputEventPtr text_input_ev (std::tr1::dynamic_pointer_cast<TextInputEvent>(inputev));
        if (text_input_ev) {
            event_data["msg"] = Invokable::asAny(String("text"));
            event_data["value"] = Invokable::asAny(text_input_ev->mText);
        }
    }

    {
        MouseHoverEventPtr mouse_hover_ev (std::tr1::dynamic_pointer_cast<MouseHoverEvent>(inputev));
        if (mouse_hover_ev) {
            event_data["msg"] = Invokable::asAny(String("mouse-hover"));
            float32 x, y;
            bool valid = mParent->mParent->translateToDisplayViewport(mouse_hover_ev->mX, mouse_hover_ev->mY, &x, &y);
            if (!valid) return;
            event_data["x"] = Invokable::asAny(x);
            event_data["y"] = Invokable::asAny(y);
            fillModifiers(event_data, mParent->getCurrentModifiers());
        }
    }

    {
        MousePressedEventPtr mouse_press_ev (std::tr1::dynamic_pointer_cast<MousePressedEvent>(inputev));
        if (mouse_press_ev) {
            event_data["msg"] = Invokable::asAny(String("mouse-press"));
            event_data["button"] = Invokable::asAny((int32)mouse_press_ev->mButton);
            float32 x, y;
            bool valid = mParent->mParent->translateToDisplayViewport(mouse_press_ev->mX, mouse_press_ev->mY, &x, &y);
            if (!valid) return;
            event_data["x"] = Invokable::asAny(x);
            event_data["y"] = Invokable::asAny(y);
            fillModifiers(event_data, mParent->getCurrentModifiers());
        }
    }

    {
        MouseReleasedEventPtr mouse_release_ev (std::tr1::dynamic_pointer_cast<MouseReleasedEvent>(inputev));
        if (mouse_release_ev) {
            event_data["msg"] = Invokable::asAny(String("mouse-release"));
            event_data["button"] = Invokable::asAny((int32)mouse_release_ev->mButton);
            float32 x, y;
            bool valid = mParent->mParent->translateToDisplayViewport(mouse_release_ev->mX, mouse_release_ev->mY, &x, &y);
            if (!valid) return;
            event_data["x"] = Invokable::asAny(x);
            event_data["y"] = Invokable::asAny(y);
            fillModifiers(event_data, mParent->getCurrentModifiers());
        }
    }

    {
        MouseClickEventPtr mouse_click_ev (std::tr1::dynamic_pointer_cast<MouseClickEvent>(inputev));
        if (mouse_click_ev) {
            event_data["msg"] = Invokable::asAny(String("mouse-click"));
            event_data["button"] = Invokable::asAny((int32)mouse_click_ev->mButton);
            float32 x, y;
            bool valid = mParent->mParent->translateToDisplayViewport(mouse_click_ev->mX, mouse_click_ev->mY, &x, &y);
            if (!valid) return;
            event_data["x"] = Invokable::asAny(x);
            event_data["y"] = Invokable::asAny(y);
            fillModifiers(event_data, mParent->getCurrentModifiers());
        }
    }

    {
        MouseDragEventPtr mouse_drag_ev (std::tr1::dynamic_pointer_cast<MouseDragEvent>(inputev));
        if (mouse_drag_ev) {
            event_data["msg"] = Invokable::asAny(String("mouse-drag"));
            event_data["button"] = Invokable::asAny((int32)mouse_drag_ev->mButton);
            float32 x, y;
            bool valid = mParent->mParent->translateToDisplayViewport(mouse_drag_ev->mX, mouse_drag_ev->mY, &x, &y);
            if (!valid) return;
            event_data["x"] = Invokable::asAny(x);
            event_data["y"] = Invokable::asAny(y);
            event_data["dx"] = Invokable::asAny(mouse_drag_ev->deltaX());
            event_data["dy"] = Invokable::asAny(mouse_drag_ev->deltaY());
            fillModifiers(event_data, mParent->getCurrentModifiers());
        }
    }

    {
        DragAndDropEventPtr dd_ev (std::tr1::dynamic_pointer_cast<DragAndDropEvent>(inputev));
        if (dd_ev) {
            event_data["msg"] = Invokable::asAny(String("dragdrop"));
        }
    }

    {
        WebViewEventPtr wv_ev (std::tr1::dynamic_pointer_cast<WebViewEvent>(inputev));
        if (wv_ev) {
            event_data["msg"] = Invokable::asAny((String("webview")));
            event_data["webview"] = Invokable::asAny((wv_ev->webview));
            event_data["name"] = Invokable::asAny((wv_ev->name));
            Invokable::Array wv_args;
            for(uint32 ii = 0; ii < wv_ev->args.size(); ii++)
                wv_args.push_back(wv_ev->args[ii]);
            event_data["args"] = Invokable::asAny(wv_args);
        }
    }

    if (event_data.empty()) return;

    std::vector<boost::any> args;
    args.push_back(Invokable::asAny(event_data));


    for (std::map<Invokable*, Invokable*>::iterator delIter = mDelegates.begin();
         delIter != mDelegates.end(); ++delIter)
    {
        delIter->first->invoke(args);
    }
}


// == OgreSystemInputHandler ==

Vector3f pixelToDirection(Camera *cam, float xPixel, float yPixel) {
    float xRadian, yRadian;
    //pixelToRadians(cam, xPixel/2, yPixel/2, xRadian, yRadian);
    xRadian = sin(cam->getOgreCamera()->getFOVy().valueRadians()*.5) * cam->getOgreCamera()->getAspectRatio() * xPixel;
    yRadian = sin(cam->getOgreCamera()->getFOVy().valueRadians()*.5) * yPixel;

    Quaternion orient = cam->getOrientation();
    return Vector3f(-orient.zAxis()*cos(cam->getOgreCamera()->getFOVy().valueRadians()*.5) +
                    orient.xAxis() * xRadian +
                    orient.yAxis() * yRadian);
}

ProxyEntity* OgreSystemInputHandler::hoverEntity (Camera *cam, Time time, float xPixel, float yPixel, bool mousedown, int *hitCount,int which, Vector3f* hitPointOut, SpaceObjectReference ignore) {
    Vector3d pos = cam->getPosition();
    Vector3f dir (pixelToDirection(cam, xPixel, yPixel));
    SILOG(input,detailed,"OgreSystemInputHandler::hoverEntity: X is "<<xPixel<<"; Y is "<<yPixel<<"; pos = "<<pos<<"; dir = "<<dir);

    double dist;
    Vector3f normal;
    IntersectResult res;
    int subent=-1;
    Ogre::Ray traceFrom(toOgre(pos, mParent->getOffset()), toOgre(dir));
    ProxyEntity *mouseOverEntity = mParent->internalRayTrace(traceFrom, false, *hitCount, dist, normal, subent, &res, mousedown, which, ignore);
    if (mouseOverEntity) {
        if (hitPointOut != NULL) *hitPointOut = Vector3f(pos) + dir.normal()*dist;
        return mouseOverEntity;
    }
    return NULL;
}

bool OgreSystemInputHandler::recentMouseInRange(float x, float y, float *lastX, float *lastY) {
    float delx = x-*lastX;
    float dely = y-*lastY;

    if (delx<0) delx=-delx;
    if (dely<0) dely=-dely;
    if (delx>.03125||dely>.03125) {
        *lastX=x;
        *lastY=y;

        return false;
    }
    return true;
}

SpaceObjectReference OgreSystemInputHandler::pick(Vector2f p, int direction, const SpaceObjectReference& ignore, Vector3f* hitPointOut) {
    if (!mParent||!mParent->mPrimaryCamera) SpaceObjectReference::null();

    Camera *camera = mParent->mPrimaryCamera;
    Time time = mParent->simTime();

    int numObjectsUnderCursor=0;
    ProxyEntity *mouseOver = hoverEntity(camera, time, p.x, p.y, true, &numObjectsUnderCursor, mWhichRayObject, hitPointOut, ignore);
    if (recentMouseInRange(p.x, p.y, &mLastHitX, &mLastHitY)==false||numObjectsUnderCursor!=mLastHitCount)
        mouseOver = hoverEntity(camera, time, p.x, p.y, true, &mLastHitCount, mWhichRayObject=0, hitPointOut, ignore);
    if (mouseOver)
        return mouseOver->getProxyPtr()->getObjectReference();

    return SpaceObjectReference::null();
}

/** Create a UI element using a web view. */
void OgreSystemInputHandler::createUIAction(const String& ui_page)
{
    WebView* ui_wv =
        WebViewManager::getSingleton().createWebView(
            mParent->context(), "__object", "__object", 300,
            300, OverlayPosition(RP_BOTTOMCENTER),
            mParent->renderStrand());

    ui_wv->loadFile(ui_page);
}

inline Vector3f direction(Quaternion cameraAngle) {
    return -cameraAngle.zAxis();
}


///// Top Level Input Event Handlers //////

EventResponse OgreSystemInputHandler::onInputDeviceEvent(InputDeviceEventPtr ev) {
    switch (ev->mType) {
      case InputDeviceEvent::ADDED:
        break;
      case InputDeviceEvent::REMOVED:
        break;
    }
    return EventResponse::nop();
}

EventResponse OgreSystemInputHandler::onKeyPressedEvent(Input::ButtonPressedPtr ev) {
    EventResponse resp = mWebViewInputListener.onKeyPressedEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onKeyPressedEvent(ev);
        return resp;
    }

    mDelegateInputListener.onKeyPressedEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onKeyPressedEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onKeyRepeatedEvent(Input::ButtonRepeatedPtr ev) {
    EventResponse resp = mWebViewInputListener.onKeyRepeatedEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onKeyRepeatedEvent(ev);
        return resp;
    }

    mDelegateInputListener.onKeyRepeatedEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onKeyRepeatedEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onKeyReleasedEvent(Input::ButtonReleasedPtr ev) {
    EventResponse resp = mWebViewInputListener.onKeyReleasedEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onKeyReleasedEvent(ev);
        return resp;
    }

    mDelegateInputListener.onKeyReleasedEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onKeyReleasedEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onKeyDownEvent(Input::ButtonDownPtr ev) {
    EventResponse resp = mWebViewInputListener.onKeyDownEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        return resp;
    }

    mDelegateInputListener.onKeyDownEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onAxisEvent(AxisEventPtr ev) {
    EventResponse resp = mWebViewInputListener.onAxisEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        return resp;
    }

    mDelegateInputListener.onAxisEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onTextInputEvent(TextInputEventPtr ev) {
    EventResponse resp = mWebViewInputListener.onTextInputEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        return resp;
    }

    mDelegateInputListener.onTextInputEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onMouseHoverEvent(MouseHoverEventPtr ev) {
    // Hover doesn't trigger changing of target because of the way it is
    // intended to support hovering over both a webview (transparent) and an
    // object. Since the webview always returns nop(), we have to just dispatch
    // to both.
    mWebViewInputListener.onMouseHoverEvent(ev);
    mDelegateInputListener.onMouseHoverEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onMousePressedEvent(MousePressedEventPtr ev) {
    EventResponse resp = mWebViewInputListener.onMousePressedEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onMousePressedEvent(ev);
        return resp;
    }

    if (mParent->mPrimaryCamera) {
        Camera *camera = mParent->mPrimaryCamera;
        Time time = mParent->simTime();
        int lhc=mLastHitCount;
        hoverEntity(camera, time, ev->mXStart, ev->mYStart, true, &lhc, mWhichRayObject);
    }

    mDelegateInputListener.onMousePressedEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onMousePressedEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onMouseReleasedEvent(MouseReleasedEventPtr ev) {
    EventResponse resp = mWebViewInputListener.onMouseReleasedEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onMouseReleasedEvent(ev);
        return resp;
    }

    if (mParent->mPrimaryCamera) {
        Camera *camera = mParent->mPrimaryCamera;
        Time time = mParent->simTime();
        int lhc=mLastHitCount;
        hoverEntity(camera, time, ev->mXStart, ev->mYStart, true, &lhc, mWhichRayObject);
    }

    mDelegateInputListener.onMouseReleasedEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onMouseReleasedEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onMouseClickEvent(MouseClickEventPtr ev) {
    EventResponse resp = mWebViewInputListener.onMouseClickEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        return resp;
    }

    mDelegateInputListener.onMouseClickEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onMouseDragEvent(MouseDragEventPtr ev) {
    if (!mParent || !mParent->mPrimaryCamera) return EventResponse::nop();

    EventResponse resp = mWebViewInputListener.onMouseDragEvent(ev);
    if (resp == EventResponse::cancel()) {
        mEventCompleter.updateTarget(&mWebViewInputListener);
        mEventCompleter.onMouseDragEvent(ev);
        return resp;
    }

    mDelegateInputListener.onMouseDragEvent(ev);
    mEventCompleter.updateTarget(&mDelegateInputListener);
    mEventCompleter.onMouseDragEvent(ev);
    return EventResponse::cancel();
}

EventResponse OgreSystemInputHandler::onWebViewEvent(WebViewEventPtr webview_ev) {
    // For everything else we let the browser go first, but in this case it should have
    // had its chance, so we just let it go
    mDelegateInputListener.onWebViewEvent(webview_ev);
    return EventResponse::cancel();
}



void OgreSystemInputHandler::fpsUpdateTick(const Task::LocalTime& t) {
    if(mUIWidgetView) {
        Task::DeltaTime dt = t - mLastFpsTime;
        if(dt.toSeconds() > 1) {
            mLastFpsTime = t;
            Ogre::RenderTarget::FrameStats stats = mParent->getRenderTarget()->getStatistics();
            ostringstream os;
            os << stats.avgFPS;
            mUIWidgetView->evaluateJS("update_fps(" + os.str() + ")");
        }
    }
}

void OgreSystemInputHandler::renderStatsUpdateTick(const Task::LocalTime& t) {
    if(mUIWidgetView) {
        Task::DeltaTime dt = t - mLastRenderStatsTime;
        if(dt.toSeconds() > 1) {
            mLastRenderStatsTime = t;
            Ogre::RenderTarget::FrameStats stats = mParent->getRenderTarget()->getStatistics();
            mUIWidgetView->evaluateJS(
                "update_render_stats(" +
                boost::lexical_cast<String>(stats.batchCount) +
                ", " +
                boost::lexical_cast<String>(stats.triangleCount) +
                ")"
            );
        }
    }
}


OgreSystemInputHandler::OgreSystemInputHandler(OgreSystem *parent)
 : mUIWidgetView(NULL),
   mParent(parent),
   mWebViewInputListener(this),
   mDelegateInputListener(this),
   mWhichRayObject(0),
   mLastCameraTime(Task::LocalTime::now()),
   mLastFpsTime(Task::LocalTime::now()),
   mLastRenderStatsTime(Task::LocalTime::now()),
   mUIReady(false)
{
    mLastHitCount=0;
    mLastHitX=0;
    mLastHitY=0;

    mParent->mInputManager->addListener(this);
}

OgreSystemInputHandler::~OgreSystemInputHandler() {

    mParent->mInputManager->removeListener(this);

    if(mUIWidgetView) {
        WebViewManager::getSingleton().destroyWebView(mUIWidgetView);
        mUIWidgetView = NULL;
    }
}

void OgreSystemInputHandler::addDelegate(Invokable* del) {
    mDelegateInputListener.mDelegates[del] = del;
}

void OgreSystemInputHandler::removeDelegate(Invokable* del)
{
    std::map<Invokable*,Invokable*>::iterator delIter = mDelegateInputListener.mDelegates.find(del);
    if (delIter != mDelegateInputListener.mDelegates.end())
        mDelegateInputListener.mDelegates.erase(delIter);
    else
        SILOG(input,error,"Error in OgreSystemInputHandler::removeDelegate.  Attempting to remove delegate that does not exist.");
}

void OgreSystemInputHandler::uiReady() {
    mUIReady = true;
}

Input::Modifier OgreSystemInputHandler::getCurrentModifiers() const {
    Input::Modifier result = MOD_NONE;

    if (mParent->getInputManager()->isModifierDown(Input::MOD_SHIFT))
        result |= MOD_SHIFT;
    if (mParent->getInputManager()->isModifierDown(Input::MOD_CTRL))
        result |= MOD_CTRL;
    if (mParent->getInputManager()->isModifierDown(Input::MOD_ALT))
        result |= MOD_ALT;
    if (mParent->getInputManager()->isModifierDown(Input::MOD_GUI))
        result |= MOD_GUI;

        return result;
}


void OgreSystemInputHandler::alert(const String& title, const String& text) {
    if (!mUIWidgetView) return;

    mUIWidgetView->evaluateJS("alert_permanent('" + title + "', '" + text + "');");
}

boost::any OgreSystemInputHandler::onUIAction(WebView* webview, const JSArguments& args) {
    SILOG(ogre, detailed, "ui action event fired arg length = " << (int)args.size());
    if (args.size() < 1) {
        SILOG(ogre, detailed, "expected at least 1 argument, returning.");
        return boost::any();
    }

    String action_triggered(args[0].data());

    SILOG(ogre, detailed, "UI Action triggered. action = '" << action_triggered << "'.");

    if(action_triggered == "exit") {
        mParent->quit();
    }

    return boost::any();
}

void OgreSystemInputHandler::ensureUI() {
    if(!mUIWidgetView) {
        SILOG(ogre, info, "Creating UI Widget");
        mUIWidgetView = WebViewManager::getSingleton().createWebView(
            mParent->context(),
            "ui_widget","ui_widget",
            mParent->getRenderTarget()->getWidth(), mParent->getRenderTarget()->getHeight(),
            OverlayPosition(RP_TOPLEFT),
            mParent->renderStrand(),
            false,70, TIER_BACK, 0,
            WebView::WebViewBorderSize(0,0,0,0));
        mUIWidgetView->bind("ui-action", std::tr1::bind(&OgreSystemInputHandler::onUIAction, this, _1, _2));
        mUIWidgetView->loadFile("chrome/ui.html");
        mUIWidgetView->setTransparent(true);
    }
}

void OgreSystemInputHandler::windowResized(uint32 w, uint32 h) {
    // Make sure our widget overlay gets scaled appropriately.
    if (mUIWidgetView) {
        mUIWidgetView->resize(w, h);
    }
}

void OgreSystemInputHandler::tick(const Task::LocalTime& t) {
    if (mUIReady) {
        fpsUpdateTick(t);
        renderStatsUpdateTick(t);
    }

    ensureUI();
}

}
}
