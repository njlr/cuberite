
#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#include "RedstoneSimulator.h"
#include "../Piston.h"
#include "../World.h"
#include "../BlockID.h"
#include "../Torch.h"





cRedstoneSimulator::cRedstoneSimulator( cWorld* a_World )
	: super(a_World)
{
}





cRedstoneSimulator::~cRedstoneSimulator()
{
}





void cRedstoneSimulator::WakeUp( int a_X, int a_Y, int a_Z )
{
	cCSLock Lock( m_CS );
	m_Blocks.push_back( Vector3i( a_X, a_Y, a_Z ) );
}





void cRedstoneSimulator::Simulate( float a_Dt )
{
	// Toggle torches on/off
	while( !m_RefreshTorchesAround.empty() )
	{
		Vector3i pos = m_RefreshTorchesAround.front();
		m_RefreshTorchesAround.pop_front();

		RefreshTorchesAround( pos );
	}

	// Set repeaters to correct values, and decrement ticks
	for( RepeaterList::iterator itr = m_SetRepeaters.begin(); itr != m_SetRepeaters.end(); )
	{
		(*itr).Ticks--;
		if( (*itr).Ticks <= 0 )
		{
			char Block = m_World->GetBlock( (*itr).Position );
			if ((*itr).bPowerOn && (Block == E_BLOCK_REDSTONE_REPEATER_OFF))
			{
				m_World->FastSetBlock( (*itr).Position.x, (*itr).Position.y, (*itr).Position.z, E_BLOCK_REDSTONE_REPEATER_ON, m_World->GetBlockMeta( (*itr).Position ) );
				m_Blocks.push_back( (*itr).Position );
			}
			else if (!(*itr).bPowerOn && (Block == E_BLOCK_REDSTONE_REPEATER_ON))
			{
				m_World->FastSetBlock( (*itr).Position.x, (*itr).Position.y, (*itr).Position.z, E_BLOCK_REDSTONE_REPEATER_OFF, m_World->GetBlockMeta( (*itr).Position ) );
				m_Blocks.push_back( (*itr).Position );
			}

			if( (*itr).bPowerOffNextTime )
			{
				(*itr).bPowerOn = false;
				(*itr).bPowerOffNextTime = false;
				(*itr).Ticks = 10; // TODO: Look up actual ticks from block metadata
				++itr;
			}
			else
			{
				itr = m_SetRepeaters.erase( itr );
			}
		}
		else
		{
			++itr;
		}
	}

	// Handle changed blocks
	{
		cCSLock Lock( m_CS );
		std::swap( m_Blocks, m_BlocksBuffer );
	}
	for( BlockList::iterator itr = m_BlocksBuffer.begin(); itr != m_BlocksBuffer.end(); ++itr )
	{
		HandleChange( *itr );
	}
	m_BlocksBuffer.clear();
}





void cRedstoneSimulator::RefreshTorchesAround( const Vector3i & a_BlockPos )
{
	static Vector3i Surroundings [] = {
		Vector3i(-1, 0, 0),
		Vector3i( 1, 0, 0),
		Vector3i( 0, 0,-1),
		Vector3i( 0, 0, 1),
		Vector3i( 0, 1, 0), // Also toggle torch on top
	};
	char TargetBlockID = E_BLOCK_REDSTONE_TORCH_ON;
	char TargetRepeaterID = E_BLOCK_REDSTONE_REPEATER_OFF;
	if( IsPowered( a_BlockPos, true ) )
	{
		TargetBlockID = E_BLOCK_REDSTONE_TORCH_OFF;
		TargetRepeaterID = E_BLOCK_REDSTONE_REPEATER_ON;
		//if( m_World->GetBlock( a_BlockPos ) == E_BLOCK_DIRT )
		//{
		//	m_World->FastSetBlock( a_BlockPos.x, a_BlockPos.y, a_BlockPos.z, E_BLOCK_STONE, 0 );
		//}
	}
	else
	{
		//if( m_World->GetBlock( a_BlockPos ) == E_BLOCK_STONE )
		//{
		//	m_World->FastSetBlock( a_BlockPos.x, a_BlockPos.y, a_BlockPos.z, E_BLOCK_DIRT, 0 );
		//}
	}

	for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
	{
		Vector3i TorchPos = a_BlockPos + Surroundings[i];
		const char Block = m_World->GetBlock( TorchPos );
		switch( Block )
		{
		case E_BLOCK_REDSTONE_TORCH_ON:
		case E_BLOCK_REDSTONE_TORCH_OFF:
			if( Block != TargetBlockID )
			{
				char TorchMeta = m_World->GetBlockMeta( TorchPos );
				if( cTorch::IsAttachedTo( TorchPos, TorchMeta, a_BlockPos ) )
				{
					m_World->FastSetBlock( TorchPos.x, TorchPos.y, TorchPos.z, TargetBlockID, m_World->GetBlockMeta( TorchPos ) );
					m_Blocks.push_back( TorchPos );
				}
			}
			break;
		case E_BLOCK_REDSTONE_REPEATER_ON:
		case E_BLOCK_REDSTONE_REPEATER_OFF:
			if ((Block != TargetRepeaterID) && IsRepeaterPointingAway( TorchPos, m_World->GetBlockMeta( TorchPos ), a_BlockPos ) )
			{
				SetRepeater( TorchPos, 10, TargetRepeaterID == E_BLOCK_REDSTONE_REPEATER_ON );
			}
			break;
		default:
			break;
		};
	}
}





