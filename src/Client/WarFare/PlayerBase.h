// CPlayerBase.h: interface for the CPlayerBase class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PlayerBase_H__B8B8986B_3635_462D_8C38_A052CA75B331__INCLUDED_)
#define AFX_PlayerBase_H__B8B8986B_3635_462D_8C38_A052CA75B331__INCLUDED_

#pragma once

#include "GameBase.h"
#include "GameDef.h"
#include "Bitset.h"
#include <deque>
#include <string>

#include <N3Base/N3Chr.h>

//	By : Ecli666 ( On 2002-07-22 AM 9:59:19 )
//
inline constexpr int SHADOW_SIZE          = 32; // Must be a power of 2..
inline constexpr float SHADOW_PLANE_SIZE  = 4.6f;
inline constexpr uint8_t SHADOW_COLOR     = 10; // A single hexadecimal digit.. alpha
//	~(By Ecli666 On 2002-07-22 AM 9:59:19 )

inline constexpr float TIME_CORPSE_REMAIN = 90.0f; // Time the corpse remains..
inline constexpr float TIME_CORPSE_REMOVE = 10.0f; // Time to fade out and remove..

class CDFont;
class CN3SndObj;

struct __InfoPlayerBase
{
	int iID;          // Unique ID
	std::string szID; // Name
	D3DCOLOR crID;    // Name color..
	e_Race eRace;     // Race based on character skeleton
	e_Nation eNation; // Nation the character belongs to..
	e_Class eClass;   // Class
	int iLevel;       // Level
	int iHPMax;
	int iHP;
	int iMP;
	int iMPMax;
	int iAuthority; // Permission - 0 admin, 1 - normal user, 255 - blocked...
	int iKnightsID; // Clan ID
	int iAllianceID;
	int iKnightsWarEnemyID;

	bool bRenderID; // Whether to render the ID on screen..

	__InfoPlayerBase()
	{
		Init();
	}

	void Init()
	{
		iID = 0;                             // Unique ID
		szID.clear();                        // Name
		crID               = 0;              // Name color..
		eRace              = RACE_UNKNOWN;   // Race based on character skeleton
		eNation            = NATION_UNKNOWN; // Nation the character belongs to..
		eClass             = CLASS_UNKNOWN;  // Class
		iLevel             = 0;              // Level
		iHPMax             = 0;
		iHP                = 0;
		iMP                = 0;
		iMPMax             = 0;
		iAuthority         = 1; // Permission - 0 admin, 1 - normal user, 255 - blocked user...
		iKnightsID         = 0;
		iAllianceID        = 0;
		iKnightsWarEnemyID = 0;
		bRenderID          = true;
	}
};

class CN3ShapeExtra;
class CPlayerBase : public CGameBase
{
	friend class CPlayerOtherMgr;

protected:
	e_PlayerType m_ePlayerType = PLAYER_BASE;    // Player Type ... Base, NPC, OTher, MySelf

	std::deque<e_Ani> m_AnimationDeque;          // Animation queue... anything pushed here is processed in order as ticks run..
	bool m_bAnimationChanged          = false;   // Set only at the moment the queued animation changes..

	CN3Chr m_Chr                      = {};      // Basic character object...
	__TABLE_PLAYER_LOOKS* m_pLooksRef = nullptr; // Base reference table - character resource info, joint positions, sound files, etc..
	__TABLE_ITEM_BASIC* m_pItemPartBasics[PART_POS_COUNT] = {}; // Weapons attached to the character..
	__TABLE_ITEM_EXT* m_pItemPartExts[PART_POS_COUNT]     = {}; // Weapons attached to the character..
	__TABLE_ITEM_BASIC* m_pItemPlugBasics[PLUG_POS_COUNT] = {}; // Weapons attached to the character..
	__TABLE_ITEM_EXT* m_pItemPlugExts[PLUG_POS_COUNT]     = {}; // Weapons attached to the character..

	// ID
	CDFont* m_pClanFont                                   = nullptr;                  // Font used to render clan or knights.. name.. -.-;
	CDFont* m_pIDFont                                     = nullptr;                  // Font used to render the ID.. -.-;
	CDFont* m_pInfoFont                                   = nullptr;                  // Party member recruitment, etc.. other info display..
	CDFont* m_pBalloonFont                                = nullptr;                  // Speech balloon display...
	float m_fTimeBalloon                                  = 0.0f;                     // Speech balloon display time..

