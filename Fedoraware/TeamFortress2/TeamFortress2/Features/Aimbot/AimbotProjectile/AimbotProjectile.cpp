#include "AimbotProjectile.h"
#include "../../Vars.h"
#include "../MovementSimulation/MovementSimulation.h"
#include "../../Visuals/Visuals.h"

Vec3 CAimbotProjectile::Predictor_t::Extrapolate(float time)
{
	G::LinearPredLine = m_vPosition;

	Vec3 vecOut;
	if (m_pEntity->IsOnGround())
	{
		vecOut = (m_vPosition + (m_vVelocity * time));
	}
	else
	{
		vecOut = (m_vPosition + (m_vVelocity * time) - m_vAcceleration * 0.5f * time * time);
	}

	return vecOut;
}

/* Returns the projectile info of a given weapon */
bool CAimbotProjectile::GetProjectileInfo(CBaseCombatWeapon* pWeapon, ProjectileInfo_t& out)
{
	switch (pWeapon->GetWeaponID())
	{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		{
			IsBoosted = true;
			out = { Utils::ATTRIB_HOOK_FLOAT(1100.0f, "mult_projectile_speed", pWeapon), 0.0f };
			break;
		}

		case TF_WEAPON_GRENADELAUNCHER:
		{
			bool isLochnLoad = G::CurItemDefIndex == Demoman_m_TheLochnLoad;
			float speed = isLochnLoad ? 1490.0f : 1200.0f;

			IsBoosted = true;
			out = { speed, 0.5f, Utils::ATTRIB_HOOK_FLOAT(3.0f, "fuse_mult", pWeapon) };
			break;

		}

		case TF_WEAPON_PIPEBOMBLAUNCHER:
		{
			float charge = (I::GlobalVars->curtime - pWeapon->GetChargeBeginTime());
			float speed = Math::RemapValClamped(charge, 0.0f, Utils::ATTRIB_HOOK_FLOAT(4.0f, "stickybomb_charge_rate", pWeapon), 900.0f, 2400.0f);
			float grav_mod = Math::RemapValClamped(charge, 0.0f, Utils::ATTRIB_HOOK_FLOAT(4.0f, "stickybomb_charge_rate", pWeapon), 0.5f, 0.1f);

			out = { speed, grav_mod };
			break;
		}

		case TF_WEAPON_CANNON:
		{
			IsBoosted = true;
			out = { 1454.0f, 0.5f };
			break;
		}

		case TF_WEAPON_FLAREGUN:
		{
			out = { 2000.0f, 0.2f };
			break;
		}

		case TF_WEAPON_CLEAVER:
		case TF_WEAPON_RAYGUN_REVENGE:
		{
			out = { 3000.0f, 0.45f };
			break;
		}

		case TF_WEAPON_COMPOUND_BOW:
		{
			float charge = (I::GlobalVars->curtime - pWeapon->GetChargeBeginTime());
			float speed = Math::RemapValClamped(charge, 0.0f, 1.0f, 1800.0f, 2600.0f);
			float grav_mod = Math::RemapValClamped(charge, 0.0f, 1.0f, 0.5f, 0.1f);
			
			out = { speed, grav_mod };
			break;
		}

		case TF_WEAPON_SYRINGEGUN_MEDIC:
		{
			out = { 1000.0f, 0.2f };
			break;
		}

		case TF_WEAPON_FLAMETHROWER:
		{
			out = { 1000.0f, 0.0f, 0.33f };
			IsFlameThrower = true;
			break;
		}

		case TF_WEAPON_FLAME_BALL: //dragon's fury
		{
			out = { 3000.0f, 0.0f, 0.1753f };
			IsFlameThrower = true;
			break;
		}

		case TF_WEAPON_RAYGUN:
		{
			out = { 1200.0f, 0.0f };
			break;
		}

		case TF_WEAPON_CROSSBOW:
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		{
			out = { 2400.0f, 0.2f };
			break;
		}
	}

	return out.m_flVelocity;
}

bool CAimbotProjectile::CalcProjAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, const ProjectileInfo_t& projInfo, Solution_t& out)
{
	const float fGravity = g_ConVars.sv_gravity->GetFloat() * projInfo.m_flGravity;
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float fHyp = sqrt(vDelta.x * vDelta.x + vDelta.y * vDelta.y);
	const float fDist = vDelta.z;
	const float fVel = projInfo.m_flVelocity;

	if (!fGravity)
	{
		const Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
		out.m_flPitch = -DEG2RAD(vAngleTo.x);
		out.m_flYaw = DEG2RAD(vAngleTo.y);
	}
	else
	{	//	arch
		const float fRoot = pow(fVel, 4) - fGravity * (fGravity * pow(fHyp, 2) + 2.f * fDist * pow(fVel, 2));
		if (fRoot < 0.f)
		{
			return false;
		}
		out.m_flPitch = atan((pow(fVel, 2) - sqrt(fRoot)) / (fGravity * fHyp));
		out.m_flYaw = atan2(vDelta.y, vDelta.x);
	}
	out.m_flTime = fHyp / (cos(out.m_flPitch) * fVel);

	return true;
}