void cRedstoneSimulator::HandleChange( const Vector3i & a_BlockPos )
{
	std::deque< Vector3i > SpreadStack;

	Vector3i Surroundings[] = {
		Vector3i( 1, 0, 0 ),
		Vector3i( 1, 1, 0 ),
		Vector3i( 1,-1, 0 ),
		Vector3i(-1, 0, 0 ),
		Vector3i(-1, 1, 0 ),
		Vector3i(-1,-1, 0 ),
		Vector3i( 0, 0, 1 ),
		Vector3i( 0, 1, 1 ),
		Vector3i( 0,-1, 1 ),
		Vector3i( 0, 0,-1 ),
		Vector3i( 0, 1,-1 ),
		Vector3i( 0,-1,-1 ),
		Vector3i( 0,-1, 0 ),
	};

	char Block = m_World->GetBlock( a_BlockPos );

	// First check whether torch should be on or off
	if( Block == E_BLOCK_REDSTONE_TORCH_ON || Block == E_BLOCK_REDSTONE_TORCH_OFF )
	{
		static Vector3i Surroundings [] = {
			Vector3i(-1, 0, 0),
			Vector3i( 1, 0, 0),
			Vector3i( 0, 0,-1),
			Vector3i( 0, 0, 1),
			Vector3i( 0,-1, 0),
		};
		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i pos = a_BlockPos + Surroundings[i];
			char OtherBlock = m_World->GetBlock( pos );
			if( (OtherBlock != E_BLOCK_AIR) && (OtherBlock != E_BLOCK_REDSTONE_TORCH_ON) && (OtherBlock != E_BLOCK_REDSTONE_TORCH_OFF) )
			{
				RefreshTorchesAround( pos );
			}
		}
		Block = m_World->GetBlock( a_BlockPos ); // Make sure we got the updated block
	}
	else if( Block == E_BLOCK_REDSTONE_REPEATER_ON || Block == E_BLOCK_REDSTONE_REPEATER_OFF ) // Check if repeater is powered by a 'powered block' (not wires/torch)
	{
		Vector3i Direction = GetRepeaterDirection( m_World->GetBlockMeta( a_BlockPos ) );
		Vector3i pos = a_BlockPos - Direction; // NOTE: It's minus Direction
		char OtherBlock = m_World->GetBlock( pos );
		if( (OtherBlock != E_BLOCK_AIR) && (OtherBlock != E_BLOCK_REDSTONE_TORCH_ON) && (OtherBlock != E_BLOCK_REDSTONE_TORCH_OFF) && (OtherBlock != E_BLOCK_REDSTONE_WIRE) )
		{
			RefreshTorchesAround( pos );
		}
		else
		{
			SetRepeater( a_BlockPos, 10, IsPowered( a_BlockPos, false ) );
		}
		Block = m_World->GetBlock( a_BlockPos );
	}

	BlockList Sources;
	// If torch is still on, use it as a source
	if( Block == E_BLOCK_REDSTONE_TORCH_ON)
	{
		Sources.push_back( a_BlockPos );
	}
	else if( Block == E_BLOCK_REDSTONE_REPEATER_ON )
	{
		static Vector3i Surroundings [] = { // It only spreads right in front, and one block up
			Vector3i( 0, 0, 0),
			Vector3i( 0, 1, 0),
		};
		Vector3i Direction = GetRepeaterDirection( m_World->GetBlockMeta( a_BlockPos ) );
		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i pos = a_BlockPos + Direction + Surroundings[i];
			if( PowerBlock( pos, a_BlockPos, 0xf ) )
			{
				SpreadStack.push_back( pos );
			}
		}
	}
	else if ( Block == E_BLOCK_LEVER )
	{ 
		//Adding lever to the source queue
		if (cRedstoneSimulator::IsLeverOn(m_World,a_BlockPos))
			Sources.push_back( a_BlockPos );
	}

	// Power all blocks legally connected to the sources
	if( Block != E_BLOCK_REDSTONE_REPEATER_ON )
	{
		BlockList NewSources = RemoveCurrent( a_BlockPos );
		Sources.insert( Sources.end(), NewSources.begin(), NewSources.end() );
		while( !Sources.empty() )
		{
			Vector3i SourcePos = Sources.back();
			Sources.pop_back();

			char Block = m_World->GetBlock( SourcePos );
			switch( Block )
			{
			case E_BLOCK_LEVER: //Treating lever as a torch
			case E_BLOCK_REDSTONE_TORCH_OFF:
			case E_BLOCK_REDSTONE_TORCH_ON:
				{
					static Vector3i Surroundings [] = {
						Vector3i(-1, 0, 0),
						Vector3i( 1, 0, 0),
						Vector3i( 0, 0,-1),
						Vector3i( 0, 0, 1),
					};
					for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
					{
						Vector3i OtherPos =  SourcePos + Surroundings[i];
						if( PowerBlock( OtherPos, a_BlockPos, 0xf ) )
						{
							SpreadStack.push_back( OtherPos ); // Changed, so add to stack
						}
					}
				}
				break;
			case E_BLOCK_REDSTONE_REPEATER_OFF:
			case E_BLOCK_REDSTONE_REPEATER_ON:
				{
					static Vector3i Surroundings [] = {
						Vector3i( 0, 0, 0),
						Vector3i( 0, 1, 0),
					};
					Vector3i Direction = GetRepeaterDirection( m_World->GetBlockMeta( SourcePos ) );
					for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
					{
						Vector3i pos = SourcePos + Direction + Surroundings[i];
						if( PowerBlock( pos, a_BlockPos, 0xf ) )
						{
							SpreadStack.push_back( pos );
						}
					}
				}
				break;
			};

			
		}
	}


	// Do a floodfill
	while( !SpreadStack.empty() )
	{
		Vector3i pos = SpreadStack.back();
		SpreadStack.pop_back();

		char Meta = m_World->GetBlockMeta( pos );

		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i OtherPos =  pos + Surroundings[i];
			if( PowerBlock( OtherPos, pos, Meta-1 ) )
			{
				SpreadStack.push_back( OtherPos ); // Changed, so add to stack
			}
		}
	}

	// Only after a redstone area has been completely simulated the redstone entities can react
	while( !m_RefreshPistons.empty() )
	{
		Vector3i pos = m_RefreshPistons.back();
		m_RefreshPistons.pop_back();

		char Block = m_World->GetBlock( pos );
		switch( Block )
		{
		case E_BLOCK_PISTON:
		case E_BLOCK_STICKY_PISTON:
			if( IsPowered( pos ) )
			{
				cPiston Piston( m_World );
				Piston.ExtendPiston( pos.x, pos.y, pos.z );
			}
			else
			{
				cPiston Piston( m_World );
				Piston.RetractPiston( pos.x, pos.y, pos.z );
			}
			break;
		default:
			break;
		};
	}
}





