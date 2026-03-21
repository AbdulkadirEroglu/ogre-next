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

#include "windowing/OgreGlSwitchableSupport.h"

#include "OgreException.h"
#include "OgreGL3PlusRenderSystem.h"
#include "OgreLogManager.h"
#include "OgreStringConverter.h"

#ifdef OGRE_GLSUPPORT_USE_GLX
#    include "windowing/GLX/OgreGLXGLSupport.h"
#endif
#ifdef OGRE_GLSUPPORT_USE_WGL
#    include "windowing/win32/OgreWin32GLSupport.h"
#endif
#ifdef OGRE_GLSUPPORT_USE_EGL_HEADLESS
#    include "windowing/EGL/PBuffer/OgreEglPBufferSupport.h"
#endif
#ifdef OGRE_GLSUPPORT_USE_EGL_WAYLAND
#    include "windowing/EGL/Wayland/OgreWaylandEglSupport.h"
#endif

namespace Ogre
{
    //-------------------------------------------------------------------------
    GlSwitchableSupport::GlSwitchableSupport() : mSelectedInterface( 0u ), mInterfaceSelected( false )
    {
#ifdef OGRE_GLSUPPORT_USE_GLX
        mAvailableInterfaces.push_back( Interface( WindowNative, 0 ) );
#endif
#ifdef OGRE_GLSUPPORT_USE_WGL
        mAvailableInterfaces.push_back( Interface( WindowNative, 0 ) );
#endif
#ifdef OGRE_GLSUPPORT_USE_EGL_HEADLESS
        mAvailableInterfaces.push_back( Interface( HeadlessEgl, 0 ) );
#endif
#ifdef OGRE_GLSUPPORT_USE_EGL_WAYLAND
        mAvailableInterfaces.push_back( Interface( WaylandEgl, 0 ) );
#endif

        if( mAvailableInterfaces.empty() )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                         "No Interface could be loaded. Check previous error messages."
                         "Try disabling OpenGL plugin from plugins.cfg.",
                         "GlSwitchableSupport::GlSwitchableSupport" );
        }
    }  // namespace Ogre
    //-------------------------------------------------------------------------
    GlSwitchableSupport::~GlSwitchableSupport()
    {
        FastArray<Interface>::const_iterator itor = mAvailableInterfaces.begin();
        FastArray<Interface>::const_iterator endt = mAvailableInterfaces.end();

        while( itor != endt )
        {
            delete itor->support;
            ++itor;
        }
        mAvailableInterfaces.clear();
    }
    //-------------------------------------------------------------------------
    const char *GlSwitchableSupport::getInterfaceName( InterfaceType interface )
    {
        switch( interface )
        {
        case WindowNative:
#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX || OGRE_PLATFORM == OGRE_PLATFORM_FREEBSD
            return "GLX Window (Default)";
#elif OGRE_PLATFORM == OGRE_PLATFORM_WIN32
            return "WGL Window (Default)";
#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE
            return "Cocoa Window (Default)";
#else
#    error Unsupported platform. Build without GL Switchable support
#endif
        case HeadlessEgl:
            return "Headless EGL / PBuffer";
        case WaylandEgl:
            return "Wayland EGL Window";
        }

        return "ERROR";
    }
    //-------------------------------------------------------------------------
    GL3PlusSupport *GlSwitchableSupport::ensureSupportCreated( uint8 idx )
    {
        Interface &selectedInterface = mAvailableInterfaces[idx];
        if( selectedInterface.support )
            return selectedInterface.support;

        try
        {
            switch( selectedInterface.type )
            {
            case WindowNative:
#if defined( OGRE_GLSUPPORT_USE_GLX )
                selectedInterface.support = new GLXGLSupport();
#elif defined( OGRE_GLSUPPORT_USE_WGL )
                selectedInterface.support = new Win32GLSupport();
#else
                OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                             "Native window OpenGL support was requested but is unavailable.",
                             "GlSwitchableSupport::ensureSupportCreated" );
#endif
                break;
            case HeadlessEgl:
#ifdef OGRE_GLSUPPORT_USE_EGL_HEADLESS
                selectedInterface.support = new EglPBufferSupport();
#else
                OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                             "Headless EGL support was requested but is unavailable.",
                             "GlSwitchableSupport::ensureSupportCreated" );
#endif
                break;
            case WaylandEgl:
#ifdef OGRE_GLSUPPORT_USE_EGL_WAYLAND
                selectedInterface.support = new WaylandEglSupport();
#else
                OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                             "Wayland EGL support was requested but is unavailable.",
                             "GlSwitchableSupport::ensureSupportCreated" );
