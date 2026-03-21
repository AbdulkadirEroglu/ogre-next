/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org/

  Copyright (c) 2000-2014 Torus Knot Software Ltd

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
  -----------------------------------------------------------------------------
*/
#ifndef OGRE_WaylandEglWindow_H
#define OGRE_WaylandEglWindow_H

#include "OgreWindow.h"
#include "OgreWaylandEglSupport.h"

#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace Ogre
{
    class WaylandEglContext;
    class WaylandEglSupport;

    class _OgrePrivate WaylandEglWindow : public Window
    {
    protected:
        wl_display      *mWlDisplay      = nullptr;
        wl_registry     *mRegistry       = nullptr;
        wl_compositor   *mCompositor     = nullptr;
        wl_surface      *mWlSurface      = nullptr;
        wl_callback     *mFrameCallback  = nullptr;

        xdg_wm_base     *mXdgWmBase      = nullptr;
        xdg_surface     *mXdgSurface     = nullptr;
        xdg_toplevel    *mXdgToplevel    = nullptr;

        wl_egl_window   *mWlEglWindow    = nullptr;

        // EGL objects
        EGLDisplay       mEglDisplay     = EGL_NO_DISPLAY;
        EGLConfig        mEglConfig      = nullptr;
        EGLContext       mEglContext     = EGL_NO_CONTEXT;
        EGLSurface       mEglSurface     = EGL_NO_SURFACE;
        WaylandEglContext *mContext      = nullptr;

        // state
        String           mTitle;
        uint32           mWidth          = 0;
        uint32           mHeight         = 0;
        bool             mClosed         = false;
        bool             mVisible        = false;
        bool             mHidden         = false;
        bool             mFullscreen     = false;
        bool             mHwGamma        = false;
        bool             mOwnsWlDisplay  = false;
        bool             mOwnsWlSurface  = false;
        bool             mOwnsXdgShellObjects = false;

        bool             mConfigured     = false;
        bool             mResizePending  = false;
        uint32           mPendingWidth   = 0;
        uint32           mPendingHeight  = 0;
        uint32           mLastConfigureSerial = 0;

        Ogre::WaylandEglSupport *mEglSupport    = nullptr;

    public:
        // Wayland lifecycle
        void initWaylandConnection();
        void bindRegistryGlobals();
        void createWaylandSurface();
        void createXdgShellObjects();
        void createEglWindow();
        void createEglSurface();
        void destroyEglSurface();
        void destroyWaylandObjects();

        // Resize / configure handling
        void applyPendingConfigure();
        void recreateSurfaceIfNeeded();
        void acknowledgeConfigure(uint32_t serial);

        // frame pacing / presentation
        void requestFrameCallback();
        void handleFrameDone(uint32_t callbackData);

        // callbacks
        static void registryGlobal(
            void *data, wl_registry *registry, uint32_t name,
            const char *interface, uint32_t version);

        static void registryGlobalRemove(
            void *data, wl_registry *registry, uint32_t name);

        static void xdgWmBasePing(
            void *data, xdg_wm_base *xdgWmBase, uint32_t serial);

        static void xdgSurfaceConfigure(
            void *data, xdg_surface *surface, uint32_t serial);

        static void xdgToplevelConfigure(
            void *data, xdg_toplevel *toplevel, int32_t width, int32_t height,
            wl_array *states);

        static void xdgToplevelClose(
            void *data, xdg_toplevel *toplevel);

        static void frameDone(
            void *data, wl_callback *callback, uint32_t time);


    public:
        WaylandEglWindow( const String &title, uint32 width, uint32 height, bool fullscreenMode,
                          const NameValuePairList *miscParams, WaylandEglSupport *glsupport );
        ~WaylandEglWindow() override;

        void create(const Ogre::NameValuePairList *miscParams);
        void destroy( void ) override;
    
        void reposition( int32 left, int32 top ) override;
        void resize( uint32 width, uint32 height );
        void windowMovedOrResized() override;
        
        void _setVisible( bool visible ) override;
        void setHidden( bool hidden ) override;
        bool isVisible( void ) const override;
        bool isHidden( void ) const override;
        bool isClosed( void ) const override;

        void swapBuffers( void ) override;
        void getCustomAttribute( IdString name, void *pData ) override;

        void _initialize( TextureGpuManager *textureManager ) override;
        void _setFullscreen( bool fullscreen, uint32 width, uint32 height);

        void requestResolution( uint32 width, uint32 height ) override;
        void pollEvents();
        void dispatchPendingEvents();
    };
}  // namespace Ogre

#endif