bool cRedstoneSimulator::PowerBlock( const Vector3i & a_BlockPos, const Vector3i & a_FromBlock, char a_Power )
{
	char Block = m_World->GetBlock( a_BlockPos );
	switch( Block )
	{
	case E_BLOCK_REDSTONE_WIRE:
		{
			if( m_World->GetBlockMeta( a_BlockPos ) < a_Power )
			{
				m_World->SetBlockMeta( a_BlockPos, a_Power );
				return true;
			}
		}
		break;
	case E_BLOCK_PISTON:
	case E_BLOCK_STICKY_PISTON:
		{
			m_RefreshPistons.push_back( a_BlockPos );
		}
		break;
	case E_BLOCK_REDSTONE_REPEATER_OFF:
	case E_BLOCK_REDSTONE_REPEATER_ON:
		{
			if (IsRepeaterPointingAway( a_BlockPos, m_World->GetBlockMeta( a_BlockPos ), a_FromBlock ))
			{
				SetRepeater( a_BlockPos, 10, true );
			}
		}
		break;
	default:
		{
			if (
				(Block != E_BLOCK_AIR) &&
				(Block != E_BLOCK_REDSTONE_TORCH_ON) &&
				(Block != E_BLOCK_REDSTONE_TORCH_OFF) &&
				(Block != E_BLOCK_LEVER)  //Treating lever as a torch, for refreshing
			)
			{
				if( IsPowered( a_BlockPos, true ) )
				{
					m_RefreshTorchesAround.push_back( a_BlockPos );
				}
			}
		}
		break;
	};

	return false;
}