	e_StateAction m_eState                                = PSA_BASIC;                // Action state..
	e_StateAction m_eStatePrev                            = PSA_BASIC;                // Previously set action state..
	e_StateAction m_eStateNext                            = PSA_BASIC;                // Previously set action state..
	e_StateMove m_eStateMove                              = PSM_STOP;                 // Moving state..
	e_StateDying m_eStateDying                            = PSD_UNKNOWN;              // How does it die when dying..??
	float m_fTimeDying                                    = 0.0f;                     // Time spent in the dying motion..

	__ColorValue m_cvDuration                             = { 1, 1, 1, 1 };           // Sustained color value
	float m_fDurationColorTimeCur                         = 0.0f;                     // Current time..
	float m_fDurationColorTime                            = 0.0f;                     // Duration..

	float m_fFlickeringFactor                             = 1.0f;                     // Flickering..
	float m_fFlickeringTime                               = 0.0f;                     // Flickering time..

	float m_fRotRadianPerSec                              = DegreesToRadians(270.0f); // Rotation radians per second
	float m_fMoveSpeedPerSec = 0.0f; // Movement value per second.. this is the base value, adjusted according to state (walk, run, backward, curse, etc.)..

	float m_fYawCur          = 0.0f; // Current rotation value..
	float m_fYawToReach      = 0.0f; // Rotates toward this rotation value in Tick..

	float m_fYNext           = 0.0f; // Height value from collision check against objects or terrain..
	float m_fGravityCur      = 0.0f; // Gravity value..

	float m_fScaleToSet      = 1.0f; // Gradual scale value change..
	float m_fScalePrev       = 1.0f;

public:
	CN3ShapeExtra* m_pShapeExtraRef = nullptr;     // If this NPC is an object such as a castle gate or house, set and use this pointer..

	int m_iMagicAni                 = 0;
	int m_iIDTarget                 = -1;          // Target ID...
	int m_iDroppedItemID            = 0;           // Item dropped after death
	bool m_bGuardSuccess            = false;       // Flag for whether the defense succeeded..
	bool m_bVisible                 = true;        // Whether it is visible??

	__InfoPlayerBase m_InfoBase     = {};          // Character info..
	__Vector3 m_vPosFromServer      = {};          // Current position most recently received from the server..

	float m_fTimeAfterDeath         = 0.0f;        // Time elapsed since death - about 5 seconds is appropriate? If attacked before that, it dies immediately.

	float m_fAttackDelta            = 1.0f;        // Attack speed changed by skills or magic.. 1.0 is the base, the larger it is the faster it attacks.
	float m_fMoveDelta              = 1.0f;        // Movement speed changed by skills or magic.. 1.0 is the base, the larger it is the faster it moves.
	__Vector3 m_vDirDying           = { 0, 0, 1 }; // Direction it is pushed when dying..

	//sound..
	bool m_bSoundAllSet             = false;
	CN3SndObj* m_pSnd_Attack_0      = nullptr;
	CN3SndObj* m_pSnd_Move          = nullptr;
	CN3SndObj* m_pSnd_Struck_0      = nullptr;
	CN3SndObj* m_pSnd_Breathe_0     = nullptr;
	CN3SndObj* m_pSnd_Blow          = nullptr;

	float m_fCastFreezeTime         = 0.0f;

	// Functions...
	//	By : Ecli666 ( On 2002-03-29 PM 1:32:12 )
	//
	CBitset m_bitset[SHADOW_SIZE]   = {}; // Used in Quake3.. ^^
	__VertexT1 m_pvVertex[4]        = {};
	uint16_t m_pIndex[6]            = {};
	__VertexT1 m_vTVertex[4]        = {};
	float m_fShaScale               = 1.0f;
	CN3Texture m_N3Tex              = {};
	static CN3SndObj* m_pSnd_MyMove;

	bool IsHostileTarget(const CPlayerBase* rhs) const;

	const __Matrix44 CalcShadowMtxBasicPlane(const __Vector3& vOffs);
	void CalcPart(CN3CPart* pPart, int nLOD, const __Vector3& vLP);
	void CalcPlug(CN3CPlugBase* pPlug, const __Matrix44* pmtxJoint, const __Vector3& vLP);

protected:
	void RenderShadow(float fSunAngle);
	//	~(By Ecli666 On 2002-03-29 PM 1:32:12 )

	virtual bool ProcessAttack(CPlayerBase* pTarget); // Attack routine processing... gets the target pointer, does the collision check, and returns true on collision..

