/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  -----------------------------------------------------------------------------
*/

#include "OgreException.h"
#include "OgreGL3PlusContext.h"
#include "OgreGL3PlusPlugin.h"
#include "OgreLogManager.h"
#include "OgreRoot.h"
#include "OgreStringConverter.h"
#include "OgreWindow.h"

#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace
{
    volatile std::sig_atomic_t gSignalCaught = 0;

    void logWaylandEvent( const char *message )
    {
        std::cerr << "[WaylandSmoke] " << message << '\n';
    }

    void signalHandler( int signalNumber )
    {
        gSignalCaught = signalNumber;

        const char *message = "[WaylandSmoke] signal received\n";
        switch( signalNumber )
        {
        case SIGINT:
            message = "[WaylandSmoke] signal received: SIGINT\n";
            break;
        case SIGTERM:
            message = "[WaylandSmoke] signal received: SIGTERM\n";
            break;
        case SIGQUIT:
            message = "[WaylandSmoke] signal received: SIGQUIT\n";
            break;
        default:
            break;
        }

        ::write( STDERR_FILENO, message, std::strlen( message ) );
    }

    void installSignalHandlers()
    {
        std::signal( SIGINT, &signalHandler );
        std::signal( SIGTERM, &signalHandler );
        std::signal( SIGQUIT, &signalHandler );
    }

    struct WaylandHostState
    {
        wl_display *display = nullptr;
        wl_registry *registry = nullptr;
        wl_compositor *compositor = nullptr;
        xdg_wm_base *wmBase = nullptr;
        wl_surface *surface = nullptr;
        xdg_surface *xdgSurface = nullptr;
        xdg_toplevel *xdgToplevel = nullptr;
        bool configured = false;
        bool closed = false;
        uint32_t width = 1280u;
        uint32_t height = 720u;
    };

    typedef void ( *PfnGlClearColor )( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha );
    typedef void ( *PfnGlClear )( GLbitfield mask );
    typedef void ( *PfnGlViewport )( GLint x, GLint y, GLsizei width, GLsizei height );

    struct GlProcedures
    {
        PfnGlClearColor clearColor = nullptr;
        PfnGlClear clear = nullptr;
        PfnGlViewport viewport = nullptr;
    };

    void registryGlobal( void *data, wl_registry *registry, uint32_t name, const char *interface,
                         uint32_t version )
    {
        WaylandHostState *state = static_cast<WaylandHostState *>( data );
        (void)state;

        std::cerr << "[WaylandSmoke] registry global: " << interface << " name=" << name
                  << " version=" << version << '\n';

        if( std::strcmp( interface, wl_compositor_interface.name ) == 0 )
        {
            state->compositor = static_cast<wl_compositor *>(
                wl_registry_bind( registry, name, &wl_compositor_interface,
                                  std::min<uint32_t>( version, 4u ) ) );
        }
        else if( std::strcmp( interface, xdg_wm_base_interface.name ) == 0 )
        {
            state->wmBase = static_cast<xdg_wm_base *>(
                wl_registry_bind( registry, name, &xdg_wm_base_interface,
                                  std::min<uint32_t>( version, 2u ) ) );
        }
    }

    void registryGlobalRemove( void *data, wl_registry *registry, uint32_t name )
    {
        (void)data;
        (void)registry;
        (void)name;
    }

    void xdgWmBasePing( void *data, xdg_wm_base *wmBase, uint32_t serial )
    {
        (void)data;
        std::cerr << "[WaylandSmoke] xdg_wm_base ping serial=" << serial << '\n';
        xdg_wm_base_pong( wmBase, serial );
    }

    void xdgSurfaceConfigure( void *data, xdg_surface *surface, uint32_t serial )
    {
        WaylandHostState *state = static_cast<WaylandHostState *>( data );
        std::cerr << "[WaylandSmoke] xdg_surface configure serial=" << serial << '\n';
        xdg_surface_ack_configure( surface, serial );
        state->configured = true;
    }

    const char *xdgToplevelStateName( uint32_t state )
    {
        switch( state )
        {
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            return "maximized";
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            return "fullscreen";
        case XDG_TOPLEVEL_STATE_RESIZING:
            return "resizing";
        case XDG_TOPLEVEL_STATE_ACTIVATED:
            return "activated";
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
            return "tiled_left";
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
            return "tiled_right";
        case XDG_TOPLEVEL_STATE_TILED_TOP:
            return "tiled_top";
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
            return "tiled_bottom";
        case XDG_TOPLEVEL_STATE_SUSPENDED:
            return "suspended";
        default:
            return "unknown";
        }
    }

    void xdgToplevelConfigure( void *data, xdg_toplevel *toplevel, int32_t width, int32_t height,
                               wl_array *states )
    {
        WaylandHostState *state = static_cast<WaylandHostState *>( data );
        (void)toplevel;

        std::string stateSummary;
        if( states )
        {
            uint32_t *entry = static_cast<uint32_t *>( states->data );
            const size_t count = states->size / sizeof( uint32_t );
            for( size_t i = 0u; i < count; ++i )
            {
                if( !stateSummary.empty() )
                    stateSummary += ",";
                stateSummary += xdgToplevelStateName( entry[i] );
            }
        }
        if( stateSummary.empty() )
            stateSummary = "none";

        std::cerr << "[WaylandSmoke] xdg_toplevel configure width=" << width
                  << " height=" << height << " states=" << stateSummary << '\n';

        if( width > 0 )
            state->width = static_cast<uint32_t>( width );
        if( height > 0 )
            state->height = static_cast<uint32_t>( height );
    }

    void xdgToplevelClose( void *data, xdg_toplevel *toplevel )
    {
        WaylandHostState *state = static_cast<WaylandHostState *>( data );
        (void)toplevel;
        logWaylandEvent( "xdg_toplevel close" );
        state->closed = true;
    }

    const wl_registry_listener cRegistryListener = { &registryGlobal, &registryGlobalRemove };
    const xdg_wm_base_listener cWmBaseListener = { &xdgWmBasePing };
    const xdg_surface_listener cXdgSurfaceListener = { &xdgSurfaceConfigure };
    const xdg_toplevel_listener cToplevelListener = { &xdgToplevelConfigure, &xdgToplevelClose };

    void checkWayland( bool condition, const char *message )
    {
        if( !condition )
            throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR, message,
                                   "WaylandEglSmokeTest" );
    }

    WaylandHostState createHostWindow( const char *title, uint32_t width, uint32_t height )
    {
        WaylandHostState state;
        state.width = width;
        state.height = height;

        logWaylandEvent( "creating Wayland host window" );

        state.display = wl_display_connect( nullptr );
        checkWayland( state.display != nullptr, "Failed to connect to Wayland display" );

        state.registry = wl_display_get_registry( state.display );
        checkWayland( state.registry != nullptr, "Failed to get Wayland registry" );
        wl_registry_add_listener( state.registry, &cRegistryListener, &state );
        wl_display_roundtrip( state.display );

        checkWayland( state.compositor != nullptr, "Wayland compositor global not found" );
        checkWayland( state.wmBase != nullptr, "xdg_wm_base global not found" );

        xdg_wm_base_add_listener( state.wmBase, &cWmBaseListener, &state );

        state.surface = wl_compositor_create_surface( state.compositor );
        checkWayland( state.surface != nullptr, "Failed to create wl_surface" );

        state.xdgSurface = xdg_wm_base_get_xdg_surface( state.wmBase, state.surface );
        checkWayland( state.xdgSurface != nullptr, "Failed to create xdg_surface" );
        xdg_surface_add_listener( state.xdgSurface, &cXdgSurfaceListener, &state );

        state.xdgToplevel = xdg_surface_get_toplevel( state.xdgSurface );
        checkWayland( state.xdgToplevel != nullptr, "Failed to create xdg_toplevel" );
        xdg_toplevel_add_listener( state.xdgToplevel, &cToplevelListener, &state );
        xdg_toplevel_set_title( state.xdgToplevel, title );
        xdg_toplevel_set_app_id( state.xdgToplevel, "org.ogre3d.WaylandEglSmoke" );

        wl_surface_commit( state.surface );
        wl_display_roundtrip( state.display );
        wl_display_roundtrip( state.display );

        checkWayland( state.configured, "Wayland toplevel did not receive initial configure" );
        logWaylandEvent( "Wayland host window configured" );
        return state;
    }

    void pumpWaylandEvents( WaylandHostState &state )
    {
        if( !state.display )
            return;

        if( wl_display_dispatch_pending( state.display ) < 0 )
        {
            throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                                   "wl_display_dispatch_pending failed",
                                   "WaylandEglSmokeTest" );
        }

        if( wl_display_flush( state.display ) < 0 )
        {
            throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                                   "wl_display_flush failed", "WaylandEglSmokeTest" );
        }

        pollfd displayFd;
        displayFd.fd = wl_display_get_fd( state.display );
        displayFd.events = POLLIN;
        displayFd.revents = 0;

        const int pollResult = poll( &displayFd, 1, 0 );
        if( pollResult > 0 )
        {
            std::cerr << "[WaylandSmoke] poll revents=0x" << std::hex << displayFd.revents
                      << std::dec << '\n';

            if( displayFd.revents & ( POLLERR | POLLHUP | POLLNVAL ) )
            {
                throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                                       "Wayland display fd reported an error",
                                       "WaylandEglSmokeTest" );
            }

            if( displayFd.revents & POLLIN )
            {
                if( wl_display_dispatch( state.display ) < 0 )
                {
                    throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                                           "wl_display_dispatch failed",
                                           "WaylandEglSmokeTest" );
                }
            }
        }
        else if( pollResult < 0 )
        {
            throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR, "poll failed",
                                   "WaylandEglSmokeTest" );
        }
    }

    GlProcedures loadGlProcedures()
    {
        GlProcedures gl;
        gl.clearColor =
            reinterpret_cast<PfnGlClearColor>( eglGetProcAddress( "glClearColor" ) );
        gl.clear = reinterpret_cast<PfnGlClear>( eglGetProcAddress( "glClear" ) );
        gl.viewport = reinterpret_cast<PfnGlViewport>( eglGetProcAddress( "glViewport" ) );

        checkWayland( gl.clearColor != nullptr, "Failed to load glClearColor" );
        checkWayland( gl.clear != nullptr, "Failed to load glClear" );
        checkWayland( gl.viewport != nullptr, "Failed to load glViewport" );
        return gl;
    }

    void destroyHostWindow( WaylandHostState &state )
    {
        if( state.xdgToplevel )
        {
            xdg_toplevel_destroy( state.xdgToplevel );
            state.xdgToplevel = nullptr;
        }
        if( state.xdgSurface )
        {
            xdg_surface_destroy( state.xdgSurface );
            state.xdgSurface = nullptr;
        }
        if( state.surface )
        {
            wl_surface_destroy( state.surface );
            state.surface = nullptr;
        }
        if( state.wmBase )
        {
            xdg_wm_base_destroy( state.wmBase );
            state.wmBase = nullptr;
        }
        if( state.compositor )
        {
            wl_compositor_destroy( state.compositor );
            state.compositor = nullptr;
        }
        if( state.registry )
        {
            wl_registry_destroy( state.registry );
            state.registry = nullptr;
        }
        if( state.display )
        {
            wl_display_disconnect( state.display );
            state.display = nullptr;
        }
    }
}  // namespace

