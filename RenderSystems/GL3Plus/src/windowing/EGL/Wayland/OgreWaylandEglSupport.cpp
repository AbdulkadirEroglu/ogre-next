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

#include "windowing/EGL/Wayland/OgreWaylandEglSupport.h"

#include "OgreException.h"
#include "OgreGL3PlusRenderSystem.h"
#include "OgreLogManager.h"
#include "OgreString.h"
#include "OgreStringConverter.h"

#include "windowing/EGL/Wayland/OgreWaylandEglWindow.h"

namespace Ogre
{
    namespace
    {
        inline void throwEglError( const char *message, const char *source )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         String( message ) + " EGL error: " +
                             StringConverter::toString( static_cast<int>( eglGetError() ) ),
                         source );
        }
    }  // namespace

    WaylandEglSupport::WaylandEglSupport() :
        mEglDisplay( EGL_NO_DISPLAY ),
        mEglConfig( 0 ),
        mSharedContext( EGL_NO_CONTEXT ),
        mWaylandDisplay( 0 )
    {
    }
    //-------------------------------------------------------------------------
    WaylandEglSupport::~WaylandEglSupport() { shutdown(); }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::addWindowConfig()
    {
        ConfigOption optFullScreen;
        optFullScreen.name = "Full Screen";
        optFullScreen.immutable = false;
        optFullScreen.possibleValues.push_back( "No" );
        optFullScreen.possibleValues.push_back( "Yes" );
        optFullScreen.currentValue = "No";

        ConfigOption optVideoMode;
        optVideoMode.name = "Video Mode";
        optVideoMode.immutable = false;
        optVideoMode.possibleValues.push_back( "1280 x 720" );
        optVideoMode.currentValue = "1280 x 720";

        ConfigOption optFSAA;
        optFSAA.name = "FSAA";
        optFSAA.immutable = false;
        optFSAA.possibleValues.push_back( "1" );
        optFSAA.currentValue = "1";

        ConfigOption optSRGB;
        optSRGB.name = "sRGB Gamma Conversion";
        optSRGB.immutable = false;
        optSRGB.possibleValues.push_back( "Yes" );
        optSRGB.possibleValues.push_back( "No" );
        optSRGB.currentValue = "Yes";

        mOptions[optFullScreen.name] = optFullScreen;
        mOptions[optVideoMode.name] = optVideoMode;
        mOptions[optFSAA.name] = optFSAA;
        mOptions[optSRGB.name] = optSRGB;
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::addConfig( void )
    {
        mOptions.clear();
        addWindowConfig();
    }
    //-------------------------------------------------------------------------
    String WaylandEglSupport::validateConfig( void ) { return BLANKSTRING; }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::setConfigOption( const String &name, const String &value )
    {
        GL3PlusSupport::setConfigOption( name, value );
    }
    //-------------------------------------------------------------------------
    Window *WaylandEglSupport::createWindow( bool autoCreateWindow, GL3PlusRenderSystem *renderSystem,
                                             const String &windowTitle )
    {
        if( !autoCreateWindow )
            return 0;

        NameValuePairList miscParams;
        ConfigOptionMap::iterator opt;
        ConfigOptionMap::iterator end = mOptions.end();

        bool fullscreen = false;
        uint32 width = 1280u;
        uint32 height = 720u;

        if( ( opt = mOptions.find( "FSAA" ) ) != end )
            miscParams["FSAA"] = opt->second.currentValue;

        if( ( opt = mOptions.find( "sRGB Gamma Conversion" ) ) != end )
            miscParams["gamma"] = opt->second.currentValue;

        if( ( opt = mOptions.find( "Full Screen" ) ) != end )
            fullscreen = opt->second.currentValue == "Yes";

        if( ( opt = mOptions.find( "Video Mode" ) ) != end )
            parseVideoMode( opt->second.currentValue, width, height );

        return renderSystem->_createRenderWindow( windowTitle, width, height, fullscreen,
                                                  &miscParams );
    }
    //-------------------------------------------------------------------------
    Window *WaylandEglSupport::newWindow( const String &name, uint32 width, uint32 height,
                                          bool fullscreen, const NameValuePairList *miscParams )
    {
        return new WaylandEglWindow( name, width, height, fullscreen, miscParams, this );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::start()
    {
        LogManager::getSingleton().logMessage(
            "*************************************\n"
            "*** Starting Wayland EGL Subsystem ***\n"
            "*************************************" );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::stop()
    {
        shutdown();
        LogManager::getSingleton().logMessage(
            "*************************************\n"
            "*** Stopping Wayland EGL Subsystem ***\n"
            "*************************************" );
    }
    //-------------------------------------------------------------------------
    void *WaylandEglSupport::getProcAddress( const char *procname ) const
    {
        return reinterpret_cast<void *>( eglGetProcAddress( procname ) );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::parseVideoMode( const String &videoMode, uint32 &width, uint32 &height ) const
    {
        StringVector tokens = StringUtil::split( videoMode, "x" );
        if( tokens.size() != 2u )
            return;

        String widthToken = tokens[0];
        String heightToken = tokens[1];
        StringUtil::trim( widthToken );
        StringUtil::trim( heightToken );

        width = StringConverter::parseUnsignedInt( widthToken );
        height = StringConverter::parseUnsignedInt( heightToken );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::initialiseEglDisplay( wl_display *waylandDisplay )
    {
        if( !waylandDisplay )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "Wayland display must not be null.",
                         "WaylandEglSupport::initialiseEglDisplay" );
        }

#if defined( EGL_VERSION_1_5 ) && defined( EGL_PLATFORM_WAYLAND_KHR )
        mEglDisplay = eglGetPlatformDisplay( EGL_PLATFORM_WAYLAND_KHR, waylandDisplay, 0 );
#else
        mEglDisplay = EGL_NO_DISPLAY;
#endif

        if( mEglDisplay == EGL_NO_DISPLAY )
            mEglDisplay = eglGetDisplay( reinterpret_cast<EGLNativeDisplayType>( waylandDisplay ) );

        if( mEglDisplay == EGL_NO_DISPLAY )
            throwEglError( "Failed to get EGL display for Wayland.",
                           "WaylandEglSupport::initialiseEglDisplay" );

        if( !eglInitialize( mEglDisplay, 0, 0 ) )
            throwEglError( "eglInitialize failed.", "WaylandEglSupport::initialiseEglDisplay" );

        mWaylandDisplay = waylandDisplay;
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::chooseEglConfig()
    {
        const EGLint configAttribs[] = { EGL_SURFACE_TYPE,
                                         EGL_WINDOW_BIT,
                                         EGL_RENDERABLE_TYPE,
                                         EGL_OPENGL_BIT,
                                         EGL_RED_SIZE,
                                         8,
                                         EGL_GREEN_SIZE,
                                         8,
                                         EGL_BLUE_SIZE,
                                         8,
                                         EGL_ALPHA_SIZE,
                                         8,
                                         EGL_DEPTH_SIZE,
                                         24,
                                         EGL_STENCIL_SIZE,
                                         8,
                                         EGL_NONE };

        EGLint numConfigs = 0;
        if( !eglChooseConfig( mEglDisplay, configAttribs, &mEglConfig, 1, &numConfigs ) ||
            numConfigs < 1 )
        {
            throwEglError( "Failed to choose Wayland EGL config.",
                           "WaylandEglSupport::chooseEglConfig" );
        }
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::createSharedContext()
    {
        if( !eglBindAPI( EGL_OPENGL_API ) )
            throwEglError( "Failed to bind EGL OpenGL API.",
                           "WaylandEglSupport::createSharedContext" );

        const EGLint primaryAttrs[] = { EGL_CONTEXT_MAJOR_VERSION,
                                        4,
                                        EGL_CONTEXT_MINOR_VERSION,
                                        5,
#if OGRE_DEBUG_MODE
                                        EGL_CONTEXT_FLAGS_KHR,
                                        EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
#endif
                                        EGL_NONE };
        const EGLint fallbackAttrs[] = { EGL_CONTEXT_MAJOR_VERSION,
                                         3,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         3,
#if OGRE_DEBUG_MODE
                                         EGL_CONTEXT_FLAGS_KHR,
                                         EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
#endif
                                         EGL_NONE };

        mSharedContext = eglCreateContext( mEglDisplay, mEglConfig, EGL_NO_CONTEXT, primaryAttrs );
        if( mSharedContext == EGL_NO_CONTEXT )
            mSharedContext = eglCreateContext( mEglDisplay, mEglConfig, EGL_NO_CONTEXT,
                                               fallbackAttrs );

        if( mSharedContext == EGL_NO_CONTEXT )
            throwEglError( "Failed to create shared Wayland EGL context.",
                           "WaylandEglSupport::createSharedContext" );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::initialise( wl_display *waylandDisplay )
    {
        if( mEglDisplay != EGL_NO_DISPLAY )
        {
            if( mWaylandDisplay != waylandDisplay )
            {
                OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                             "Reinitializing Wayland EGL support with a different wl_display is "
                             "not supported yet.",
                             "WaylandEglSupport::initialise" );
            }
            return;
        }

        initialiseEglDisplay( waylandDisplay );
        chooseEglConfig();
        createSharedContext();
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::shutdown()
    {
        clearCurrent();

        if( mSharedContext != EGL_NO_CONTEXT )
        {
            eglDestroyContext( mEglDisplay, mSharedContext );
            mSharedContext = EGL_NO_CONTEXT;
        }

        if( mEglDisplay != EGL_NO_DISPLAY )
        {
            eglTerminate( mEglDisplay );
            mEglDisplay = EGL_NO_DISPLAY;
        }

        mEglConfig = 0;
        mWaylandDisplay = 0;
    }
    //-------------------------------------------------------------------------
    EGLContext WaylandEglSupport::createContext( EGLContext shareContext )
    {
        if( mEglDisplay == EGL_NO_DISPLAY || !mEglConfig )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Wayland EGL support must be initialized before creating a context.",
                         "WaylandEglSupport::createContext" );
        }

        if( shareContext == EGL_NO_CONTEXT )
            shareContext = mSharedContext;

        const EGLint primaryAttrs[] = { EGL_CONTEXT_MAJOR_VERSION,
                                        4,
                                        EGL_CONTEXT_MINOR_VERSION,
                                        5,
#if OGRE_DEBUG_MODE
                                        EGL_CONTEXT_FLAGS_KHR,
                                        EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
#endif
                                        EGL_NONE };
        const EGLint fallbackAttrs[] = { EGL_CONTEXT_MAJOR_VERSION,
                                         3,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         3,
#if OGRE_DEBUG_MODE
                                         EGL_CONTEXT_FLAGS_KHR,
                                         EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
#endif
                                         EGL_NONE };

        EGLContext context = eglCreateContext( mEglDisplay, mEglConfig, shareContext, primaryAttrs );
        if( context == EGL_NO_CONTEXT )
            context = eglCreateContext( mEglDisplay, mEglConfig, shareContext, fallbackAttrs );

        if( context == EGL_NO_CONTEXT )
            throwEglError( "Failed to create Wayland EGL window context.",
                           "WaylandEglSupport::createContext" );

        return context;
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::destroyContext( EGLContext context )
    {
        if( context != EGL_NO_CONTEXT && context != mSharedContext && mEglDisplay != EGL_NO_DISPLAY )
            eglDestroyContext( mEglDisplay, context );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::makeCurrent( EGLSurface surface, EGLContext context ) const
    {
        if( !eglMakeCurrent( mEglDisplay, surface, surface, context ) )
            throwEglError( "eglMakeCurrent failed.", "WaylandEglSupport::makeCurrent" );
    }
    //-------------------------------------------------------------------------
    void WaylandEglSupport::clearCurrent() const
    {
        if( mEglDisplay != EGL_NO_DISPLAY )
            eglMakeCurrent( mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
    }
}  // namespace Ogre