int cRedstoneSimulator::UnPowerBlock( const Vector3i & a_BlockPos, const Vector3i & a_FromBlock)
{
	BLOCKTYPE BlockType = m_World->GetBlock(a_BlockPos);
	switch (BlockType)
	{
		case E_BLOCK_REDSTONE_WIRE:
		{
			if( m_World->GetBlockMeta( a_BlockPos ) > 0 )
			{
				m_World->SetBlockMeta( a_BlockPos, 0 );
				return 1;
			}
			break;
		}
		
		case E_BLOCK_PISTON:
		case E_BLOCK_STICKY_PISTON:
		{
			m_RefreshPistons.push_back( a_BlockPos );
			break;
		}

		case E_BLOCK_REDSTONE_TORCH_ON:
		{
			return 2;
			break;
		}
		case E_BLOCK_LEVER:
		{
			//Check if lever is ON. If it is, report it back as a source
			if (cRedstoneSimulator::IsLeverOn(m_World,a_BlockPos))
			{
				return 2;
			}
			break;
		}
		
		case E_BLOCK_REDSTONE_REPEATER_ON:
		{
			if (
				IsRepeaterPointingTo(a_BlockPos, m_World->GetBlockMeta(a_BlockPos), a_FromBlock ) ||  // Repeater is next to wire
				IsRepeaterPointingTo(a_BlockPos, m_World->GetBlockMeta(a_BlockPos), a_FromBlock - Vector3i(0, 1, 0))  // Repeater is below wire
			)
			{
				return 2;
			}
			else if (IsRepeaterPointingAway( a_BlockPos, m_World->GetBlockMeta( a_BlockPos ), a_FromBlock ) )
			{
				SetRepeater( a_BlockPos, 10, false );
			}
			// fall-through:
		}
		
		case E_BLOCK_REDSTONE_REPEATER_OFF:
		{
			if (IsRepeaterPointingAway( a_BlockPos, m_World->GetBlockMeta( a_BlockPos ), a_FromBlock ))
			{
				SetRepeater( a_BlockPos, 10, false );
			}
			break;
		}
		
		default:
		{
			if ((BlockType != E_BLOCK_AIR) && (BlockType != E_BLOCK_REDSTONE_TORCH_ON) && (BlockType != E_BLOCK_REDSTONE_TORCH_OFF) && (BlockType != E_BLOCK_LEVER)) //Treating lever as a torch
			{
				if (!IsPowered(a_BlockPos, true))
				{
					m_RefreshTorchesAround.push_back( a_BlockPos );
				}
			}
			break;
		}
	}  // switch (BlockType)

	return 0;
}





