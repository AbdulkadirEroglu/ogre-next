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
#include "OgreWindow.h"

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace
{
    volatile std::sig_atomic_t gSignalCaught = 0;

    void logEvent( const char *message )
    {
        std::cerr << "[GlxSmoke] " << message << '\n';
    }

    void signalHandler( int signalNumber )
    {
        gSignalCaught = signalNumber;

        const char *message = "[GlxSmoke] signal received\n";
        switch( signalNumber )
        {
        case SIGINT:
            message = "[GlxSmoke] signal received: SIGINT\n";
            break;
        case SIGTERM:
            message = "[GlxSmoke] signal received: SIGTERM\n";
            break;
        case SIGQUIT:
            message = "[GlxSmoke] signal received: SIGQUIT\n";
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

    void checkState( bool condition, const char *message )
    {
        if( !condition )
            throw Ogre::Exception( Ogre::Exception::ERR_RENDERINGAPI_ERROR, message,
                                   "GlxSmokeTest" );
    }

    typedef void ( *PfnGlClearColor )( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha );
    typedef void ( *PfnGlClear )( GLbitfield mask );
    typedef void ( *PfnGlViewport )( GLint x, GLint y, GLsizei width, GLsizei height );

    struct GlProcedures
    {
        PfnGlClearColor clearColor = 0;
        PfnGlClear clear = 0;
        PfnGlViewport viewport = 0;
    };

    struct EventPumpResult
    {
        bool closed = false;
        bool resized = false;
    };

    GlProcedures loadGlProcedures()
    {
        GlProcedures gl;
        gl.clearColor = reinterpret_cast<PfnGlClearColor>( glXGetProcAddressARB(
            reinterpret_cast<const GLubyte *>( "glClearColor" ) ) );
        gl.clear = reinterpret_cast<PfnGlClear>( glXGetProcAddressARB(
            reinterpret_cast<const GLubyte *>( "glClear" ) ) );
        gl.viewport = reinterpret_cast<PfnGlViewport>( glXGetProcAddressARB(
            reinterpret_cast<const GLubyte *>( "glViewport" ) ) );

        checkState( gl.clearColor != 0, "Failed to load glClearColor" );
        checkState( gl.clear != 0, "Failed to load glClear" );
        checkState( gl.viewport != 0, "Failed to load glViewport" );
        return gl;
    }

    EventPumpResult pumpX11Events( Display *display, ::Window window, Atom deleteAtom,
                                   Ogre::Window *ogreWindow, uint32_t &width, uint32_t &height )
    {
        EventPumpResult result;

        while( XPending( display ) > 0 )
        {
            XEvent event;
            XNextEvent( display, &event );

            if( event.xany.window != window )
                continue;

            switch( event.type )
            {
            case ConfigureNotify:
                if( event.xconfigure.width > 0 && event.xconfigure.height > 0 )
                {
                    width = static_cast<uint32_t>( event.xconfigure.width );
                    height = static_cast<uint32_t>( event.xconfigure.height );
                    result.resized = true;
                    std::cerr << "[GlxSmoke] configure " << width << "x" << height << '\n';
                    ogreWindow->windowMovedOrResized();
                }
                break;
            case ClientMessage:
                if( static_cast<Atom>( event.xclient.data.l[0] ) == deleteAtom )
                {
                    logEvent( "wm_delete_window" );
                    result.closed = true;
                }
                break;
            case DestroyNotify:
                logEvent( "destroy_notify" );
                result.closed = true;
                break;
            default:
                break;
            }
        }

        return result;
    }
}  // namespace

int main( int argc, char **argv )
{
    const int maxFrames = argc > 1 ? std::max( 1, std::atoi( argv[1] ) ) : 120;
    const bool verboseOgreLogging = std::getenv( "OGRE_GLX_SMOKE_OGRE_DEBUG" ) != 0;

    try
    {
        installSignalHandlers();

        Ogre::LogManager logManager;
        Ogre::Log *defaultLog =
            logManager.createLog( "GlxSmoke.log", true, verboseOgreLogging, false );
        defaultLog->setDebugOutputEnabled( verboseOgreLogging );
        logManager.setLogDetail( verboseOgreLogging ? Ogre::LL_NORMAL : Ogre::LL_LOW );

        Ogre::Root root( "", "glx-smoke.cfg", "GlxSmoke.log" );
        Ogre::GL3PlusPlugin plugin;
        root.installPlugin( &plugin, 0 );

        Ogre::RenderSystem *renderSystem =
            root.getRenderSystemByName( "OpenGL 3+ Rendering Subsystem" );
        checkState( renderSystem != 0, "OpenGL 3+ Rendering Subsystem not found" );

        root.setRenderSystem( renderSystem );
        renderSystem->setConfigOption( "Interface", "GLX Window (Default)" );
        renderSystem->setConfigOption( "sRGB Gamma Conversion", "No" );
        root.initialise( false );

        Ogre::NameValuePairList miscParams;
        miscParams["title"] = "Ogre GLX Smoke Test";

        Ogre::Window *window =
            root.createRenderWindow( "GlxSmoke", 1280u, 720u, false, &miscParams );
        checkState( window != 0, "Failed to create GLX render window" );

        Ogre::GL3PlusContext *context = 0;
        window->getCustomAttribute( "GLCONTEXT", &context );
        checkState( context != 0, "Failed to obtain GL3Plus context" );

        Display *display = 0;
        ::Window xWindow = 0;
        Atom deleteAtom = None;
        window->getCustomAttribute( "DISPLAY", &display );
        window->getCustomAttribute( "WINDOW", &xWindow );
        window->getCustomAttribute( "ATOM", &deleteAtom );
        checkState( display != 0, "Failed to obtain X11 display" );
        checkState( xWindow != 0, "Failed to obtain X11 window" );

        const GlProcedures gl = loadGlProcedures();
        const auto startTime = std::chrono::steady_clock::now();
        auto fpsWindowStart = startTime;
        auto lastResizeTime = startTime - std::chrono::seconds( 1 );
        uint32_t width = window->getWidth();
        uint32_t height = window->getHeight();
        bool closed = false;
        int fpsFrameCount = 0;

        logEvent( "GLX smoke window created" );

        for( int frame = 0; frame < maxFrames && !closed && !gSignalCaught; ++frame )
        {
            const EventPumpResult eventResult =
                pumpX11Events( display, xWindow, deleteAtom, window, width, height );
            closed = eventResult.closed;
            if( eventResult.resized )
                lastResizeTime = std::chrono::steady_clock::now();

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime );
            const float t = static_cast<float>( elapsed.count() ) * 0.001f;

            context->setCurrent();
            gl.viewport( 0, 0, static_cast<GLsizei>( width ), static_cast<GLsizei>( height ) );
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
                std::cerr << "[GlxSmoke] fps=" << fps << " frame_ms=" << frameMs << '\n';
                fpsWindowStart = now;
                fpsFrameCount = 0;
            }

            const auto sinceResize =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - lastResizeTime );
            if( sinceResize.count() >= 150 )
                std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
        }

        if( gSignalCaught )
            std::cerr << "[GlxSmoke] exiting after signal=" << gSignalCaught << '\n';

        logEvent( "destroying render window" );
        renderSystem->destroyRenderWindow( window );
        logEvent( "render window destroyed" );
    }
    catch( const Ogre::Exception &e )
    {
        std::cerr << e.getFullDescription() << '\n';
        return 1;
    }
    catch( const std::exception &e )
    {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
