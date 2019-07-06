@0x9a3e9fafc7daa5c7; # unique file ID generated by Cap'n Proto


struct AvSharedTextureInfo
{
	enum Type
	{
		invalid @0;			#Somebody didn't set something
		d3d11Texture2D @1;	# An ID3D11Texture2D object
	}

	enum Format
	{
		r8g8b8a8 @0;
		b8g8r8a8 @1;
	}

	type @0 : Type;
	format @1 : Format;
	width @2 : UInt32;
	height @3 : UInt32;
	sharedTextureHandle @4 : UInt64;
	invertY @5 : Bool;
}

struct AvVector
{
	x @0 : Float32;
	y @1 : Float32;
	z @2 : Float32;
}

struct AvColor
{
	r @0 : Float32;
	g @1 : Float32;
	b @2 : Float32;
	a @3 : Float32;
}

struct AvQuaternion
{
	x @0 : Float32;
	y @1 : Float32;
	z @2 : Float32;
	w @3 : Float32;
}

struct AvTransform
{
	position @0: AvVector;
	rotation @1: AvQuaternion;
	scale @2: AvVector;
}

struct AvLight
{
	transform @0: AvTransform;
	diffuse @1: AvColor;
}

struct AvGadgetTextureInfo
{
	gadgetName @0: Text;
	gadgetId @1: UInt32;
	sharedTextureInfo @2: AvSharedTextureInfo;
}

struct AvVisualFrame
{
	id @0: UInt64;
	roots @1: List( AvNodeRoot );
	gadgetTextures @2: List( AvGadgetTextureInfo );
}

interface AvFrameListener
{
	newFrame @0 (frame: AvVisualFrame) -> (  );
	sendHapticEvent @1 ( targetGlobalId: UInt64, amplitude: Float32, frequency: Float32, duration: Float32 ) -> ();
	startGrab @2 ( grabberGlobalId: UInt64, grabbableGlobalId: UInt64 ) -> ();
	endGrab @3 ( grabberGlobalId: UInt64, grabbableGlobalId: UInt64 ) -> ();
}

struct AvPanelProximity
{
	panelId @0: UInt64;
	x @1: Float32;
	y @2: Float32;
	distance @3: Float32;
}

interface AvPokerProcessor
{
	updatePanelProximity @0 ( pokerId: UInt32, proximity: List( AvPanelProximity ) ) -> ();
}

struct AvPanelMouseEvent
{
	enum Type
	{
		down @0;
		up @1;
		enter @2;
		leave @3;
		move @4;
	}

	type @0: Type;
	panelId @1: UInt64;
	pokerId @2: UInt64;
	x @3: Float32;
	y @4: Float32;
}

interface AvPanelProcessor
{
	mouseEvent @0 ( panelId: UInt32, event: AvPanelMouseEvent ) -> ();
}


interface AvGrabberProcessor
{
	updateGrabberIntersections @0 ( grabberId: UInt32, grabPressed: Bool, intersections: List( UInt64) ) -> ();
}

struct AvGrabEvent
{
	enum Type
	{
		enterRange @0;
		leaveRange @1;
		startGrab @2;
		endGrab @3;
	}

	type @0: Type;
	grabbableId @1: UInt64;
	grabberId @2: UInt64;
	transform @3: AvTransform;
}

interface AvGrabbableProcessor
{
	grabEvent @0 ( grabbableId: UInt32, event: AvGrabEvent ) -> ();
}

struct AvVolume
{
	enum Type
	{
		invalid @0;			# something is broken about this volume

		sphere @1;			# has a radius from 0,0,0 
	}

	type @0: Type;
	radius @1: Float32;
}


struct AvNode
{
	enum Type
	{
		invalid @0;			# something is broken about this node

		container @1;		# has no properties. Just contains other nodes
		origin @2;			# Sets the origin path
		transform @3;		# Contains a transform
		model @4;			# Contains a model URI
		panel @5;			# Contains: propInteractive, propTextureSource
		poker @6;			# has no properties
		grabbable @7;		# has no properties; contains handles
		handle @8;			# contains a volume
		grabber @9;			# contains a volume
	}

	id @0: UInt32;
	name @1: Text;
	children @2: List(UInt32);
	type @3: Type = invalid;
	flags @4: UInt32 = 0;

	# These properties are allowed for a subset of node types
	propOrigin @5: Text;			# origin
	propTransform @6: AvTransform;	# transform
	propModelUri @7: Text;			# model
	propTextureSource @8: Text;		# panel
	propInteractive @9: Bool;		# panel
	propVolume @10: AvVolume;		# grabber and handle
}

struct AvNodeWrapper
{
	node @0 : AvNode;
}

struct AvNodeRoot
{
	nodes @0 : List( AvNodeWrapper );
	sourceId @1 : UInt32;
	pokerProcessor @2: AvPokerProcessor;
	panelProcessor @3: AvPanelProcessor;
	grabberProcessor @4: AvGrabberProcessor;
	grabbableProcessor @5: AvGrabbableProcessor;
	hook @6: Text;
}

interface AvServer
{
	createGadget @0 ( name: Text, initialHook: Text ) -> ( gadget: AvGadget, gadgetId: UInt32 );
	listenForFrames @1 ( listener: AvFrameListener ) -> ();
	updateDxgiTextureForGadgets @2 
	( 
		gadgetIds: List( UInt32 ), 
		sharedTextureInfo: AvSharedTextureInfo 
	) -> ( success: Bool );
	pushPokerProximity @3 
	(
		pokerId : UInt64,
		proximity: List( AvPanelProximity )
	) -> ();
	pushGrabIntersections @4 
	(
		grabberId : UInt64,
		isGrabPressed: Bool,
		intersections: List( UInt64 )
	) -> ();
}

interface AvGadget
{
	name @0 () -> ( name: Text );

	destroy @1 () -> ( success: Bool );

	updateSceneGraph @2 (root: AvNodeRoot ) -> ( success: Bool );
	pushMouseEvent @3 ( pokerNodeId: UInt32, event: AvPanelMouseEvent ) -> ();
	sendHapticEvent @4 ( nodeGlobalId: UInt64, amplitude: Float32, frequency: Float32, duration: Float32 ) -> ();
	pushGrabEvent @5 ( grabberNodeId: UInt32, event: AvGrabEvent ) -> ();
}