int main( int argc, char **argv )
{
    const int maxFrames = argc > 1 ? std::max( 1, std::atoi( argv[1] ) ) : 120;
    const bool verboseOgreLogging = std::getenv( "OGRE_WAYLAND_SMOKE_OGRE_DEBUG" ) != nullptr;
    WaylandHostState hostState;

    try
    {
        installSignalHandlers();
        hostState = createHostWindow( "Ogre Wayland EGL Smoke Test", 1280u, 720u );

        Ogre::LogManager logManager;
        Ogre::Log *defaultLog =
            logManager.createLog( "WaylandEglSmoke.log", true, verboseOgreLogging, false );
        defaultLog->setDebugOutputEnabled( verboseOgreLogging );
        logManager.setLogDetail( verboseOgreLogging ? Ogre::LL_NORMAL : Ogre::LL_LOW );

        Ogre::Root root( "", "wayland-egl-smoke.cfg", "WaylandEglSmoke.log" );
        Ogre::GL3PlusPlugin plugin;
        root.installPlugin( &plugin, nullptr );

        Ogre::RenderSystem *renderSystem =
            root.getRenderSystemByName( "OpenGL 3+ Rendering Subsystem" );
        checkWayland( renderSystem != nullptr, "OpenGL 3+ Rendering Subsystem not found" );

        root.setRenderSystem( renderSystem );
        renderSystem->setConfigOption( "Interface", "Wayland EGL Window" );
        renderSystem->setConfigOption( "sRGB Gamma Conversion", "No" );
        root.initialise( false );

        Ogre::NameValuePairList miscParams;
        miscParams["title"] = "Ogre Wayland EGL Smoke Test";
        miscParams["externalWaylandDisplay"] =
            Ogre::StringConverter::toString( reinterpret_cast<size_t>( hostState.display ) );
        miscParams["externalWaylandSurface"] =
            Ogre::StringConverter::toString( reinterpret_cast<size_t>( hostState.surface ) );

        Ogre::Window *window = root.createRenderWindow( "WaylandEglSmoke", hostState.width,
                                                        hostState.height, false, &miscParams );
        checkWayland( window != nullptr, "Failed to create Ogre render window" );

        Ogre::GL3PlusContext *context = nullptr;
        window->getCustomAttribute( "GLCONTEXT", &context );
        checkWayland( context != nullptr, "Failed to obtain GL3Plus context from render window" );

        const GlProcedures gl = loadGlProcedures();
        const auto startTime = std::chrono::steady_clock::now();
        auto fpsWindowStart = startTime;
        auto lastResizeTime = startTime - std::chrono::seconds( 1 );
        uint32_t appliedWidth = hostState.width;
        uint32_t appliedHeight = hostState.height;
        int fpsFrameCount = 0;

        for( int frame = 0; frame < maxFrames && !hostState.closed && !gSignalCaught; ++frame )
        {
            pumpWaylandEvents( hostState );

            if( hostState.width != appliedWidth || hostState.height != appliedHeight )
            {
                std::cerr << "[WaylandSmoke] applying imported resize " << hostState.width << "x"
                          << hostState.height << '\n';
                window->requestResolution( hostState.width, hostState.height );
                appliedWidth = hostState.width;
                appliedHeight = hostState.height;
                lastResizeTime = std::chrono::steady_clock::now();
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime );
            const float t = static_cast<float>( elapsed.count() ) * 0.001f;

            context->setCurrent();
            gl.viewport( 0, 0, static_cast<GLsizei>( hostState.width ),
                         static_cast<GLsizei>( hostState.height ) );
            gl.clearColor( 0.5f + 0.5f * std::sin( t ),
                           0.5f + 0.5f * std::sin( t + 2.0943951f ),
                           0.5f + 0.5f * std::sin( t + 4.1887902f ),
                           1.0f );
            gl.clear( GL_COLOR_BUFFER_BIT );
            window->swapBuffers();
            context->endCurrent();
            ++fpsFrameCount;

            const auto now = std::chrono::steady_clock::now();
            const auto fpsWindowElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - fpsWindowStart );
            if( fpsWindowElapsed.count() >= 1000 )
            {
                const float fps =
                    static_cast<float>( fpsFrameCount ) * 1000.0f / fpsWindowElapsed.count();
                const float frameMs =
                    static_cast<float>( fpsWindowElapsed.count() ) / fpsFrameCount;
                std::cerr << "[WaylandSmoke] fps=" << fps << " frame_ms=" << frameMs << '\n';
                fpsWindowStart = now;
                fpsFrameCount = 0;
            }

            wl_display_flush( hostState.display );

            const auto sinceResize =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - lastResizeTime );
            if( sinceResize.count() >= 150 )
                std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
        }

        if( gSignalCaught )
        {
            std::cerr << "[WaylandSmoke] exiting after signal=" << gSignalCaught << '\n';
        }

        logWaylandEvent( "destroying render window" );
        renderSystem->destroyRenderWindow( window );
        logWaylandEvent( "render window destroyed" );
    }
    catch( const Ogre::Exception &e )
    {
        std::cerr << e.getFullDescription() << '\n';
        logWaylandEvent( "destroying host window after Ogre exception" );
        destroyHostWindow( hostState );
        return 1;
    }
    catch( const std::exception &e )
    {
        std::cerr << e.what() << '\n';
        logWaylandEvent( "destroying host window after std exception" );
        destroyHostWindow( hostState );
        return 1;
    }

    logWaylandEvent( "destroying host window after normal exit" );
    destroyHostWindow( hostState );
    logWaylandEvent( "host window destroyed" );
    return 0;
}
