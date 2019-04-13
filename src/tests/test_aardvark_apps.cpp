// ---------------------------------------------------------------------------
// Purpose: Test app API in Aardvark
// ---------------------------------------------------------------------------
#include <catch/catch.hpp>
#include <aardvark/aardvark_apps.h>

using namespace aardvark;

TEST_CASE( "Aardvark apps", "[apps]" ) 
{
	AppHandle_t hApp = nullptr;
	REQUIRE( avCreateApp( "fnord", &hApp ) == AardvarkError_None );
	REQUIRE( hApp != nullptr );
	REQUIRE( avDestroyApp( hApp ) == AardvarkError_None );
}