	/// \brief applies any on-hit elemental effects associated with a weapon
	bool TryWeaponElementEffect(e_PlugPosition plugPosition, const CPlayerBase& target, const __Vector3& collisionPosition);

public:
	const __Matrix44* JointMatrixGet(int nJointIndex)
	{
		return m_Chr.MatrixGet(nJointIndex);
	}

	bool JointPosGet(int iJointIdx, __Vector3& vPos);

	e_PlayerType PlayerType() const
	{
		return m_ePlayerType;
	}

	e_Race Race() const
	{
		return m_InfoBase.eRace;
	}

	e_Nation Nation() const
	{
		return m_InfoBase.eNation;
	}

	virtual void SetSoundAndInitFont(uint32_t dwFontFlag = 0U);
	void SetSoundPlug(__TABLE_ITEM_BASIC* pItemBasic);
	void ReleaseSoundAndFont();
	void RegenerateCollisionMesh(); // Find the max/min values again and rebuild the collision mesh..

	// Action state...
	e_StateAction State() const
	{
		return m_eState;
	}

	// Moving state
	e_StateMove StateMove() const
	{
		return m_eStateMove;
	}

	e_ItemClass ItemClass_RightHand() const
	{
		if (m_pItemPlugBasics[PLUG_POS_RIGHTHAND])
			return (e_ItemClass) (m_pItemPlugBasics[PLUG_POS_RIGHTHAND]->byClass); // Item type - right hand

		return ITEM_CLASS_UNKNOWN;
	}

	e_ItemClass ItemClass_LeftHand() const
	{
		if (m_pItemPlugBasics[PLUG_POS_LEFTHAND])
			return (e_ItemClass) (m_pItemPlugBasics[PLUG_POS_LEFTHAND]->byClass); // Item type - left hand

		return ITEM_CLASS_UNKNOWN;
	}

	e_Ani JudgeAnimationBreath();       // Determine the breathing motion... returns a different animation index depending on the held item and whether there is a target.
	e_Ani JudgeAnimationWalk();         // Determine the walk mode... returns a different animation index depending on the held item and whether there is a target.
	e_Ani JudgeAnimationRun();          // Determine the run mode... returns a different animation index depending on the held item and whether there is a target.
	e_Ani JudgeAnimationWalkBackward(); // Determine the walk-backward mode... returns a different animation index depending on the held item and whether there is a target.
	e_Ani JudgeAnimationAttack();       // Determine the attack motion... returns a different animation index depending on the held item.
	e_Ani JudgeAnimationStruck();       // Just distinguishes between NPC and user and returns an animation index
	e_Ani JudgeAnimationGuard();        // Determine the guard motion.  Just distinguishes between NPC and user and returns an animation index
	e_Ani JudgeAnimationDying();        // Just distinguishes between NPC and user and returns an animation index
	e_Ani JudgetAnimationSpellMagic();  // Magic motion

	// Is it dead?
	bool IsDead() const
	{
		return (PSA_DYING == m_eState || PSA_DEATH == m_eState);
	}

	// Is it alive?
	bool IsAlive() const
	{
		return !IsDead();
	}

	// Is it currently moving?
	bool IsMovingNow() const
	{
		return (PSM_WALK == m_eStateMove || PSM_RUN == m_eStateMove || PSM_WALK_BACKWARD == m_eStateMove);
	}

	void AnimationAdd(e_Ani eAni, bool bImmediately);

	void AnimationClear()
	{
		m_AnimationDeque.clear();
	}

	int AnimationCountRemain() const
	{
		return static_cast<int>(m_AnimationDeque.size()) + 1;
	}

	// Set only at the moment the queued animation changes..
	bool IsAnimationChange() const
	{
		return m_bAnimationChanged;
	}

	// Perform the action according to the action table..
	bool Action(e_StateAction eState, bool bLooping, CPlayerBase* pTarget = nullptr, bool bForceSet = false);

	// Move..
	bool ActionMove(e_StateMove eMove);

	// Decide how to die..
	void ActionDying(e_StateDying eSD, const __Vector3& vDir);

	// Rotation value..
	float Yaw() const
	{
		return m_fYawCur;
	}

	float MoveSpeed() const
	{
		return m_fMoveSpeedPerSec;
	}

	const __Vector3& Position() const
	{
		return m_Chr.Pos();
	}

