import * as React from 'react';
import { AvBaseNode, AvBaseNodeProps } from '@aardvarkxr/aardvark-react';
import { AvNodeType, MinimalPose, AvNodeTransform } from '@aardvarkxr/aardvark-shared';
import { ChamberSubscription, ChamberMemberInfo, PoseUpdatedArgs } from 'common/net_chamber';
import bind from 'bind-decorator';
const Quaternion = require( 'quaternion' );


function minimalToAvNodeTransform( minimal: MinimalPose ): AvNodeTransform
{
	let transform: AvNodeTransform = 
	{
		position: { x: minimal[0], y: minimal[1], z: minimal[2] },
		rotation: { w: minimal[3], x: minimal[4], y: minimal[5], z: minimal[6] }, 
	};
	return transform;
}

interface AvRemoteUniverseProps extends AvBaseNodeProps
{
	universe: string;
	chamberFromRemote: MinimalPose;
}

class AvRemoteUniverse extends AvBaseNode< AvRemoteUniverseProps, {} >
{
	public buildNode()
	{
		let node = this.createNodeObject( AvNodeType.RemoteUniverse, this.m_nodeId );
		node.propUniverseName = this.props.universe;
		node.propTransform = minimalToAvNodeTransform( this.props.chamberFromRemote );
		return node;
	}
}


interface AvRemoteOriginProps extends AvBaseNodeProps
{
	originPath: string;
	remoteFromOrigin: MinimalPose;
}

class AvRemoteOrigin extends AvBaseNode< AvRemoteOriginProps, {} >
{
	public buildNode()
	{
		let node = this.createNodeObject( AvNodeType.RemoteOrigin, this.m_nodeId );
		node.propOrigin = this.props.originPath;
		node.propTransform = minimalToAvNodeTransform( this.props.remoteFromOrigin );
		return node;
	}
}


interface ChamberPosesProps extends AvBaseNodeProps
{
	chamber: ChamberSubscription;
	member: ChamberMemberInfo;
}

//create your forceUpdate hook
function useForceUpdate(){
    const [value, setValue] = React.useState(0); // integer state
    return () => setValue(value => ++value); // update the state to force render
}

export function ChamberMemberPoses( props: ChamberPosesProps )
{
	let [ poses, setPoses ] = React.useState( props.member.poses );

	React.useEffect( () =>
	{
		let fnPoseUpdated = ( chamber: ChamberSubscription, args: PoseUpdatedArgs ) => 
		{
			if( args.userUuid == props.member.uuid )
			{
				setPoses( { ...props.member.poses } );
			}
		}

		props.chamber.addPoseHandler( fnPoseUpdated );

		return () => 
		{
			props.chamber.removePoseHandler( fnPoseUpdated );
		}
	} );

	let origins: JSX.Element[] = [];
	for( let originPath in poses )
	{
		origins.push(
			<AvRemoteOrigin key={ originPath } originPath={ originPath } 
				remoteFromOrigin={ poses[ originPath ] } />
		);
	}

	let rot = Quaternion.fromAxisAngle( [ 0, 1, 0 ], Math.PI );

	// let transform: MinimalPose = [ 0, 0.5, 0, 1, 0, 0, 0 ];
	let transform: MinimalPose = [ 0, 0.0, 1.5, rot.w, rot.x, rot.y, rot.z ];

	let universePath = props.chamber.chamberPath + "/" + props.member.uuid;
	return <AvRemoteUniverse key={ universePath } universe={ universePath }
		chamberFromRemote={ transform }>
		{ origins }
	</AvRemoteUniverse>
}

export interface ChamberProps
{
	chamber: ChamberSubscription;
}

export function Chamber( props: ChamberProps )
{
	let members: JSX.Element[] = [];
	console.log( `Render chamber ${ props.chamber.chamberPath } with ${ props.chamber.members.length } members`)
	for( let member of props.chamber.members )
	{
		members.push(
			<ChamberMemberPoses key={ props.chamber.chamberPath + "/" + member.uuid } 
				chamber={ props.chamber } member={ member } />
		);
	}

	return <div key={ props.chamber.chamberPath }>
		{ members }</div>;

}