#endif
                break;
            }
        }
        catch( Exception &e )
        {
            const char *interfaceName = getInterfaceName( selectedInterface.type );
            LogManager::getSingleton().logMessage( String( interfaceName ) +
                                                   " raised an exception during creation." );
            LogManager::getSingleton().logMessage( e.getFullDescription() );
            throw;
        }

        return selectedInterface.support;
    }
    //-------------------------------------------------------------------------
    void GlSwitchableSupport::addConfig( void )
    {
        mInterfaceSelected = false;

        ConfigOption optInterfaces;

        optInterfaces.name = "Interface";

        FastArray<Interface>::const_iterator itor = mAvailableInterfaces.begin();
        FastArray<Interface>::const_iterator endt = mAvailableInterfaces.end();

        while( itor != endt )
        {
            optInterfaces.possibleValues.push_back( getInterfaceName( itor->type ) );
            ++itor;
        }

        optInterfaces.currentValue = optInterfaces.possibleValues[mSelectedInterface];
        optInterfaces.immutable = false;

        ensureSupportCreated( mSelectedInterface )->addConfig();

        mOptions[optInterfaces.name] = optInterfaces;
        mOptions.insert( ensureSupportCreated( mSelectedInterface )->getConfigOptions().begin(),
                         ensureSupportCreated( mSelectedInterface )->getConfigOptions().end() );

        refreshConfig();
    }
    //-------------------------------------------------------------------------
    void GlSwitchableSupport::refreshConfig( void )
    {
        ConfigOptionMap::iterator optInterfaces = mOptions.find( "Interface" );

        if( optInterfaces != mOptions.end() )
        {
            const uint8 newInterfaceIdx = findSelectedInterfaceIdx();
            if( newInterfaceIdx != mSelectedInterface )
            {
                ConfigOptionMap::iterator itNext = optInterfaces;
                ++itNext;

                mOptions.erase( mOptions.begin(), optInterfaces );
                mOptions.erase( itNext, mOptions.end() );

                if( mAvailableInterfaces[mSelectedInterface].support )
                    mAvailableInterfaces[mSelectedInterface].support->stop();
                mSelectedInterface = newInterfaceIdx;
                start();
                ensureSupportCreated( mSelectedInterface )->addConfig();
                mOptions.insert(
                    ensureSupportCreated( mSelectedInterface )->getConfigOptions().begin(),
                    ensureSupportCreated( mSelectedInterface )->getConfigOptions().end() );
            }
        }
    }
    //-------------------------------------------------------------------------
    void GlSwitchableSupport::setConfigOption( const String &name, const String &value )
    {
        ConfigOptionMap::iterator option = mOptions.find( name );

        if( name == "Interface" )
        {
            option->second.currentValue = value;
            refreshConfig();
        }
        else
        {
            ensureSupportCreated( mSelectedInterface )->setConfigOption( name, value );

            // Update our copy of mOptions
            const ConfigOptionMap &interfOpts =
                ensureSupportCreated( mSelectedInterface )->getConfigOptions();
            ConfigOptionMap::const_iterator itOpt = interfOpts.find( name );
            if( interfOpts.find( name ) != interfOpts.end() )
                mOptions[name] = itOpt->second;
        }
    }
    //-------------------------------------------------------------------------
    String GlSwitchableSupport::validateConfig( void )
    {
        return ensureSupportCreated( mSelectedInterface )->validateConfig();
    }
    //-------------------------------------------------------------------------
    const char *GlSwitchableSupport::getPriorityConfigOption( size_t idx ) const
    {
        if( idx > 0u )
            return const_cast<GlSwitchableSupport *>( this )->ensureSupportCreated(
                mSelectedInterface )->getPriorityConfigOption( idx );
        return "Interface";
    }
    //-------------------------------------------------------------------------
    size_t GlSwitchableSupport::getNumPriorityConfigOptions( void ) const
    {
        return 1u + const_cast<GlSwitchableSupport *>( this )->ensureSupportCreated(
                        mSelectedInterface )->getNumPriorityConfigOptions();
    }
    //-------------------------------------------------------------------------
    Window *GlSwitchableSupport::createWindow( bool autoCreateWindow, GL3PlusRenderSystem *renderSystem,
                                               const String &windowTitle )
    {
        return ensureSupportCreated( mSelectedInterface )->createWindow(
            autoCreateWindow, renderSystem, windowTitle );
    }
    //-------------------------------------------------------------------------
    Window *GlSwitchableSupport::newWindow( const String &name, uint32 width, uint32 height,
                                            bool fullscreen, const NameValuePairList *miscParams )
    {
        return ensureSupportCreated( mSelectedInterface )->newWindow( name, width, height,
                                                                      fullscreen, miscParams );
    }
    //-------------------------------------------------------------------------
    void GlSwitchableSupport::start() { ensureSupportCreated( mSelectedInterface )->start(); }
    //-------------------------------------------------------------------------
    void GlSwitchableSupport::stop()
    {
        if( mAvailableInterfaces[mSelectedInterface].support )
            mAvailableInterfaces[mSelectedInterface].support->stop();
    }
    //-------------------------------------------------------------------------
    void *GlSwitchableSupport::getProcAddress( const char *procname ) const
    {
        return const_cast<GlSwitchableSupport *>( this )->ensureSupportCreated(
            mSelectedInterface )->getProcAddress( procname );
    }
    //-------------------------------------------------------------------------
    uint8 GlSwitchableSupport::findSelectedInterfaceIdx( void ) const
    {
        ConfigOptionMap::const_iterator it = mOptions.find( "Interface" );
        if( it != mOptions.end() )
        {
            uint8 interfaceIdx = 0u;

            FastArray<Interface>::const_iterator itor = mAvailableInterfaces.begin();
            FastArray<Interface>::const_iterator endt = mAvailableInterfaces.end();

            while( itor != endt )
            {
                if( it->second.currentValue == getInterfaceName( itor->type ) )
                    return interfaceIdx;
                ++interfaceIdx;
                ++itor;
            }
        }

        return 0u;
    }
}  // namespace Ogre
