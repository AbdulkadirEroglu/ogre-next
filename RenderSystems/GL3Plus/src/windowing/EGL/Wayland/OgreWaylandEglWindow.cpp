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

#include "windowing/EGL/Wayland/OgreWaylandEglWindow.h"
#include "windowing/EGL/Wayland/OgreWaylandEglContext.h"
#include "windowing/EGL/Wayland/OgreWaylandEglSupport.h"

#include "OgreDepthBuffer.h"
#include "OgreException.h"
#include "OgreGL3PlusTextureGpuManager.h"
#include "OgreLogManager.h"
#include "OgrePixelFormatGpuUtils.h"
#include "OgreStringConverter.h"
#include "OgreTextureGpuListener.h"
#include "OgreTextureGpuManager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <vector>

namespace Ogre
{
    namespace
    {
        static const wl_registry_listener cRegistryListener =
        {
            &WaylandEglWindow::registryGlobal,
            &WaylandEglWindow::registryGlobalRemove
        };

        static const xdg_wm_base_listener cXdgWmBaseListener =
        {
            &WaylandEglWindow::xdgWmBasePing
        };

        static const xdg_surface_listener cXdgSurfaceListener =
        {
            &WaylandEglWindow::xdgSurfaceConfigure
        };

        static const xdg_toplevel_listener cXdgToplevelListener =
        {
            &WaylandEglWindow::xdgToplevelConfigure,
            &WaylandEglWindow::xdgToplevelClose
        };

        static const wl_callback_listener cFrameListener =
        {
            &WaylandEglWindow::frameDone
        };