bool CAimbotProjectile::SolveProjectile(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd, Predictor_t& predictor, const ProjectileInfo_t& projInfo, Solution_t& out)
{
	const INetChannel* pNetChannel = I::EngineClient->GetNetChannelInfo();
	ConVar* cl_flipviewmodels = g_ConVars.cl_flipviewmodels;

	G::PredictionLines.clear();

	if (!G::WeaponCanAttack)
	{
		return true; // we can't attack, so it shouldn't matter if we say we've solved it, also shouldn't f wit da chammies iykyk
	}

	if (!pNetChannel || !cl_flipviewmodels)
	{
		return false;
	}

	bool oValue = cl_flipviewmodels->GetBool(); // assume false
	if (Vars::Debug::DebugInfo.Value)
	{
		cl_flipviewmodels->SetValue(oValue);
	}

	Ray_t traceRay = {};
	CGameTrace trace = {};
	static CTraceFilterWorldAndPropsOnly traceFilter = {};
	traceFilter.pSkip = predictor.m_pEntity;

	Vec3 vLocalPos = pLocal->GetEyePosition();
	const float maxTime = predictor.m_pEntity->IsPlayer()
		? (projInfo.m_flMaxTime == 0.f ? Vars::Aimbot::Projectile::PredictionTime.Value : projInfo.m_flMaxTime)
		: (projInfo.m_flMaxTime == 0.f ? 1024.f : projInfo.m_flMaxTime);
	const float fLatency = pNetChannel->GetLatency(FLOW_OUTGOING) + pNetChannel->GetLatency(FLOW_INCOMING) + G::LerpTime;	//	correct

	/*
			This should now be able to predict anything that moves.
			Should also stop wasting time predicting static players.
	*/
	const bool useTPred = !predictor.m_pEntity->GetVecVelocity().IsZero() ? true : false;

	if (!useTPred)
	{
		std::optional<Vec3> staticPos = predictor.m_pEntity->IsPlayer() ? GetAimPos(pLocal, predictor.m_pEntity, predictor.m_vPosition) : GetAimPosBuilding(pLocal, predictor.m_pEntity);
		if (!staticPos)
		{
			return false;
		}

		// get angle offsets for demoman weapons?weew
		switch (pWeapon->GetWeaponID())
		{
			case TF_WEAPON_GRENADELAUNCHER:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
			case TF_WEAPON_STICKBOMB:
			case TF_WEAPON_STICKY_BALL_LAUNCHER:
			{
				Vec3 vDelta = (staticPos.value() - vLocalPos);
				const float fRange = Math::VectorNormalize(vDelta);
				const float fElevationAngle = std::min(fRange * (G::CurItemDefIndex == Demoman_m_TheLochnLoad ? 0.0075f : 0.013f), 45.f);
				// if our angle is above 45 degree will we even hit them? shouldn't we just return???

				float s = 0.0f, c = 0.0f;
				Math::SinCos((fElevationAngle * PI / 180.0f), &s, &c);

				const float fElevation = (fRange * (s / c));
				staticPos.value().z += (c > 0.0f ? fElevation : 0.0f);
				break;
			}

			default: break;
		}

		// trace hull of projectile
		Utils::TraceHull(predictor.m_vPosition, staticPos.value(), Vec3(-3.8f, -3.8f, -3.8f), Vec3(3.8f, 3.8f, 3.8f), MASK_SOLID_BRUSHONLY, &traceFilter, &trace);
		if (trace.DidHit())
		{
			staticPos.value().z = trace.vEndPos.z;
		}

		switch (pWeapon->GetWeaponID())
		{
			case TF_WEAPON_GRENADELAUNCHER:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
			case TF_WEAPON_STICKBOMB:
			case TF_WEAPON_STICKY_BALL_LAUNCHER:
			{
				Vec3 vecOffset(16.0f, 8.0f, -6.0f);
				Utils::GetProjectileFireSetup(pLocal, pCmd->viewangles, vecOffset, &vLocalPos);
				break;
			}

			default: break;
		}

		if (!CalcProjAngle(vLocalPos, staticPos.value(), projInfo, out))
		{
			return false;
		}

		if (out.m_flTime > maxTime)
		{
			return false;
		}

		if (WillProjectileHit(pLocal, pWeapon, pCmd, staticPos.value(), out, projInfo, predictor))
		{
			G::PredictedPos = staticPos.value();
			return true;
		}
	}
	else
	{
		Vec3 vPredictedPos = {};
		CMoveData moveData = {};
		Vec3 absOrigin = {};
		bool bNeedsTimeCheck = false;	//fuick me

		if (F::MoveSim.Initialize(predictor.m_pEntity))
		{
			if (!predictor.m_pEntity) {
				F::MoveSim.Restore();
				return false;
			}

			for (int n = 0; n < TIME_TO_TICKS(maxTime); n++)
			{
				F::MoveSim.RunTick(moveData, absOrigin);
				vPredictedPos = absOrigin;

				const std::optional<Vec3> aimPosition = GetAimPos(pLocal, predictor.m_pEntity, vPredictedPos);
				if (!aimPosition)
				{
					bNeedsTimeCheck = true;
					continue; 
				} // don't remove.

				//const Vec3 vAimDelta = predictor.m_pEntity->GetAbsOrigin() - aimPosition;
				//vPredictedPos.x += abs(vAimDelta.x);
				//vPredictedPos.y += abs(vAimDelta.y);
				//vPredictedPos.z += abs(vAimDelta.z);
				vPredictedPos = aimPosition.value();


				// get angle offsets for demoman weapons?
				switch (pWeapon->GetWeaponID())
				{
					case TF_WEAPON_GRENADELAUNCHER:
					case TF_WEAPON_PIPEBOMBLAUNCHER:
					case TF_WEAPON_STICKBOMB:
					case TF_WEAPON_STICKY_BALL_LAUNCHER:
					{
						Vec3 vDelta = (vPredictedPos - vLocalPos);
						const float fRange = Math::VectorNormalize(vDelta);
						const float fElevationAngle = std::min(fRange * (G::CurItemDefIndex == Demoman_m_TheLochnLoad ? 0.0075f : 0.013f), 45.f);
						// if our angle is above 45 degree will we even hit them? shouldn't we just return???

						float s = 0.0f, c = 0.0f;
						Math::SinCos((fElevationAngle * PI / 180.0f), &s, &c);

						const float fElevation = (fRange * (s / c));
						vPredictedPos.z += (c > 0.0f ? fElevation : 0.0f);
						break;
					}
					default: break;
				}

				//Utils::TraceHull(predictor.m_vPosition, vPredictedPos, Vec3(-3.8f, -3.8f, -3.8f), Vec3(3.8f, 3.8f, 3.8f),
				//	MASK_SOLID_BRUSHONLY, &traceFilter, &trace);

				//if (trace.DidHit())
				//{
				//	vPredictedPos.z = trace.vEndPos.z;
				G::PredictedPos = vPredictedPos;
				//}

				switch (pWeapon->GetWeaponID())
				{
					case TF_WEAPON_GRENADELAUNCHER:
					case TF_WEAPON_PIPEBOMBLAUNCHER:
					case TF_WEAPON_STICKBOMB:
					case TF_WEAPON_STICKY_BALL_LAUNCHER:
					{
						Vec3 vecOffset(16.0f, 8.0f, -6.0f);
						Utils::GetProjectileFireSetup(pLocal, pCmd->viewangles, vecOffset, &vLocalPos);
						break;
					}

					default: break;
				}

				if (!CalcProjAngle(vLocalPos, vPredictedPos, projInfo, out))
				{
					break;
				}

				if (TIME_TO_TICKS(out.m_flTime) > (n + 1)) {
					continue;
				}

				out.m_flTime += fLatency;

				if (out.m_flTime < TICKS_TO_TIME(n))
				{
					if (WillProjectileHit(pLocal, pWeapon, pCmd, vPredictedPos, out, projInfo, predictor))
					{
						G::PredictedPos = vPredictedPos;
						F::MoveSim.Restore();
						m_flTravelTime = out.m_flTime;
						return true;
					}
				}
			}
			F::MoveSim.Restore();
		}
	}
	return false;
}