// Removes current from all powered redstone wires until it reaches an energy source.
// Also returns all energy sources it encountered
cRedstoneSimulator::BlockList cRedstoneSimulator::RemoveCurrent( const Vector3i & a_BlockPos )
{


	std::deque< Vector3i > SpreadStack;
	std::deque< Vector3i > FoundSources;

	Vector3i Surroundings[] = {
		Vector3i( 1, 0, 0 ),
		Vector3i( 1, 1, 0 ),
		Vector3i( 1,-1, 0 ),
		Vector3i(-1, 0, 0 ),
		Vector3i(-1, 1, 0 ),
		Vector3i(-1,-1, 0 ),
		Vector3i( 0, 0, 1 ),
		Vector3i( 0, 1, 1 ),
		Vector3i( 0,-1, 1 ),
		Vector3i( 0, 0,-1 ),
		Vector3i( 0, 1,-1 ),
		Vector3i( 0,-1,-1 ),
		Vector3i( 0,-1, 0 ),
	};

	char Block = m_World->GetBlock( a_BlockPos );
	if (
		(Block == E_BLOCK_REDSTONE_REPEATER_ON) ||
		(Block == E_BLOCK_REDSTONE_REPEATER_OFF)
	)
	{
		static Vector3i Surroundings [] = { // Repeaters only spread right in front and 1 block up
			Vector3i( 0, 0, 0),
			Vector3i( 0, 1, 0),
		};
		Vector3i Direction = GetRepeaterDirection( m_World->GetBlockMeta( a_BlockPos ) );
		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i pos = a_BlockPos + Direction + Surroundings[i];
			int RetVal = UnPowerBlock( pos, a_BlockPos );
			if( RetVal == 1 )
			{
				SpreadStack.push_back( pos ); // Changed, so add to stack
			}
			else if( RetVal == 2 )
			{
				FoundSources.push_back( pos );
			}
		}
	}
	else if (
		(Block == E_BLOCK_REDSTONE_TORCH_OFF) ||
		(Block == E_BLOCK_REDSTONE_TORCH_ON) ||
		(Block == E_BLOCK_LEVER)  // Treating lever as a torch
	)
	{
		static Vector3i Surroundings [] = {	// Torches only spread on the same level
			Vector3i(-1, 0, 0),
			Vector3i( 1, 0, 0),
			Vector3i( 0, 0,-1),
			Vector3i( 0, 0, 1),
		};

		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i pos = Vector3i( a_BlockPos ) + Surroundings[i];
			int RetVal = UnPowerBlock( pos, a_BlockPos );
			if( RetVal == 1 )
			{
				SpreadStack.push_back( pos ); // Changed, so add to stack
			}
			else if( RetVal == 2 )
			{
				FoundSources.push_back( pos );
			}
		}
	}
	else
	{
		SpreadStack.push_back( a_BlockPos );
	}


	while( !SpreadStack.empty() )
	{
		Vector3i pos = SpreadStack.back();
		SpreadStack.pop_back();

		for( unsigned int i = 0; i < ARRAYCOUNT( Surroundings ); ++i )
		{
			Vector3i OtherPos =  pos + Surroundings[i];
			int RetVal = UnPowerBlock( OtherPos, pos );
			if( RetVal == 1 )
			{
				SpreadStack.push_back( OtherPos ); // Changed, so add to stack
			}
			else if( RetVal == 2 )
			{
				FoundSources.push_back( OtherPos );
			}
		}
	}

	return FoundSources;
}





bool cRedstoneSimulator::IsPowering( const Vector3i & a_PowerPos, const Vector3i & a_BlockPos, eRedstoneDirection a_WireDirection, bool a_bOnlyByWire )
{
	BLOCKTYPE PowerBlock = m_World->GetBlock( a_PowerPos );
	if (
		!a_bOnlyByWire && (
			(PowerBlock == E_BLOCK_REDSTONE_TORCH_ON) ||
			(PowerBlock == E_BLOCK_LEVER)
		)
	)
	{
		return true;
	}
	if (PowerBlock == E_BLOCK_REDSTONE_REPEATER_ON ) // A repeater pointing towards block is regarded as wire
	{
		if (IsRepeaterPointingTo( a_PowerPos, m_World->GetBlockMeta( a_PowerPos ), a_BlockPos ) )
		{
			return true;
		}
	}
	if (PowerBlock == E_BLOCK_REDSTONE_WIRE)
	{
		if (m_World->GetBlockMeta(a_PowerPos) > 0)
		{
			if (GetDirection(a_PowerPos) == a_WireDirection)
			{
				return true;
			}
		}
	}

	return false;
}





