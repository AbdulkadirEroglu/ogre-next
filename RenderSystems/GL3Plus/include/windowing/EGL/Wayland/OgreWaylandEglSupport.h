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
#ifndef OGRE_WaylandEglSupport_H
#define OGRE_WaylandEglSupport_H

#include "OgreGL3PlusSupport.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

struct wl_display;

namespace Ogre
{
    class WaylandEglWindow;

    class _OgrePrivate WaylandEglSupport : public GL3PlusSupport
    {
        EGLDisplay mEglDisplay;
        EGLConfig mEglConfig;
        EGLContext mSharedContext;
        wl_display *mWaylandDisplay;

        void addWindowConfig( void );
        void parseVideoMode( const String &videoMode, uint32 &width, uint32 &height ) const;
        void initialiseEglDisplay( wl_display *waylandDisplay );
        void chooseEglConfig();
        void createSharedContext();

    public:
        WaylandEglSupport();
        ~WaylandEglSupport() override;

        void addConfig( void ) override;
        String validateConfig( void ) override;
        void setConfigOption( const String &name, const String &value ) override;

        Window *createWindow( bool autoCreateWindow, GL3PlusRenderSystem *renderSystem,
                              const String &windowTitle ) override;
        Window *newWindow( const String &name, uint32 width, uint32 height, bool fullScreen,
                           const NameValuePairList *miscParams = 0 ) override;

        void start() override;
        void stop() override;
        void *getProcAddress( const char *procname ) const override;

        void initialise( wl_display *waylandDisplay );
        void shutdown();

        EGLDisplay getGLDisplay() const { return mEglDisplay; }
        EGLConfig getGLConfig() const { return mEglConfig; }

        EGLContext createContext( EGLContext shareContext = EGL_NO_CONTEXT );
        void destroyContext( EGLContext context );
        void makeCurrent( EGLSurface surface, EGLContext context ) const;
        void clearCurrent() const;
    };
}  // namespace Ogre

#endif