bool IsPointAllowed(int nHitbox)
{
	switch (nHitbox)
	{
		case 0: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 0);
		case 1: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 1);
		case 2: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 2);
		case 3:
		case 4:
		case 5:
		case 6: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 0);
		case 7:
		case 8:
		case 9:
		case 10: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 1);
		case 11:
		case 12:
		case 13:
		case 14: return Vars::Aimbot::Projectile::AllowedHitboxes.Value & (1 << 2);
	}
	return true; // never
}

//	Tries to find the best position to aim at on our target.
std::optional<Vec3> CAimbotProjectile::GetAimPos(CBaseEntity* pLocal, CBaseEntity* pEntity, const Vec3& targetPredPos)
{
	std::optional<Vec3> vRetVec = pLocal->GetAbsOrigin();
	const Vec3 vLocalPos = pLocal->GetAbsOrigin();

	const Vec3 vShootPos = pLocal->GetShootPos();

	const bool bIsDucking = (pEntity->m_bDucked() || pEntity->m_fFlags() & FL_DUCKING);

	const float flBBoxScale = Vars::Aimbot::Projectile::ScanScale.Value; // stop shoot flor (:D)

	// this way overshoots players that are crouching and I don't know why.
	const Vec3 vMaxs = I::GameMovement->GetPlayerMaxs(bIsDucking) * flBBoxScale;
	const auto vMins = Vec3(-vMaxs.x, -vMaxs.y, vMaxs.z - vMaxs.z * flBBoxScale);

	const Vec3 vHeadDelta = Utils::GetHeadOffset(pEntity);

	const std::vector vPoints = {
		// oh you don't like 15 points because it fucks your fps??? TOO BAD!//
		Vec3(vHeadDelta.x, vHeadDelta.y, vMaxs.z), //	head bone probably
		Vec3(0, 0, (vMins.z + vMaxs.z) / 2), //	middles (scan first bc they are more accurate)
		Vec3(0, 0, vMins.z), //	-
		Vec3(vMins.x, vMins.y, vMaxs.z), //	top four corners
		Vec3(vMins.x, vMaxs.y, vMaxs.z), //	-
		Vec3(vMaxs.x, vMaxs.y, vMaxs.z), //	-
		Vec3(vMaxs.x, vMins.y, vMaxs.z), //	-
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) / 2), //	middle four corners
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) / 2), //	-
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) / 2), //	-
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) / 2), //	-
		Vec3(vMins.x, vMins.y, vMins.z), //	bottom four corners
		Vec3(vMins.x, vMaxs.y, vMins.z), //	-
		Vec3(vMaxs.x, vMaxs.y, vMins.z), //	-
		Vec3(vMaxs.x, vMins.y, vMins.z) //	-
	};

	std::vector<Vec3> vVisPoints{};
	const matrix3x4 transform = {
		{1.f, 0, 0, targetPredPos.x},
		{0, 1.f, 0, targetPredPos.y},
		{0, 0, 1.f, pEntity->GetVecVelocity().IsZero() ? pEntity->GetAbsOrigin().z : targetPredPos.z}
	};

	int iAimMethod = Vars::Aimbot::Projectile::AimPosition.Value;

	int iCurPoint = 0, iTestPoints = 0; //maybe better way to do this
	for (const auto& vPoint : vPoints)
	{
		if (iTestPoints > 15) { break; }
		if (static_cast<int>(vVisPoints.size()) >= 7) { break; }
		if (!IsPointAllowed(iCurPoint))
		{
			iCurPoint++;
			continue;
		}

		Vec3 vTransformed = {};
		Math::VectorTransform(vPoint, transform, vTransformed);

		if (Utils::VisPosMask(pLocal, pEntity, vShootPos, vTransformed, MASK_SHOT_HULL))
		{
			if (iCurPoint == iAimMethod && iAimMethod < 3) { return vTransformed; } // return this value now if it is going to get returned anyway, avoid useless scanning.
			vVisPoints.push_back(vTransformed);
		}
		iCurPoint++;
		iTestPoints++; // Only increment this if we actually tested.
	}
	if (vVisPoints.empty())
	{
		return std::nullopt;
	}

	Vec3 HeadPoint, TorsoPoint, FeetPoint;
	
	const auto& pWeapon = g_EntityCache.GetWeapon();
	if (!pWeapon) { return vRetVec; }
	
	const int iClassNum = pLocal->GetClassNum();
	const int iSlot = pWeapon->GetSlot();

	static KeyHelper kBounce{ &Vars::Aimbot::Projectile::BounceKey.Value };

	if (kBounce.Down())	//	player demands we aim at feet (fetishist ngl)
	{ iAimMethod = 2; }

	static KeyHelper kCritHack{ &Vars::CritHack::CritKey.Value };

	if (pWeapon->GetWeaponID() == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT)
	{
		if (kCritHack.Down() || pLocal->IsCritBoosted()) //force baim with direct hit if critting
		{
			iAimMethod = 1;
		}
	}

	while (iAimMethod == 3)
	{
		if (pEntity->OnSolid() && Vars::Aimbot::Projectile::FeetAimIfOnGround.Value) {
			switch (iClassNum) {
			case CLASS_SOLDIER: {
				if (iSlot == 0) { iAimMethod = 2; }
				break;
			}
			case CLASS_DEMOMAN: {
				if (iSlot < 2) { iAimMethod = 2; }
				break;
			}
			default: {
				break;
			}
			}
			if (iAimMethod == 2) { break; }	//	i forgor how loops work and disabled optimisation on this function for a while until realising what i had done lmao
		}

		switch (iClassNum)
		{
		case CLASS_SNIPER:
		{ iAimMethod = 0; break; }	//	keeping breaks here to prevent warnings but i doubt they are needed
		default:
		{ iAimMethod = 1; break; }
		}
	}
	
	if (Vars::Debug::DebugInfo.Value)
	{
		I::Cvar->ConsolePrintf("iAimMethod	: %d\n", iAimMethod);
		I::Cvar->ConsolePrintf("iWeaponID	: %d\n", pWeapon->GetWeaponID());
		I::Cvar->ConsolePrintf("iWeaponSlot	: %d\n", iSlot);
		I::Cvar->ConsolePrintf("iClassNum	: %d\n", iClassNum);
	}

	switch (iAimMethod)
	{
		case 0:
		{
			//head
			Math::VectorTransform(vPoints.at(0), transform, HeadPoint); //	get transformed location of our "best point" for our selected prio hitbox
			for (const auto& vAimPoint : vVisPoints)
			{
				//	iterate through visible points
				if (vAimPoint.DistTo(HeadPoint) < vRetVec.value().DistTo(HeadPoint))
				{
					//	if the distance to our best point is lower than the previous selected point,
					vRetVec = vAimPoint; //	set the new point to our currently selected point
				}
			}
			break;
		}
		case 1:
		{
			//torso
			Math::VectorTransform(vPoints.at(1), transform, TorsoPoint);
			for (const auto& vAimPoint : vVisPoints)
			{
				if (vAimPoint.DistTo(TorsoPoint) < vRetVec.value().DistTo(TorsoPoint))
				{
					vRetVec = vAimPoint;
				}
			}
			break;
		}
		case 2:
		{
			//feet
			Math::VectorTransform(vPoints.at(2), transform, FeetPoint);
			for (const auto& vAimPoint : vVisPoints)
			{
				if (vAimPoint.DistTo(FeetPoint) < vRetVec.value().DistTo(FeetPoint))
				{
					vRetVec = vAimPoint;
				}
			}
			break;
		}
	}
	return vRetVec.value().DistTo(vLocalPos) < 1.f ? std::nullopt : vRetVec;	//	messy lol
}