bool cRedstoneSimulator::IsPowered( const Vector3i & a_BlockPos, bool a_bOnlyByWire /* = false */ )
{
	BLOCKTYPE Block = m_World->GetBlock( a_BlockPos );
	if ((Block == E_BLOCK_REDSTONE_REPEATER_OFF) || (Block == E_BLOCK_REDSTONE_REPEATER_ON))
	{
		Vector3i Behind = a_BlockPos - GetRepeaterDirection( m_World->GetBlockMeta( a_BlockPos ) );
		BLOCKTYPE BehindBlock = m_World->GetBlock( Behind );
		if ((BehindBlock == E_BLOCK_REDSTONE_TORCH_ON) || (BehindBlock == E_BLOCK_LEVER))
		{
			return true;
		}
		if (BehindBlock == E_BLOCK_REDSTONE_WIRE)
		{
			if( m_World->GetBlockMeta( Behind ) > 0 )
			{
				return true;
			}
		}
		if (BehindBlock == E_BLOCK_REDSTONE_REPEATER_ON)
		{
			if (IsRepeaterPointingTo( Behind, m_World->GetBlockMeta( Behind ), a_BlockPos))
			{
				return true;
			}
		}
		return false;
	}

	if( IsPowering( Vector3i( a_BlockPos.x-1, a_BlockPos.y, a_BlockPos.z ), a_BlockPos, REDSTONE_X_POS, a_bOnlyByWire ) )
		return true;
	if( IsPowering( Vector3i( a_BlockPos.x+1, a_BlockPos.y, a_BlockPos.z ), a_BlockPos, REDSTONE_X_NEG, a_bOnlyByWire ) )
		return true;
	if( IsPowering( Vector3i( a_BlockPos.x, a_BlockPos.y, a_BlockPos.z-1 ), a_BlockPos, REDSTONE_Z_POS, a_bOnlyByWire ) )
		return true;
	if( IsPowering( Vector3i( a_BlockPos.x, a_BlockPos.y, a_BlockPos.z+1 ), a_BlockPos, REDSTONE_Z_NEG, a_bOnlyByWire ) )
		return true;

	// Only wires can power the bottom block
	char PosY = m_World->GetBlock( a_BlockPos.x, a_BlockPos.y+1, a_BlockPos.z );
	if( PosY == E_BLOCK_REDSTONE_WIRE )
	{
		if( m_World->GetBlockMeta( a_BlockPos.x, a_BlockPos.y+1, a_BlockPos.z ) > 0 )
		{
			return true;
		}
	}

	return false;
}




// Believe me, it works!! TODO: Add repeaters and low/high wires
cRedstoneSimulator::eRedstoneDirection cRedstoneSimulator::GetDirection( int a_X, int a_Y, int a_Z )
{
	int Dir = REDSTONE_NONE;

	char NegX = m_World->GetBlock( a_X-1, a_Y, a_Z );
	if (
		(NegX == E_BLOCK_REDSTONE_WIRE) ||
		(NegX == E_BLOCK_REDSTONE_TORCH_ON) ||
		(NegX == E_BLOCK_LEVER)
	)
	{
		Dir |= (REDSTONE_X_POS);
	}
	char PosX = m_World->GetBlock( a_X+1, a_Y, a_Z );
	if (
		(PosX == E_BLOCK_REDSTONE_WIRE) ||
		(PosX == E_BLOCK_REDSTONE_TORCH_ON) ||
		(PosX == E_BLOCK_LEVER)
	)
	{
		Dir |= (REDSTONE_X_NEG);
	}
	char NegZ = m_World->GetBlock( a_X, a_Y, a_Z-1 );
	if (
		(NegZ == E_BLOCK_REDSTONE_WIRE) ||
		(NegZ == E_BLOCK_REDSTONE_TORCH_ON) ||
		(NegZ == E_BLOCK_LEVER)
	)
	{
		if( (Dir & REDSTONE_X_POS) && !(Dir & REDSTONE_X_NEG ) ) //corner
		{
			Dir ^= REDSTONE_X_POS;
			Dir |= REDSTONE_X_NEG;
		}
		if( (Dir & REDSTONE_X_NEG) && !(Dir & REDSTONE_X_POS ) ) //corner
		{
			Dir ^= REDSTONE_X_NEG;
			Dir |= REDSTONE_X_POS;
		}
		Dir |= REDSTONE_Z_POS;
	}
	char PosZ = m_World->GetBlock( a_X, a_Y, a_Z+1 );
	if (
		(PosZ == E_BLOCK_REDSTONE_WIRE) ||
		(PosZ == E_BLOCK_REDSTONE_TORCH_ON) ||
		(PosZ == E_BLOCK_LEVER)
	)
	{
		if( (Dir & REDSTONE_X_POS) && !(Dir & REDSTONE_X_NEG ) ) //corner
		{
			Dir ^= REDSTONE_X_POS;
			Dir |= REDSTONE_X_NEG;
		}
		if( (Dir & REDSTONE_X_NEG) && !(Dir & REDSTONE_X_POS ) ) //corner
		{
			Dir ^= REDSTONE_X_NEG;
			Dir |= REDSTONE_X_POS;
		}
		Dir |= REDSTONE_Z_NEG;
	}

	return (eRedstoneDirection)Dir;
}





