// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "av_cef_app.h"

#include <string>

// #include "include/cef_browser.h"
// #include "include/cef_command_line.h"
// #include "include/views/cef_browser_view.h"
// #include "include/views/cef_window.h"
// #include "include/wrapper/cef_helpers.h"
// #include "av_cef_handler.h"
// #include "av_cef_javascript.h"
#include <aardvark/aardvark_gadget_manifest.h>

#include <openvr.h>
#include <processthreadsapi.h>

#include <tools/logging.h>

#include <glm/gtc/type_ptr.hpp>
#include "out.h"
#include "javascript_renderer.h"

namespace 
{

	CAardvarkCefApp* g_instance = NULL;



/* // When using the Views framework this object provides the delegate
// implementation for the CefWindow that hosts the Views-based browser.
class SimpleWindowDelegate : public CefWindowDelegate 
{
public:
	explicit SimpleWindowDelegate(CefRefPtr<CefBrowserView> browser_view)
		: browser_view_(browser_view) {}

	void OnWindowCreated(CefRefPtr<CefWindow> window) OVERRIDE 
	{
		// Add the browser view and show the window.
		window->AddChildView(browser_view_);
		window->Show();

		// Give keyboard focus to the browser view.
		browser_view_->RequestFocus();
	}

	void OnWindowDestroyed(CefRefPtr<CefWindow> window) OVERRIDE 
	{
		browser_view_ = NULL;
	}

	bool CanClose(CefRefPtr<CefWindow> window) OVERRIDE 
	{
		// Allow the window to close if the browser says it's OK.
		CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
		if (browser)
			return browser->GetHost()->TryCloseBrowser();
		return true;
	}

private:
	CefRefPtr<CefBrowserView> browser_view_;

	IMPLEMENT_REFCOUNTING(SimpleWindowDelegate);
	DISALLOW_COPY_AND_ASSIGN(SimpleWindowDelegate);
}; */

}  // namespace

CAardvarkCefApp::CAardvarkCefApp() 
{
	g_instance = this;
  
  vr::EVRInitError err;
	vr::VR_Init( &err, vr::VRApplication_Overlay );
	IDXGIAdapter* pAdapter = nullptr;
	if (err != vr::VRInitError_None)
	{
		IDXGIFactory1* pIDXGIFactory;
		if ( !FAILED(CreateDXGIFactory1( __uuidof(IDXGIFactory1), (void**)&pIDXGIFactory ) ) )
		{
			int32_t nAdapterIndex;
			vr::VRSystem()->GetDXGIOutputInfo(&nAdapterIndex);

			if ( !FAILED( pIDXGIFactory->EnumAdapters( nAdapterIndex, &pAdapter ) ) )
			{
				LOG(INFO) << "Using adapter " << nAdapterIndex << " for graphics device" << std::endl;
			}

		}

		if (pIDXGIFactory)
		{
			pIDXGIFactory->Release();
			pIDXGIFactory = nullptr;
		}
	}
	vr::VR_Shutdown();

	D3D_FEATURE_LEVEL featureLevel[] = { D3D_FEATURE_LEVEL_11_1 };
	D3D_FEATURE_LEVEL createdFeatureLevel;

	D3D11CreateDevice( pAdapter, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
		D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT, 
		featureLevel, 1, D3D11_SDK_VERSION,
		&m_pD3D11Device, &createdFeatureLevel, &m_pD3D11ImmediateContext );

	if ( pAdapter )
	{
		pAdapter->Release();
		pAdapter = nullptr;
	}
}

CAardvarkCefApp::~CAardvarkCefApp()
{
	g_instance = nullptr;
}

void CAardvarkCefApp::startRenderer() {
  renderer.reset(new CJavascriptRenderer());
  renderer->init();
}

bool CAardvarkCefApp::tickRenderer() {
	renderer->runFrame();
	return !renderer->m_quitting;
}

void CAardvarkCefApp::getPoses(float *hmd, float *left, float *right) {
	glm::mat4 headMatrix;
  renderer->m_vrManager->getUniverseFromOrigin("/user/head", &headMatrix);
	memcpy(hmd, glm::value_ptr(headMatrix), sizeof(headMatrix));
	glm::mat4 leftMatrix;
  renderer->m_vrManager->getUniverseFromOrigin("/user/hand/left", &leftMatrix);
	memcpy(left, glm::value_ptr(leftMatrix), sizeof(leftMatrix));
	glm::mat4 rightMatrix;
  renderer->m_vrManager->getUniverseFromOrigin("/user/hand/right", &rightMatrix);
	memcpy(right, glm::value_ptr(rightMatrix), sizeof(rightMatrix));
}