std::optional<Vec3> CAimbotProjectile::GetAimPosBuilding(CBaseEntity* pLocal, CBaseEntity* pEntity)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();

	const float bboxScale = std::max(Vars::Aimbot::Projectile::ScanScale.Value - 0.05f, 0.5f); // set the maximum scale for buildings at .95f

	const Vec3 vMins = pEntity->GetCollideableMins() * bboxScale;
	const Vec3 vMaxs = pEntity->GetCollideableMaxs() * bboxScale;

	const std::vector vecPoints = {
		Vec3(vMaxs.x / 2, vMaxs.y / 2, vMaxs.z / 2), //	middle
		Vec3(vMins.x, vMins.y, vMaxs.z), //	top four corners
		Vec3(vMins.x, vMaxs.y, vMaxs.z), //	-
		Vec3(vMaxs.x, vMaxs.y, vMaxs.z), //	-
		Vec3(vMaxs.x, vMins.y, vMaxs.z), //	-
		Vec3(vMins.x, vMins.y, vMins.z), //	bottom four corners
		Vec3(vMins.x, vMaxs.y, vMins.z), //	-
		Vec3(vMaxs.x, vMaxs.y, vMins.z), //	-
		Vec3(vMaxs.x, vMins.y, vMins.z) //	-
	};

	const matrix3x4& transform = pEntity->GetRgflCoordinateFrame();

	for (const auto& point : vecPoints)
	{
		Vec3 vTransformed = {};
		Math::VectorTransform(point, transform, vTransformed);

		if (Utils::VisPos(pLocal, pEntity, vLocalPos, vTransformed))
		{
			return vTransformed; // just return the first point we see
		}
	}

	return std::nullopt;
}