bool cRedstoneSimulator::IsRepeaterPointingTo(const Vector3i & a_RepeaterPos, char a_MetaData, const Vector3i & a_BlockPos)
{
	switch (a_MetaData & 0x3)
	{
		case 0x0:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 0, 0, 1 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x1:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i(-1, 0, 0 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x2:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 0, 0,-1 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x3:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 1, 0, 0 ) ) )
			{
				return true;
			}
			break;
		}
	}
	return false;
}





bool cRedstoneSimulator::IsRepeaterPointingAway( const Vector3i & a_RepeaterPos, char a_MetaData, const Vector3i & a_BlockPos )
{
	switch (a_MetaData & 0x3)
	{
		case 0x0:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 0, 0,-1 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x1:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 1, 0, 0 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x2:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i( 0, 0, 1 ) ) )
			{
				return true;
			}
			break;
		}
		
		case 0x3:
		{
			if( (a_RepeaterPos - a_BlockPos).Equals( Vector3i(-1, 0, 0 ) ) )
			{
				return true;
			}
			break;
		}
	}
	return false;
}





NIBBLETYPE cRedstoneSimulator::RepeaterRotationToMetaData(float a_Rotation)
{
	a_Rotation += 90 + 45; // So its not aligned with axis
	if (a_Rotation > 360.f)
	{
		a_Rotation -= 360.f;
	}
	
	if ((a_Rotation >= 0.f) && (a_Rotation < 90.f))
	{
		return 0x1;
	}
	else if ((a_Rotation >= 180) && (a_Rotation < 270))
	{
		return 0x3;
	}
	else if ((a_Rotation >= 90) && (a_Rotation < 180))
	{
		return 0x2;
	}
	else
	{
		return 0x0;
	}
}





Vector3i cRedstoneSimulator::GetRepeaterDirection(NIBBLETYPE a_MetaData)
{
	switch (a_MetaData & 0x3)
	{
		case 0x0: return Vector3i( 0, 0,-1);
		case 0x1: return Vector3i( 1, 0, 0);
		case 0x2: return Vector3i( 0, 0, 1);
		case 0x3: return Vector3i(-1, 0, 0);
	}
	return Vector3i();
}





NIBBLETYPE cRedstoneSimulator::LeverDirectionToMetaData(NIBBLETYPE a_Dir)
{
	// Determine lever direction.
	switch (a_Dir)
	{
		case BLOCK_FACE_TOP:    return 0x6;
		case BLOCK_FACE_EAST:   return 0x1;
		case BLOCK_FACE_WEST:   return 0x2;
		case BLOCK_FACE_SOUTH:  return 0x3;
		case BLOCK_FACE_NORTH:  return 0x4;
		case BLOCK_FACE_BOTTOM: return 0x0;
		default:                return 0x6;
	}
}





bool cRedstoneSimulator::IsLeverOn (cWorld *a_World, const Vector3i & a_BlockPos)
{
	// Extract the ON bit from metadata and return if true if it is set
	return ((a_World->GetBlockMeta(a_BlockPos) & 0x8) == 0x8);
}





void cRedstoneSimulator::SetRepeater( const Vector3i & a_Position, int a_Ticks, bool a_bPowerOn )
{
	for( RepeaterList::iterator itr = m_SetRepeaters.begin(); itr != m_SetRepeaters.end(); ++itr )
	{
		sRepeaterChange & Change = *itr;
		if( Change.Position.Equals( a_Position ) )
		{
			if (Change.bPowerOn && !a_bPowerOn)
			{
				Change.bPowerOffNextTime = true;
			}
			if (a_bPowerOn)
			{
				Change.bPowerOffNextTime = false;
			}
			Change.bPowerOn |= a_bPowerOn;
			return;
		}
	}

	sRepeaterChange RC;
	RC.Position = a_Position;
	RC.Ticks = a_Ticks;
	RC.bPowerOn = a_bPowerOn;
	RC.bPowerOffNextTime = false;
	m_SetRepeaters.push_back( RC );
}