/* void CAardvarkCefApp::OnContextInitialized()
{
	CEF_REQUIRE_UI_THREAD();

	CefRefPtr<CefCommandLine> command_line =
	CefCommandLine::GetGlobalCommandLine();

#if defined(OS_WIN) || defined(OS_LINUX)
	// Create the browser using the Views framework if "--use-views" is specified
	// via the command-line. Otherwise, create the browser using the native
	// platform framework. The Views framework is currently only supported on
	// Windows and Linux.
	const bool use_views = command_line->HasSwitch("use-views");
#else
	const bool use_views = false;
#endif

	vr::EVRInitError err;
	vr::VR_Init( &err, vr::VRApplication_Overlay );
	IDXGIAdapter* pAdapter = nullptr;
	if (err != vr::VRInitError_None)
	{
		IDXGIFactory1* pIDXGIFactory;
		if ( !FAILED(CreateDXGIFactory1( __uuidof(IDXGIFactory1), (void**)&pIDXGIFactory ) ) )
		{
			int32_t nAdapterIndex;
			vr::VRSystem()->GetDXGIOutputInfo(&nAdapterIndex);

			if ( !FAILED( pIDXGIFactory->EnumAdapters( nAdapterIndex, &pAdapter ) ) )
			{
				LOG(INFO) << "Using adapter " << nAdapterIndex << " for graphics device" << std::endl;
			}

		}

		if (pIDXGIFactory)
		{
			pIDXGIFactory->Release();
			pIDXGIFactory = nullptr;
		}
	}
	vr::VR_Shutdown();

	D3D_FEATURE_LEVEL featureLevel[] = { D3D_FEATURE_LEVEL_11_1 };
	D3D_FEATURE_LEVEL createdFeatureLevel;

	D3D11CreateDevice( pAdapter, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
		D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT, 
		featureLevel, 1, D3D11_SDK_VERSION,
		&m_pD3D11Device, &createdFeatureLevel, &m_pD3D11ImmediateContext );

	if ( pAdapter )
	{
		pAdapter->Release();
		pAdapter = nullptr;
	}

	startGadget( "http://localhost:23842/gadgets/aardvark_master", "", "master", aardvark::EndpointAddr_t(), "" );
} */


/* void CAardvarkCefApp::startGadget( const std::string & uri, const std::string & initialHook, 
	const std::string & persistenceUuid, const aardvark::EndpointAddr_t & epToNotify,
	const std::string & remoteUniversePath )
{
	CEF_REQUIRE_UI_THREAD();

	// CAardvarkCefHandler implements browser-level callbacks.
	CefRefPtr<CAardvarkCefHandler> handler( new CAardvarkCefHandler( this, uri, initialHook, persistenceUuid, epToNotify, remoteUniversePath ) );
	m_browsers.push_back( handler );
	handler->start();
} */


/* void CAardvarkCefApp::OnBeforeCommandLineProcessing( const CefString& processType, CefRefPtr<CefCommandLine> commandLine )
{
	// turn on chrome dev tools
	commandLine->AppendSwitchWithValue( "remote-debugging-port", std::to_string( 8042 ) );
} */


/* CefRefPtr<CefRenderProcessHandler> CAardvarkCefApp::GetRenderProcessHandler()
{
	if ( !m_renderProcessHandler )
	{
		CefRefPtr<CefRenderProcessHandler> newHandler( new CAardvarkRenderProcessHandler() );
		m_renderProcessHandler = newHandler;
	}
	return m_renderProcessHandler;
} */

/* void CAardvarkCefApp::CloseAllBrowsers( bool forceClose )
{
	std::vector< CefRefPtr< CAardvarkCefHandler > > browsers = m_browsers;
	for ( auto browser : browsers )
	{
		browser->triggerClose( forceClose );
	}
} */

CAardvarkCefApp* CAardvarkCefApp::instance()
{
	return g_instance;
}

void CAardvarkCefApp::runFrame()
{
	m_pD3D11ImmediateContext->Flush();

	/* if ( m_quitRequested && !m_quitHandled )
	{
		m_quitHandled = true;
		CloseAllBrowsers( true );
	} */
}

bool CAardvarkCefApp::wantsToQuit()
{
	return /* m_browsers.empty() && */m_quitRequested;
}

/* void CAardvarkCefApp::quitRequested()
{
	m_quitRequested = true;
} */


/* void CAardvarkCefApp::browserClosed( CAardvarkCefHandler *handler )
{
	auto i = std::find( m_browsers.begin(), m_browsers.end(), handler );
	if ( i != m_browsers.end() )
	{
		m_browsers.erase( i );
	}
} */



/* bool CAardvarkCefApp::createTextureForBrowser( void **sharedHandle, 
	int width, int height )
{
	if ( !m_pD3D11Device )
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC sharedDesc;
	sharedDesc.Width = width;
	sharedDesc.Height = height;
	sharedDesc.MipLevels = sharedDesc.ArraySize = 1;
	sharedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sharedDesc.SampleDesc.Count = 1;
	sharedDesc.SampleDesc.Quality = 0;
	sharedDesc.Usage = D3D11_USAGE_DEFAULT;
	sharedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	sharedDesc.CPUAccessFlags = 0;
	sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	ID3D11Texture2D *texture = nullptr;
	HRESULT result = m_pD3D11Device->CreateTexture2D( &sharedDesc, nullptr, &texture );
	if ( result != S_OK )
		return false;

	IDXGIResource *dxgiResource = nullptr;
	texture->QueryInterface( __uuidof( IDXGIResource ), (LPVOID*)& dxgiResource );

	void *handle = nullptr;
	dxgiResource->GetSharedHandle( &handle );
	dxgiResource->Release();

	if ( !handle )
	{
		return false;
	}

	m_browserTextures.insert( std::make_pair( handle, texture ) );

	*sharedHandle = handle;
	return true;
}

void CAardvarkCefApp::updateTexture( void *sharedHandle, const void *buffer, int width, int height )
{
	auto i = m_browserTextures.find( sharedHandle );
	if ( i == m_browserTextures.end() )
		return;

	D3D11_BOX box;
	box.left = 0;
	box.right = width;
	box.top = 0;
	box.bottom = height;
	box.front = 0;
	box.back = 1;
	m_pD3D11ImmediateContext->UpdateSubresource( i->second, 0, &box, buffer, width * 4, width * height * 4 );
} */