	void PositionSet(const __Vector3& vPos, bool bForcely)
	{
		m_Chr.PosSet(vPos);
		if (bForcely)
			m_fYNext = vPos.y;
	}

	float Distance(const __Vector3& vPos) const
	{
		return (m_Chr.Pos() - vPos).Magnitude();
	}

	__Vector3 Scale() const
	{
		return m_Chr.Scale();
	}

	void ScaleSet(float fScale)
	{
		m_fScaleToSet = m_fScalePrev = fScale;
		m_Chr.ScaleSet(fScale, fScale, fScale);
	}

	// Gradual scale change..
	void ScaleSetGradually(float fScale)
	{
		m_fScaleToSet = fScale;
		m_fScalePrev  = m_Chr.Scale().y;
	}

	__Vector3 Direction() const;

	const __Quaternion& Rotation() const
	{
		return m_Chr.Rot();
	}

	void RotateTo(float fYaw, bool bImmediately);
	void RotateTo(CPlayerBase* pOther); // Looks at this guy.
	float Height() const;
	float Radius() const;
	__Vector3 HeadPosition() const;     // Gets the constantly changing head position..

	__Vector3 RootPosition() const
	{
		if (!m_Chr.m_MtxJoints.empty())
			return m_Chr.m_MtxJoints[0].Pos();
		return __Vector3(0, 0, 0);
	}

	int LODLevel() const
	{
		return m_Chr.m_nLOD;
	}

	__Vector3 Max() const;
	__Vector3 Min() const;
	__Vector3 Center() const;

	void DurationColorSet(const _D3DCOLORVALUE& color, float fDurationTime); // Keeps the set color for the given duration, then returns to the original color.
	void FlickerFactorSet(float fAlpha);

	void InfoStringSet(const std::string& szInfo, D3DCOLOR crFont);
	void BalloonStringSet(const std::string& szBalloon, D3DCOLOR crFont);
	void IDSet(int iID, const std::string& szID, D3DCOLOR crID);
	virtual void KnightsInfoSet(int iID, const std::string& szName, int iGrade, int iRank);

	// The ID is substituted by the name of the Character pointer.
	const std::string& IDString() const
	{
		return m_InfoBase.szID;
	}

	int IDNumber() const
	{
		return m_InfoBase.iID;
	}

	CPlayerBase* TargetPointerCheck(bool bMustAlive);

	////////////////////
	// Collision check functions...
	bool CheckCollisionByBox(const __Vector3& v0, const __Vector3& v1, __Vector3* pVCol, __Vector3* pVNormal);
	bool CheckCollisionToTargetByPlug(CPlayerBase* pTarget, int nPlug, __Vector3* pVCol);

	virtual bool InitChr(__TABLE_PLAYER_LOOKS* pTbl);

	virtual void InitHair()
	{
	}

	virtual void InitFace()
	{
	}

	CN3CPart* Part(e_PartPosition ePos)
	{
		return m_Chr.Part(ePos);
	}

	CN3CPlugBase* Plug(e_PlugPosition ePos)
	{
		return m_Chr.Plug(ePos);
	}

	virtual CN3CPart* PartSet(e_PartPosition ePos, const std::string& szFN, __TABLE_ITEM_BASIC* pItemBasic, __TABLE_ITEM_EXT* pItemExt);
	virtual CN3CPlugBase* PlugSet(e_PlugPosition ePos, const std::string& szFN, __TABLE_ITEM_BASIC* pItemBasic, __TABLE_ITEM_EXT* pItemExt);
	virtual void DurabilitySet(e_ItemSlot eSlot, int iDurability);

	void TickYaw();           // Rotation value processing.
	void TickAnimation();     // Animation processing.
	void TickDurationColor(); // Character color change processing.
	void TickSound();         // Sound processing..

	virtual void Tick();
	virtual void Render(float fSunAngle);
#ifdef _DEBUG
	virtual void RenderCollisionMesh()
	{
		m_Chr.RenderCollisionMesh();
	}
#endif
	void RenderChrInRect(CN3Chr* pChr, const RECT& Rect); // Added by Dino, draws the character inside the specified rectangle.

	void Release() override;

	CPlayerBase();
	~CPlayerBase() override;

	int GetNPCOriginID() const
	{
		if (m_pLooksRef != nullptr)
			return m_pLooksRef->dwID;

		return -1;
	}
};

#endif // !defined(AFX_PlayerBase_H__B8B8986B_3635_462D_8C38_A052CA75B331__INCLUDED_)
