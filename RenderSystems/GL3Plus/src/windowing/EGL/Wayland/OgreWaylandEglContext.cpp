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

#include "windowing/EGL/Wayland/OgreWaylandEglContext.h"

#include "OgreGL3PlusRenderSystem.h"
#include "OgreRoot.h"
#include "windowing/EGL/Wayland/OgreWaylandEglSupport.h"

namespace Ogre
{
    WaylandEglContext::WaylandEglContext( WaylandEglSupport *support ) :
        mGLSupport( support ),
        mEglDisplay( EGL_NO_DISPLAY ),
        mEglSurface( EGL_NO_SURFACE ),
        mEglContext( EGL_NO_CONTEXT ),
        mOwnsContext( false )
    {
    }
    //-------------------------------------------------------------------------
    WaylandEglContext::~WaylandEglContext()
    {
        endCurrent();

        if( mOwnsContext && mGLSupport && mEglContext != EGL_NO_CONTEXT )
            mGLSupport->destroyContext( mEglContext );

        if( Root::getSingletonPtr() && Root::getSingletonPtr()->getRenderSystem() )
        {
            GL3PlusRenderSystem *rs = static_cast<GL3PlusRenderSystem *>(
                Root::getSingleton().getRenderSystem() );
            rs->_unregisterContext( this );
        }
    }
    //-------------------------------------------------------------------------
    void WaylandEglContext::setEglHandles( EGLDisplay eglDisplay, EGLSurface eglSurface,
                                           EGLContext eglContext, bool ownsContext )
    {
        mEglDisplay = eglDisplay;
        mEglSurface = eglSurface;
        mEglContext = eglContext;
        mOwnsContext = ownsContext;
    }
    //-------------------------------------------------------------------------
    void WaylandEglContext::setCurrent()
    {
        if( mEglDisplay != EGL_NO_DISPLAY && mEglSurface != EGL_NO_SURFACE &&
            mEglContext != EGL_NO_CONTEXT )
        {
            eglMakeCurrent( mEglDisplay, mEglSurface, mEglSurface, mEglContext );
        }
    }
    //-------------------------------------------------------------------------
    void WaylandEglContext::endCurrent()
    {
        if( mEglDisplay != EGL_NO_DISPLAY )
            eglMakeCurrent( mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
    }
    //-------------------------------------------------------------------------
    GL3PlusContext *WaylandEglContext::clone() const
    {
        WaylandEglContext *retVal = new WaylandEglContext( mGLSupport );
        EGLContext clonedContext = mGLSupport->createContext();
        retVal->setEglHandles( mEglDisplay, mEglSurface, clonedContext, true );
        return retVal;
    }
}  // namespace Ogre