bool CAimbotProjectile::WillProjectileHit(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd, const Vec3& vPredictedPos, Solution_t& out, const ProjectileInfo_t& projInfo,
										  const Predictor_t& predictor)
{
	Vec3 hullSize = { 3.8f, 3.8f, 3.8f };
	Vec3 vVisCheck = pLocal->GetEyePosition();
	const Vec3 predictedViewAngles = { -RAD2DEG(out.m_flPitch), RAD2DEG(out.m_flYaw), 0.0f };
	CGameTrace trace = {};
	static CTraceFilterWorldAndPropsOnly traceFilter = {};
	traceFilter.pSkip = predictor.m_pEntity;

	// this shit's messy
	{
		switch (pWeapon->GetWeaponID())
		{
			case TF_WEAPON_PARTICLE_CANNON:
			{
				hullSize = { 1.f, 1.f, 1.f };
				Vec3 vecOffset(23.5f, 8.0f, -3.0f); //tf_weaponbase_gun.cpp @L529 & @L760
				if (pLocal->IsDucking())
				{
					vecOffset.z = 8.0f;
				}
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_CROSSBOW:
			{
				hullSize = { 3.f, 3.f, 3.f };
				const Vec3 vecOffset(23.5f, -8.0f, -3.0f);
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_RAYGUN_REVENGE:
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_FLAREGUN:
			{
				hullSize = { 1.f, 1.f, 1.f }; //tf_projectile_base.cpp @L117

				Vec3 vecOffset = Vec3(23.5f, 12.0f, -3.0f); //tf_weaponbase_gun.cpp @L529 & @L760
				if (G::CurItemDefIndex == Soldier_m_TheOriginal)
				{
					vecOffset.y = 0.f;
				}
				if (pLocal->IsDucking())
				{
					vecOffset.z = 8.0f;
				}
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_SYRINGEGUN_MEDIC:
			{
				hullSize = { 1.f, 1.f, 1.f };

				const Vec3 vecOffset(16.f, 6.f, -8.f); //tf_weaponbase_gun.cpp @L628
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_COMPOUND_BOW:
			{
				hullSize = { 1.f, 1.f, 1.f };	//	tf_projectile_arrow.cpp @L271

				const Vec3 vecOffset(23.5f, 12.0f, -3.0f); //tf_weapon_grapplinghook.cpp @L355 ??
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			{
				hullSize = { 1.f, 1.f, 1.f };
				Vec3 vecOffset(23.5f, -8.0f, -3.0f); //tf_weaponbase_gun.cpp @L568
				if (pLocal->IsDucking())
				{
					vecOffset.z = 8.0f;
				}
				Utils::GetProjectileFireSetup(pLocal, predictedViewAngles, vecOffset, &vVisCheck);
				break;
			}
			case TF_WEAPON_GRENADELAUNCHER:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
			case TF_WEAPON_STICKBOMB:
			case TF_WEAPON_STICKY_BALL_LAUNCHER:
			{
				hullSize = { 4.f, 4.f, 4.f }; //basegrenade_timed.cpp @L32 ????

				auto vecAngle = Vec3(), vecForward = Vec3(), vecRight = Vec3(), vecUp = Vec3();
				Math::AngleVectors({ -RAD2DEG(out.m_flPitch), RAD2DEG(out.m_flYaw), 0.0f }, &vecForward, &vecRight, &vecUp);
				const Vec3 vecVelocity = ((vecForward * projInfo.m_flVelocity) - (vecUp * 200.0f));
				Math::VectorAngles(vecVelocity, vecAngle);
				out.m_flPitch = -DEG2RAD(vecAngle.x);

				// see relevant code @tf_weaponbase_gun.cpp L684
				const float fRight = g_ConVars.cl_flipviewmodels->GetInt() ? -8.f : 8.f;
				vVisCheck = pLocal->GetShootPos() + vecForward * 16.0f + vecRight * fRight + vecUp * -6.0f;

				break;
			}
			default: break;
		}
	}

	//	TODO: find the actual hull size of projectiles
	//	maybe - https://www.unknowncheats.me/forum/team-fortress-2-a/475502-weapons-projectile-min-max-collideables.html
	//	UTIL_SetSize( this, -Vector( 1.0f, 1.0f, 1.0f ), Vector( 1.0f, 1.0f, 1.0f ) ); @tf_projectile_base.cpp L117
	//	UTIL_TraceHull( vecEye, vecSrc, -Vector(8,8,8), Vector(8,8,8), MASK_SOLID_BRUSHONLY, &traceFilter, &trace ); @tf_weaponbase_gun.cpp L696 pills
	Utils::TraceHull(vVisCheck, vPredictedPos, hullSize * 1.01f, hullSize * -1.01f, MASK_SHOT_HULL, &traceFilter, &trace);

	return !trace.DidHit() && !trace.bStartSolid;
}

std::vector<Target_t> CAimbotProjectile::GetTargets(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon)
{
	std::vector<Target_t> validTargets;
	const auto sortMethod = static_cast<ESortMethod>(Vars::Aimbot::Projectile::SortMethod.Value);

	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	// Players
	if (Vars::Aimbot::Global::AimAt.Value & (ToAimAt::PLAYER))
	{
		const bool bIsCrossbow = pWeapon->GetWeaponID() == TF_WEAPON_CROSSBOW;

		for (const auto& pTarget : g_EntityCache.GetGroup(bIsCrossbow ? EGroupType::PLAYERS_ALL : EGroupType::PLAYERS_ENEMIES))
		{
			if (!pTarget->IsAlive() || pTarget->IsAGhost() || pTarget == pLocal)
			{
				continue;
			}

			// Check if the crossbow should shoot at friendly players
			if (bIsCrossbow && (pTarget->GetTeamNum() == pLocal->GetTeamNum()))
			{
				if (pTarget->GetHealth() >= pTarget->GetMaxHealth())
				{
					continue;
				}
			}

			if (pTarget->GetTeamNum() != pLocal->GetTeamNum())
			{
				if (F::AimbotGlobal.ShouldIgnore(pTarget)) { continue; }
			}

			Vec3 vPos = pTarget->GetWorldSpaceCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);

			if (flFOVTo > Vars::Aimbot::Projectile::AimFOV.Value)
			{
				continue;
			}

			const float flDistTo = (sortMethod == ESortMethod::DISTANCE) ? vLocalPos.DistTo(vPos) : 0.0f;
			const auto& priority = F::AimbotGlobal.GetPriority(pTarget->GetIndex());
			validTargets.push_back({ pTarget, ETargetType::PLAYER, vPos, vAngleTo, flFOVTo, flDistTo, -1, false, priority });
		}
	}

	// Buildings
	const bool bIsRescueRanger = pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE;

	for (const auto& pBuilding : g_EntityCache.GetGroup(bIsRescueRanger ? EGroupType::BUILDINGS_ALL : EGroupType::BUILDINGS_ENEMIES))
	{
		bool isSentry = pBuilding->GetClassID() == ETFClassID::CObjectSentrygun;
		bool isDispenser = pBuilding->GetClassID() == ETFClassID::CObjectDispenser;
		bool isTeleporter = pBuilding->GetClassID() == ETFClassID::CObjectTeleporter;

		if (!(Vars::Aimbot::Global::AimAt.Value & (ToAimAt::SENTRY)) && isSentry) { continue; }
		if (!(Vars::Aimbot::Global::AimAt.Value & (ToAimAt::DISPENSER)) && isDispenser) { continue; }
		if (!(Vars::Aimbot::Global::AimAt.Value & (ToAimAt::TELEPORTER)) && isTeleporter) { continue; }

		const auto& Building = reinterpret_cast<CBaseObject*>(pBuilding);

		if (!pBuilding->IsAlive()) { continue; }

		// Check if the Rescue Ranger should shoot at friendly buildings
		if (bIsRescueRanger && (pBuilding->GetTeamNum() == pLocal->GetTeamNum()))
		{
			if (Building->GetHealth() >= Building->GetMaxHealth()) { continue; }
		}

		Vec3 vPos = pBuilding->GetWorldSpaceCenter();
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
		const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);

		if (flFOVTo > Vars::Aimbot::Projectile::AimFOV.Value)
		{
			continue;
		}
		const float flDistTo = sortMethod == ESortMethod::DISTANCE ? vLocalPos.DistTo(vPos) : 0.0f;
		validTargets.push_back({ pBuilding, isSentry ? ETargetType::SENTRY : (isDispenser ? ETargetType::DISPENSER : ETargetType::TELEPORTER), vPos, vAngleTo, flFOVTo, flDistTo });
	}

	// NPCs
	if (Vars::Aimbot::Global::AimAt.Value & (ToAimAt::NPC))
	{
		for (const auto& NPC : g_EntityCache.GetGroup(EGroupType::WORLD_NPC))
		{
			Vec3 vPos = NPC->GetWorldSpaceCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);

			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = sortMethod == ESortMethod::DISTANCE ? vLocalPos.DistTo(vPos) : 0.0f;

			if (flFOVTo > Vars::Aimbot::Projectile::AimFOV.Value)
			{
				continue;
			}

			validTargets.push_back({ NPC, ETargetType::NPC, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	//Bombs
	if (Vars::Aimbot::Global::AimAt.Value & (ToAimAt::BOMB))
	{
		//This is pretty bad with projectiles
		for (const auto& Bombs : g_EntityCache.GetGroup(EGroupType::WORLD_BOMBS))
		{
			Vec3 vPos = Bombs->GetWorldSpaceCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);

			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			const float flDistTo = sortMethod == ESortMethod::DISTANCE ? vLocalPos.DistTo(vPos) : 0.0f;

			if (flFOVTo > Vars::Aimbot::Projectile::AimFOV.Value)
			{
				continue;
			}

			validTargets.push_back({ Bombs, ETargetType::BOMBS, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	return validTargets;
}

bool CAimbotProjectile::VerifyTarget(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd, Target_t& target)
{
	ProjectileInfo_t projInfo = {};
	if (!GetProjectileInfo(pWeapon, projInfo))
	{
		return false;
	}

	if (pLocal->IsPrecisionRune() && IsBoosted)
	{
		projInfo.m_flVelocity *= 2.5f;
	}

	auto vVelocity = Vec3();
	auto vAcceleration = Vec3();

	switch (target.m_TargetType)
	{
		case ETargetType::PLAYER:
		{
			vVelocity = target.m_pEntity->GetVelocity();
			vAcceleration = Vec3(0.0f, 0.0f, g_ConVars.sv_gravity->GetFloat() * ((target.m_pEntity->GetCondEx2() & TFCondEx2_Parachute) ? 0.224f : 1.0f));
			break;
		}

		default: break;
	}

	Predictor_t predictor = {
		target.m_pEntity,
		target.m_vPos,
		vVelocity,
		vAcceleration
	};

	Solution_t solution = {};

	if (!SolveProjectile(pLocal, pWeapon, pCmd, predictor, projInfo, solution))
	{
		return false;
	}

	target.m_vAngleTo = { -RAD2DEG(solution.m_flPitch), RAD2DEG(solution.m_flYaw), 0.0f };

	return true;
}

// Returns the best target
bool CAimbotProjectile::GetTarget(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd, Target_t& outTarget)
{
	auto validTargets = GetTargets(pLocal, pWeapon);
	if (validTargets.empty()) { return false; }

	const auto& sortMethod = static_cast<ESortMethod>(Vars::Aimbot::Projectile::SortMethod.Value);
	F::AimbotGlobal.SortTargets(&validTargets, sortMethod);

	//instead of this just limit to like 4-6 targets, should save perf without any noticeable changes in functionality
	for (auto& target : validTargets)
	{
		if (!VerifyTarget(pLocal, pWeapon, pCmd, target))
		{
			continue;
		}

		outTarget = target;
		return true;
	}

	return false;
}

// Aims at the given angles
void CAimbotProjectile::Aim(CUserCmd* pCmd, CBaseCombatWeapon* pWeapon, Vec3& vAngle)
{
	vAngle -= G::PunchAngles;
	Math::ClampAngles(vAngle);

	switch (Vars::Aimbot::Projectile::AimMethod.Value)
	{
		case 0:
		{
			// Plain
			pCmd->viewangles = vAngle;
			I::EngineClient->SetViewAngles(pCmd->viewangles);
			break;
		}

		case 1:
		{
			// Silent
			Utils::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			break;
		}

		default: break;
	}
}

bool CAimbotProjectile::ShouldFire(CUserCmd* pCmd)
{
	return (Vars::Aimbot::Global::AutoShoot.Value && G::WeaponCanAttack);
}

bool CAimbotProjectile::IsAttacking(const CUserCmd* pCmd, CBaseCombatWeapon* pWeapon)
{
	if (G::CurItemDefIndex == Soldier_m_TheBeggarsBazooka)
	{
		static bool bLoading = false;

		if (pWeapon->GetClip1() > 0)
		{
			bLoading = true;
		}

		if (!(pCmd->buttons & IN_ATTACK) && bLoading)
		{
			bLoading = false;
			return true;
		}
	}
	else
	{
		if (pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW || pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
		{
			static bool bCharging = false;

			if (pWeapon->GetChargeBeginTime() > 0.0f)
			{
				bCharging = true;
			}

			if (!(pCmd->buttons & IN_ATTACK) && bCharging)
			{
				bCharging = false;
				return true;
			}
		}
		else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
		{
			static bool Charging = false;

			if (pWeapon->GetDetonateTime() > 0.0f)
			{
				Charging = true;
			}

			if (!(pCmd->buttons & IN_ATTACK) && Charging)
			{
				Charging = false;
				return true;
			}
		}

		//pssst..
		//Dragon's Fury has a gauge (seen on the weapon model) maybe it would help for pSilent hmm..
		/*
		if (pWeapon->GetWeaponID() == 109) {
		}*/

		else
		{
			if ((pCmd->buttons & IN_ATTACK) && G::WeaponCanAttack)
			{
				return true;
			}
		}
	}

	return false;
}

// Returns the best target for splash damage
bool CAimbotProjectile::GetSplashTarget(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd, Target_t& outTarget)
{
	if (!Vars::Aimbot::Projectile::SplashPrediction.Value) { return false; }

	std::optional<float> splashRadius;

	splashRadius = Utils::ATTRIB_HOOK_FLOAT(148, "mult_explosion_radius", pWeapon);
	float splashRadiusModified = splashRadius.value() * 0.8; //this value will only be used if you are blast jumping with the air strike

	//check if you are rocket jumping, and change the value appropriately, because the air strike blast radius changes if you are rocket jumping.
	if (pLocal->GetCondEx2() & TFCondEx2_BlastJumping && G::CurItemDefIndex == Soldier_m_TheAirStrike)
		splashRadius = splashRadiusModified;

	// Don't do it with the direct hit or if the splash radius is unknown
	if (pWeapon->GetClassID() == ETFClassID::CTFRocketLauncher_DirectHit || !splashRadius) { return false; }

	const auto& sortMethod = static_cast<ESortMethod>(Vars::Aimbot::Projectile::SortMethod.Value);
	const auto& vLocalAngles = I::EngineClient->GetViewAngles();
	const auto& vLocalShootPos = pLocal->GetShootPos();
	const auto& vLocalOrigin = pLocal->GetAbsOrigin();

	for (const auto& pTarget : g_EntityCache.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (!pTarget || !pTarget->IsAlive() || !pTarget->IsOnGround()) { continue; }

		if (F::AimbotGlobal.ShouldIgnore(pTarget))
		{
			continue;
		}

		const auto& vTargetCenter = pTarget->GetWorldSpaceCenter();
		const auto& vTargetOrigin = pTarget->GetAbsOrigin();

		if (vLocalOrigin.z < vTargetOrigin.z - 45.f) { continue; } // Don't shoot from below

		// Don't predict enemies that are visible
		if (Utils::VisPos(pLocal, pTarget, pLocal->GetShootPos(), vTargetCenter)) { continue; }

		// Scan every 45 degree angle
		for (int i = 0; i < 315; i += 45)
		{
			Vec3 scanPos = Utils::GetRotatedPosition(vTargetCenter, static_cast<float>(i), *splashRadius);

			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly traceFilter = {};

			// Check FOV if enabled
			const Vec3 vAngleTo = Math::CalcAngle(vLocalShootPos, scanPos);
			const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::Projectile::AimFOV.Value) { continue; }

			// Can the target receive splash damage? (Don't predict through walls)
			Utils::Trace(scanPos, pTarget->GetWorldSpaceCenter(), MASK_SOLID, &traceFilter, &trace);
			if (trace.flFraction < 0.99f && trace.entity != pTarget) { continue; }

			// Is the predicted position even visible?
			if (!Utils::WillProjectileHit(pLocal, pWeapon, scanPos)) { continue; }

			// Get the closest point to the target
			float currentRadius = *splashRadius;
			while (currentRadius > 10.f && Utils::WillProjectileHit(pLocal, pWeapon, scanPos))
			{
				scanPos = Utils::GetRotatedPosition(vTargetCenter, static_cast<float>(i), currentRadius - 10.f);
				currentRadius -= 10.f;
			}

			/*
			 *	We found the closest point!
			 *	Now we need to get the shoot pos/angles relative to vTargetOrigin instead of vTargetCenter
			 */
			currentRadius = std::clamp(currentRadius + 10.f, 0.f, *splashRadius);
			scanPos = Utils::GetRotatedPosition(vTargetOrigin, static_cast<float>(i), currentRadius);

			if (Vars::Debug::DebugInfo.Value)
			{
				I::DebugOverlay->AddLineOverlay(vTargetOrigin, scanPos, 255, 0, 0, false, MAXIMUM_TICK_INTERVAL);
				I::DebugOverlay->AddTextOverlay(scanPos, MAXIMUM_TICK_INTERVAL, "X");
			}

			const Vec3 vAngleToSplash = Math::CalcAngle(pLocal->GetEyePosition(), scanPos);
			outTarget = { pTarget, ETargetType::PLAYER, vTargetOrigin, vAngleToSplash };
			return true;
		}
	}

	return false;
}

void CAimbotProjectile::Run(CBaseEntity* pLocal, CBaseCombatWeapon* pWeapon, CUserCmd* pCmd)
{
	running = false;

	static int nLastTracerTick = pCmd->tick_count;

	IsFlameThrower = false;

	if (!Vars::Aimbot::Global::Active.Value || !Vars::Aimbot::Projectile::Active.Value)
	{
		return;
	}

	const bool bShouldAim = (Vars::Aimbot::Global::AimKey.Value == VK_LBUTTON
							 ? (pCmd->buttons & IN_ATTACK)
							 : F::AimbotGlobal.IsKeyDown());
	if (!bShouldAim) { return; }

	Target_t target{};
	if (GetTarget(pLocal, pWeapon, pCmd, target) || GetSplashTarget(pLocal, pWeapon, pCmd, target))
	{
		// Aim at the current target or splashtarget
		G::CurrentTargetIdx = target.m_pEntity->GetIndex();

		if (Vars::Aimbot::Projectile::AimMethod.Value == 1)
		{
			G::AimPos = G::PredictedPos;
		}

		if (ShouldFire(pCmd))
		{
			running = true;
			pCmd->buttons |= IN_ATTACK;

			if (G::CurItemDefIndex == Soldier_m_TheBeggarsBazooka)
			{
				if (pWeapon->GetClip1() > 0)
				{
					pCmd->buttons &= ~IN_ATTACK;
				}
			}
			else
			{
				if ((pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW || pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
					&& pWeapon->GetChargeBeginTime() > 0.0f)
				{
					pCmd->buttons &= ~IN_ATTACK;
				}
				else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON && pWeapon->GetDetonateTime() > 0.0f)
				{
					const Vec3 vEyePos = pLocal->GetShootPos();
					const float bestCharge = vEyePos.DistTo(G::PredictedPos) / 1454.0f;

					if (Vars::Aimbot::Projectile::ChargeLooseCannon.Value)
					{
						if (target.m_TargetType == ETargetType::SENTRY || 
							target.m_TargetType == ETargetType::DISPENSER || 
							target.m_TargetType == ETargetType::TELEPORTER || 
							target.m_TargetType == ETargetType::BOMBS) // please DONT double donk buildings or bombs
						{
							pCmd->buttons &= ~IN_ATTACK; 
						}

						if (target.m_pEntity->GetHealth() > 50) //check if we even need to double donk to kill first
						{
							if (pWeapon->GetDetonateTime() - I::GlobalVars->curtime <= bestCharge)
							{
								pCmd->buttons &= ~IN_ATTACK;
							}
						}
						else
						{
							pCmd->buttons &= ~IN_ATTACK;
						}
					}
					else
					{
						pCmd->buttons &= ~IN_ATTACK;
					}
				}
			}
		}

		const bool bIsAttacking = IsAttacking(pCmd, pWeapon);

		if (bIsAttacking)
		{
			G::IsAttacking = true;
			m_flTravelTimeStart = I::GlobalVars->curtime + m_flTravelTime;
			if (Vars::Visuals::BulletTracer.Value && abs(pCmd->tick_count - nLastTracerTick) > 1)
			{
				F::Visuals.DrawProjectileTracer(pLocal, target.m_vPos);
				nLastTracerTick = pCmd->tick_count;
			}
			G::PredLinesBackup.clear();
			G::PredLinesBackup = G::PredictionLines;

			//I::DebugOverlay->AddLineOverlayAlpha(Target.m_vPos, G::m_vPredictedPos, 0, 255, 0, 255, true, 2); // Predicted aim pos
		}

		if (Vars::Aimbot::Projectile::AimMethod.Value == 1)
		{
			if (IsFlameThrower)
			{
				G::UpdateView = false;
				Aim(pCmd, pWeapon, target.m_vAngleTo);
			}

			else
			{
				if (bIsAttacking)
				{
					Aim(pCmd, pWeapon, target.m_vAngleTo);
					G::SilentTime = true;
				}
			}
		}
		else
		{
			Aim(pCmd, pWeapon, target.m_vAngleTo);
		}
	}
}