        inline void throwIfNull( const void *ptr, const char *msg, const char *src )
        {
            if( !ptr )
            {
                OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, msg, src );
            }
        }

    }  // namespace

    // ---------------------------------------------------------------------
    WaylandEglWindow::WaylandEglWindow( const String &title, uint32 width, uint32 height,
                                        bool fullscreenMode,
                                        const NameValuePairList *miscParams,
                                        WaylandEglSupport *glsupport ) :
        Window( title, width, height, fullscreenMode ),
        mTitle( title ),
        mWidth( width ),
        mHeight( height ),
        mFullscreen( fullscreenMode ),
        mEglSupport( glsupport )
    {
        create( miscParams );
    }

    // ---------------------------------------------------------------------
    WaylandEglWindow::~WaylandEglWindow()
    {
        destroy();

        OGRE_DELETE mContext;
        mContext = nullptr;

        if( mTexture )
        {
            mTexture->notifyAllListenersTextureChanged( TextureGpuListener::Deleted );
            OGRE_DELETE mTexture;
            mTexture = nullptr;
        }
        if( mDepthBuffer )
        {
            mDepthBuffer->notifyAllListenersTextureChanged( TextureGpuListener::Deleted );
            OGRE_DELETE mDepthBuffer;
            mDepthBuffer = nullptr;
        }
        mStencilBuffer = nullptr;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::_initialize( TextureGpuManager *textureManager )
    {
        GL3PlusTextureGpuManager *glTextureManager =
            static_cast<GL3PlusTextureGpuManager *>( textureManager );

        if( !mContext )
            mContext = OGRE_NEW WaylandEglContext( mEglSupport );

        mContext->setEglHandles( mEglDisplay, mEglSurface, mEglContext, false );

        mTexture = glTextureManager->createTextureGpuWindow( mContext, this );
        mTexture->setPixelFormat( mHwGamma ? PFG_RGBA8_UNORM_SRGB : PFG_RGBA8_UNORM );
        mTexture->setSampleDescription( mRequestedSampleDescription );

        mDepthBuffer = glTextureManager->createTextureGpuWindow( mContext, this );
        mDepthBuffer->setSampleDescription( mRequestedSampleDescription );
        mDepthBuffer->setPixelFormat( DepthBuffer::DefaultDepthBufferFormat );
        if( PixelFormatGpuUtils::isStencil( mDepthBuffer->getPixelFormat() ) )
            mStencilBuffer = mDepthBuffer;

        setFinalResolution( mRequestedWidth, mRequestedHeight );

        if( mDepthBuffer )
        {
            mTexture->_setDepthBufferDefaults( DepthBuffer::POOL_NON_SHAREABLE, false,
                                               mDepthBuffer->getPixelFormat() );
        }
        else
        {
            mTexture->_setDepthBufferDefaults( DepthBuffer::POOL_NO_DEPTH, false, PFG_NULL );
        }

        mSampleDescription = mRequestedSampleDescription;
        mTexture->_transitionTo( GpuResidency::Resident, (uint8 *)0 );
        if( mDepthBuffer )
            mDepthBuffer->_transitionTo( GpuResidency::Resident, (uint8 *)0 );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::create( const Ogre::NameValuePairList *miscParams )
    {
        if( mWlDisplay || mEglSurface != EGL_NO_SURFACE )
            return;

        bool hasExternalDisplay = false;
        bool hasExternalSurface = false;

        if( miscParams )
        {
            NameValuePairList::const_iterator it;

            it = miscParams->find( "title" );
            if( it != miscParams->end() )
                mTitle = it->second;

            it = miscParams->find( "hidden" );
            if( it != miscParams->end() )
                mHidden = StringConverter::parseBool( it->second );

            it = miscParams->find( "fullscreen" );
            if( it != miscParams->end() )
                mFullscreen = StringConverter::parseBool( it->second );

            it = miscParams->find( "width" );
            if( it != miscParams->end() )
                mWidth = StringConverter::parseUnsignedInt( it->second );

            it = miscParams->find( "height" );
            if( it != miscParams->end() )
                mHeight = StringConverter::parseUnsignedInt( it->second );

            it = miscParams->find( "gamma" );
            if( it != miscParams->end() )
                mHwGamma = StringConverter::parseBool( it->second );

            it = miscParams->find( "FSAA" );
            if( it != miscParams->end() )
                mRequestedSampleDescription.parseString( it->second );

            it = miscParams->find( "externalWaylandDisplay" );
            if( it != miscParams->end() )
            {
                mWlDisplay = reinterpret_cast<wl_display *>(StringConverter::parseSizeT( it->second ) );
                hasExternalDisplay = mWlDisplay != nullptr;
            }
            
            it = miscParams->find( "externalWaylandSurface" );
            if( it != miscParams->end() )
            {
                mWlSurface = reinterpret_cast<wl_surface *>(StringConverter::parseSizeT( it->second ) );
                hasExternalSurface = mWlSurface != nullptr;
            }
        }

        if( !hasExternalDisplay && hasExternalSurface )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "externalWaylandSurface requires externalWaylandDisplay.",
                         "WaylandEglWindow::create" );
        }

        if( hasExternalDisplay && hasExternalSurface )
        {
            std::fprintf( stderr,
                          "[OgreWaylandEglWindow] imported external display=%p surface=%p\n",
                          static_cast<void *>( mWlDisplay ),
                          static_cast<void *>( mWlSurface ) );
            LogManager::getSingleton().logMessage(
                "WaylandEglWindow using imported external Wayland display/surface" );
            mOwnsWlDisplay = false;
            mOwnsWlSurface = false;
            mOwnsXdgShellObjects = false;
        }
        else
        {
            if( hasExternalDisplay )
            {
                std::fprintf( stderr,
                              "[OgreWaylandEglWindow] using imported external display=%p "
                              "with self-owned surface\n",
                              static_cast<void *>( mWlDisplay ) );
                LogManager::getSingleton().logMessage(
                    "WaylandEglWindow using imported Wayland display with self-owned surface" );
                mOwnsWlDisplay = false;
            }
            else
            {
                std::fprintf( stderr,
                              "[OgreWaylandEglWindow] creating self-owned toplevel surface\n" );
                LogManager::getSingleton().logMessage(
                    "WaylandEglWindow creating self-owned Wayland surface/toplevel" );
                initWaylandConnection();
            }
            bindRegistryGlobals();
            createWaylandSurface();
            createXdgShellObjects();
        }

        mEglSupport->initialise( mWlDisplay );
        mEglDisplay = mEglSupport->getGLDisplay();
        mEglConfig = mEglSupport->getGLConfig();
        createEglWindow();
        createEglSurface();
        mEglContext = mEglSupport->createContext();
        mEglSupport->makeCurrent( mEglSurface, mEglContext );

        EGLint contextMajor = 0;
        EGLint contextMinor = 0;
        eglQueryContext( mEglDisplay, mEglContext, EGL_CONTEXT_MAJOR_VERSION, &contextMajor );
        eglQueryContext( mEglDisplay, mEglContext, EGL_CONTEXT_MINOR_VERSION, &contextMinor );
        std::fprintf( stderr,
                      "[OgreWaylandEglWindow] current EGL context version=%d.%d display=%p "
                      "surface=%p context=%p\n",
                      static_cast<int>( contextMajor ), static_cast<int>( contextMinor ),
                      static_cast<void *>( mEglDisplay ), static_cast<void *>( mEglSurface ),
                      static_cast<void *>( mEglContext ) );

        if( !mContext )
            mContext = OGRE_NEW WaylandEglContext( mEglSupport );
        mContext->setEglHandles( mEglDisplay, mEglSurface, mEglContext, false );

        mVisible = !mHidden;
        mClosed = false;
        setFinalResolution( mRequestedWidth, mRequestedHeight );

        wl_surface_commit( mWlSurface );
        if( mOwnsXdgShellObjects )
            wl_display_roundtrip( mWlDisplay );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::destroy()
    {
        if( mClosed && !mWlDisplay && mEglSurface == EGL_NO_SURFACE )
            return;

        if( mContext )
            mContext->endCurrent();
        else
            mEglSupport->clearCurrent();
        destroyEglSurface();
        destroyWaylandObjects();

        if( mEglContext != EGL_NO_CONTEXT )
        {
            mEglSupport->destroyContext( mEglContext );
            mEglContext = EGL_NO_CONTEXT;
        }

        if( mContext )
            mContext->setEglHandles( EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_CONTEXT, false );

        mEglDisplay = EGL_NO_DISPLAY;
        mEglConfig = 0;
        mClosed = true;
        mVisible = false;
        mConfigured = false;
        mResizePending = false;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::initWaylandConnection()
    {
        mWlDisplay = wl_display_connect( nullptr );
        mOwnsWlDisplay = mWlDisplay != nullptr;
        throwIfNull( mWlDisplay, "Failed to connect to Wayland display", "WaylandEglWindow::initWaylandConnection" );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::bindRegistryGlobals()
    {
        mRegistry = wl_display_get_registry( mWlDisplay );
        throwIfNull( mRegistry, "Failed to get Wayland registry", "WaylandEglWindow::bindRegistryGlobals" );

        if( wl_registry_add_listener( mRegistry, &cRegistryListener, this ) != 0 )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to add Wayland registry listener",
                         "WaylandEglWindow::bindRegistryGlobals" );
        }

        // First roundtrip: get globals
        wl_display_roundtrip( mWlDisplay );

        throwIfNull( mCompositor, "Wayland compositor global not found", "WaylandEglWindow::bindRegistryGlobals" );
        throwIfNull( mXdgWmBase, "xdg_wm_base global not found", "WaylandEglWindow::bindRegistryGlobals" );

        if( xdg_wm_base_add_listener( mXdgWmBase, &cXdgWmBaseListener, this ) != 0 )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to add xdg_wm_base listener",
                         "WaylandEglWindow::bindRegistryGlobals" );
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::createWaylandSurface()
    {
        mWlSurface = wl_compositor_create_surface( mCompositor );
        mOwnsWlSurface = mWlSurface != nullptr;
        throwIfNull( mWlSurface, "Failed to create wl_surface", "WaylandEglWindow::createWaylandSurface" );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::createXdgShellObjects()
    {
        mXdgSurface = xdg_wm_base_get_xdg_surface( mXdgWmBase, mWlSurface );
        throwIfNull( mXdgSurface, "Failed to create xdg_surface", "WaylandEglWindow::createXdgShellObjects" );
        mOwnsXdgShellObjects = true;

        if( xdg_surface_add_listener( mXdgSurface, &cXdgSurfaceListener, this ) != 0 )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to add xdg_surface listener",
                         "WaylandEglWindow::createXdgShellObjects" );
        }

        mXdgToplevel = xdg_surface_get_toplevel( mXdgSurface );
        throwIfNull( mXdgToplevel, "Failed to create xdg_toplevel", "WaylandEglWindow::createXdgShellObjects" );

        if( xdg_toplevel_add_listener( mXdgToplevel, &cXdgToplevelListener, this ) != 0 )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to add xdg_toplevel listener",
                         "WaylandEglWindow::createXdgShellObjects" );
        }

        if( !mTitle.empty() )
            xdg_toplevel_set_title( mXdgToplevel, mTitle.c_str() );

        if( mFullscreen )
            xdg_toplevel_set_fullscreen( mXdgToplevel, nullptr );

        // Initial commit required to receive first configure
        wl_surface_commit( mWlSurface );
        wl_display_roundtrip( mWlDisplay );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::createEglWindow()
    {
        mWlEglWindow = wl_egl_window_create( mWlSurface, static_cast<int>( mWidth ),
                                             static_cast<int>( mHeight ) );
        throwIfNull( mWlEglWindow, "Failed to create wl_egl_window", "WaylandEglWindow::createEglWindow" );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::createEglSurface()
    {
#if defined(EGL_VERSION_1_5)
        mEglSurface = eglCreatePlatformWindowSurface( mEglDisplay, mEglConfig, mWlEglWindow, nullptr );
#else
        mEglSurface = eglCreateWindowSurface(
            mEglDisplay, mEglConfig,
            reinterpret_cast<EGLNativeWindowType>( mWlEglWindow ), nullptr );
#endif

        if( mEglSurface == EGL_NO_SURFACE )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "Failed to create EGL surface. EGL error: " +
                             StringConverter::toString( static_cast<int>( eglGetError() ) ),
                         "WaylandEglWindow::createEglSurface" );
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::destroyEglSurface()
    {
        if( mEglSurface != EGL_NO_SURFACE )
        {
            eglDestroySurface( mEglDisplay, mEglSurface );
            mEglSurface = EGL_NO_SURFACE;
        }

        if( mWlEglWindow )
        {
            wl_egl_window_destroy( mWlEglWindow );
            mWlEglWindow = nullptr;
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::destroyWaylandObjects()
    {
        if( mFrameCallback )
        {
            wl_callback_destroy( mFrameCallback );
            mFrameCallback = nullptr;
        }

        if( mOwnsXdgShellObjects && mXdgToplevel )
        {
            xdg_toplevel_destroy( mXdgToplevel );
            mXdgToplevel = nullptr;
        }

        if( mOwnsXdgShellObjects && mXdgSurface )
        {
            xdg_surface_destroy( mXdgSurface );
            mXdgSurface = nullptr;
        }

        if( mOwnsWlSurface && mWlSurface )
        {
            wl_surface_destroy( mWlSurface );
            mWlSurface = nullptr;
        }

        if( mXdgWmBase )
        {
            xdg_wm_base_destroy( mXdgWmBase );
            mXdgWmBase = nullptr;
        }

        if( mCompositor )
        {
            wl_compositor_destroy( mCompositor );
            mCompositor = nullptr;
        }

        if( mRegistry )
        {
            wl_registry_destroy( mRegistry );
            mRegistry = nullptr;
        }

        if( mOwnsWlDisplay && mWlDisplay )
        {
            wl_display_disconnect( mWlDisplay );
            mWlDisplay = nullptr;
        }

        if( !mOwnsWlSurface )
            mWlSurface = nullptr;
        if( !mOwnsWlDisplay )
            mWlDisplay = nullptr;

        mOwnsXdgShellObjects = false;
        mOwnsWlSurface = false;
        mOwnsWlDisplay = false;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::applyPendingConfigure()
    {
        if( !mResizePending )
            return;

        Window::requestResolution( mPendingWidth, mPendingHeight );
        mWidth = mPendingWidth;
        mHeight = mPendingHeight;

        if( mWlEglWindow )
        {
            wl_egl_window_resize( mWlEglWindow,
                                  static_cast<int>( mWidth ),
                                  static_cast<int>( mHeight ),
                                  0, 0 );
        }

        setFinalResolution( mWidth, mHeight );

        mResizePending = false;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::recreateSurfaceIfNeeded()
    {
        if( mEglSurface == EGL_NO_SURFACE || !mWlEglWindow )
            return;

        applyPendingConfigure();
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::acknowledgeConfigure( uint32_t serial )
    {
        mLastConfigureSerial = serial;

        if( mXdgSurface )
            xdg_surface_ack_configure( mXdgSurface, serial );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::requestFrameCallback()
    {
        if( !mWlSurface || mFrameCallback )
            return;

        mFrameCallback = wl_surface_frame( mWlSurface );
        if( !mFrameCallback )
            return;

        wl_callback_add_listener( mFrameCallback, &cFrameListener, this );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::handleFrameDone( uint32_t callbackData )
    {
        OGRE_UNUSED( callbackData );
        // Hook point for later pacing logic.
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::reposition( int32 left, int32 top )
    {
        OGRE_UNUSED( left );
        OGRE_UNUSED( top );
        // Wayland toplevel windows do not support client-driven absolute positioning.
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::resize( uint32 width, uint32 height )
    {
        if( mClosed || width == 0u || height == 0u )
            return;

        Window::requestResolution( width, height );
        mWidth = width;
        mHeight = height;

        if( mWlEglWindow )
        {
            wl_egl_window_resize( mWlEglWindow, static_cast<int>( width ),
                                  static_cast<int>( height ), 0, 0 );
        }

        setFinalResolution( width, height );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::requestResolution( uint32 width, uint32 height )
    {
        if( mClosed || width == 0u || height == 0u )
            return;

        if( mTexture && mTexture->getWidth() == width && mTexture->getHeight() == height )
            return;

        Window::requestResolution( width, height );

        if( !mOwnsXdgShellObjects )
        {
            resize( width, height );
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::windowMovedOrResized()
    {
        if( mClosed )
            return;

        recreateSurfaceIfNeeded();

        if( mWlEglWindow )
            setFinalResolution( mWidth, mHeight );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::_setVisible( bool visible )
    {
        mVisible = visible;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::setHidden( bool hidden )
    {
        mHidden = hidden;
        mVisible = !hidden;
    }

    // ---------------------------------------------------------------------
    bool WaylandEglWindow::isVisible( void ) const
    {
        return mVisible && !mHidden && !mClosed;
    }

    // ---------------------------------------------------------------------
    bool WaylandEglWindow::isHidden( void ) const
    {
        return mHidden;
    }

    // ---------------------------------------------------------------------
    bool WaylandEglWindow::isClosed( void ) const
    {
        return mClosed;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::_setFullscreen( bool fullscreen, uint32 width, uint32 height )
    {
        mFullscreen = fullscreen;

        if( width && height )
        {
            mWidth = width;
            mHeight = height;
        }

        if( mXdgToplevel )
        {
            if( fullscreen )
                xdg_toplevel_set_fullscreen( mXdgToplevel, nullptr );
            else
                xdg_toplevel_unset_fullscreen( mXdgToplevel );

            wl_surface_commit( mWlSurface );
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::swapBuffers( void )
    {
        if( mClosed || mEglSurface == EGL_NO_SURFACE )
            return;

        recreateSurfaceIfNeeded();
        requestFrameCallback();

        if( !eglSwapBuffers( mEglDisplay, mEglSurface ) )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "eglSwapBuffers failed. EGL error: " +
                             StringConverter::toString( static_cast<int>( eglGetError() ) ),
                         "WaylandEglWindow::swapBuffers" );
        }

        if( mWlSurface )
            wl_surface_commit( mWlSurface );

        dispatchPendingEvents();
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::pollEvents()
    {
        if( !mWlDisplay )
            return;

        if( wl_display_dispatch_pending( mWlDisplay ) < 0 )
            return;

        if( wl_display_flush( mWlDisplay ) < 0 )
            return;

        while( wl_display_prepare_read( mWlDisplay ) != 0 )
            wl_display_dispatch_pending( mWlDisplay );

        pollfd displayFd;
        displayFd.fd = wl_display_get_fd( mWlDisplay );
        displayFd.events = POLLIN;
        displayFd.revents = 0;

        const int pollResult = poll( &displayFd, 1, 0 );
        if( pollResult < 0 )
        {
            wl_display_cancel_read( mWlDisplay );
            return;
        }

        if( pollResult == 0 )
        {
            wl_display_cancel_read( mWlDisplay );
            return;
        }

        if( displayFd.revents & POLLIN )
        {
            if( wl_display_read_events( mWlDisplay ) < 0 )
                return;

            wl_display_dispatch_pending( mWlDisplay );
            return;
        }

        wl_display_cancel_read( mWlDisplay );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::dispatchPendingEvents()
    {
        pollEvents();
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::getCustomAttribute( IdString name, void *pData )
    {
        // Replace these comparisons with your branch's preferred IdString helpers if needed.
        if( name == IdString( "WAYLAND_DISPLAY" ) )
            *static_cast<wl_display **>( pData ) = mWlDisplay;
        else if( name == IdString( "GLCONTEXT" ) )
            *static_cast<GL3PlusContext **>( pData ) = mContext;
        else if( name == IdString( "WAYLAND_SURFACE" ) )
            *static_cast<wl_surface **>( pData ) = mWlSurface;
        else if( name == IdString( "WAYLAND_EGL_WINDOW" ) )
            *static_cast<wl_egl_window **>( pData ) = mWlEglWindow;
        else if( name == IdString( "XDG_TOPLEVEL" ) )
            *static_cast<xdg_toplevel **>( pData ) = mXdgToplevel;
        else if( name == IdString( "EGLDISPLAY" ) )
            *static_cast<EGLDisplay *>( pData ) = mEglDisplay;
        else if( name == IdString( "EGLCONTEXT" ) )
            *static_cast<EGLContext *>( pData ) = mEglContext;
        else if( name == IdString( "EGLSURFACE" ) )
            *static_cast<EGLSurface *>( pData ) = mEglSurface;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::registryGlobal( void *data, wl_registry *registry, uint32_t name,
                                           const char *interface, uint32_t version )
    {
        WaylandEglWindow *self = static_cast<WaylandEglWindow *>( data );

        if( std::strcmp( interface, wl_compositor_interface.name ) == 0 )
        {
            self->mCompositor = static_cast<wl_compositor *>(
                wl_registry_bind( registry, name, &wl_compositor_interface,
                                  std::min<uint32_t>( version, 4u ) ) );
        }
        else if( std::strcmp( interface, xdg_wm_base_interface.name ) == 0 )
        {
            self->mXdgWmBase = static_cast<xdg_wm_base *>(
                wl_registry_bind( registry, name, &xdg_wm_base_interface,
                                  std::min<uint32_t>( version, 2u ) ) );
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::registryGlobalRemove( void *data, wl_registry *registry, uint32_t name )
    {
        OGRE_UNUSED( data );
        OGRE_UNUSED( registry );
        OGRE_UNUSED( name );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::xdgWmBasePing( void *data, xdg_wm_base *xdgWmBase, uint32_t serial )
    {
        OGRE_UNUSED( data );
        xdg_wm_base_pong( xdgWmBase, serial );
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::xdgSurfaceConfigure( void *data, xdg_surface *surface, uint32_t serial )
    {
        WaylandEglWindow *self = static_cast<WaylandEglWindow *>( data );
        OGRE_UNUSED( surface );

        self->acknowledgeConfigure( serial );
        self->applyPendingConfigure();
        self->mConfigured = true;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::xdgToplevelConfigure( void *data, xdg_toplevel *toplevel,
                                                 int32_t width, int32_t height,
                                                 wl_array *states )
    {
        WaylandEglWindow *self = static_cast<WaylandEglWindow *>( data );
        OGRE_UNUSED( toplevel );
        OGRE_UNUSED( states );

        if( width > 0 && height > 0 )
        {
            self->mPendingWidth = static_cast<uint32>( width );
            self->mPendingHeight = static_cast<uint32>( height );
            self->mResizePending = true;
        }
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::xdgToplevelClose( void *data, xdg_toplevel *toplevel )
    {
        WaylandEglWindow *self = static_cast<WaylandEglWindow *>( data );
        OGRE_UNUSED( toplevel );

        self->mClosed = true;
        self->mVisible = false;
    }

    // ---------------------------------------------------------------------
    void WaylandEglWindow::frameDone( void *data, wl_callback *callback, uint32_t time )
    {
        WaylandEglWindow *self = static_cast<WaylandEglWindow *>( data );

        if( callback )
            wl_callback_destroy( callback );

        self->mFrameCallback = nullptr;
        self->handleFrameDone( time );
    }

}  // namespace Ogre
