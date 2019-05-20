// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "stdafx.h"
#include "Object.h"
#include "CVars.h"
#include "EventInstance.h"
#include "Impl.h"
#include "Listener.h"

#include <AK/SoundEngine/Common/AkSoundEngine.h>

#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)
	#include <Logger.h>
	#include <DebugStyle.h>
#endif  // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE

namespace CryAudio
{
namespace Impl
{
namespace Wwise
{
constexpr char const* g_szAbsoluteVelocityParameterName = "absolute_velocity";
static AkRtpcID const s_absoluteVelocityParameterId = AK::SoundEngine::GetIDFromString(g_szAbsoluteVelocityParameterName);

constexpr char const* g_szRelativeVelocityParameterName = "relative_velocity";
static AkRtpcID const s_relativeVelocityParameterId = AK::SoundEngine::GetIDFromString(g_szRelativeVelocityParameterName);

//////////////////////////////////////////////////////////////////////////
CObject::CObject(
	AkGameObjectID const id,
	CTransformation const& transformation,
	ListenerInfos const& listenerInfos,
	char const* const szName)
	: CBaseObject(id, listenerInfos, szName, transformation.GetPosition())
	, m_needsToUpdateAuxSends(false)
	, m_previousRelativeVelocity(0.0f)
	, m_previousAbsoluteVelocity(0.0f)
	, m_transformation(transformation)
	, m_previousPosition(transformation.GetPosition())
	, m_velocity(ZERO)
{
	m_auxSendValues.reserve(4);

	AkSoundPosition soundPos;
	FillAKObjectPosition(transformation, soundPos);
	AK::SoundEngine::SetPosition(id, soundPos);
}

//////////////////////////////////////////////////////////////////////////
CObject::~CObject()
{
	if ((m_flags& EObjectFlags::TrackRelativeVelocity) != 0)
	{
		CRY_ASSERT_MESSAGE(g_numObjectsWithRelativeVelocity > 0, "g_numObjectsWithRelativeVelocity is 0 but an object with relative velocity tracking still exists during %s", __FUNCTION__);
		g_numObjectsWithRelativeVelocity--;
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::Update(float const deltaTime)
{
	if (m_needsToUpdateAuxSends)
	{
		SetAuxSendValues();
	}

	CBaseObject::Update(deltaTime);

	if (deltaTime > 0.0f)
	{
		UpdateVelocities(deltaTime);
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::SetTransformation(CTransformation const& transformation)
{
	m_position = transformation.GetPosition();

	if (((m_flags& EObjectFlags::TrackAbsoluteVelocity) != 0) || ((m_flags& EObjectFlags::TrackRelativeVelocity) != 0))
	{
		m_flags |= EObjectFlags::MovingOrDecaying;
	}
	else
	{
		m_previousPosition = m_position;
	}

	float const threshold = m_shortestDistanceToListener * g_cvars.m_positionUpdateThresholdMultiplier;

	if (!m_transformation.IsEquivalent(transformation, threshold))
	{
		m_transformation = transformation;

		AkSoundPosition soundPos;
		FillAKObjectPosition(m_transformation, soundPos);
		AK::SoundEngine::SetPosition(m_id, soundPos);
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::SetOcclusion(IListener* const pIListener, float const occlusion, uint8 const numRemainingListeners)
{
	auto const pListener = static_cast<CListener*>(pIListener);

	AK::SoundEngine::SetObjectObstructionAndOcclusion(
		m_id,
		pListener->GetId(),               // Set occlusion for only the default listener for now.
		static_cast<AkReal32>(occlusion), // The occlusion value is currently used on obstruction as well until a correct obstruction value is calculated.
		static_cast<AkReal32>(occlusion));
}

//////////////////////////////////////////////////////////////////////////
void CObject::SetOcclusionType(EOcclusionType const occlusionType)
{
	// For disabling ray casts of the propagation processor if an object is virtual.
	if ((occlusionType != EOcclusionType::None) && (occlusionType != EOcclusionType::Ignore))
	{
		m_flags |= EObjectFlags::UpdateVirtualStates;
	}
	else
	{
		m_flags &= ~EObjectFlags::UpdateVirtualStates;
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::SetAuxSendValues()
{
	std::size_t const numAuxSends = m_auxSendValues.size();

	if (numAuxSends > 0)
	{
		AK::SoundEngine::SetGameObjectAuxSendValues(m_id, &m_auxSendValues[0], static_cast<AkUInt32>(numAuxSends));

		m_auxSendValues.erase(
			std::remove_if(
				m_auxSendValues.begin(),
				m_auxSendValues.end(),
				[](AkAuxSendValue const& auxSendValue) -> bool { return auxSendValue.fControlValue == 0.0f; }
				),
			m_auxSendValues.end()
			);
	}

	m_needsToUpdateAuxSends = false;
}

//////////////////////////////////////////////////////////////////////////
void CObject::SetAuxBusSend(AkAuxBusID const busId, float const amount)
{
	static float const envEpsilon = 0.0001f;
	bool addAuxSendValue = true;

	for (auto& auxSendValue : m_auxSendValues)
	{
		if (auxSendValue.auxBusID == busId)
		{
			addAuxSendValue = false;

			if (fabs(auxSendValue.fControlValue - amount) > envEpsilon)
			{
				auxSendValue.fControlValue = amount;
				m_needsToUpdateAuxSends = true;
			}

			break;
		}
	}

	if (addAuxSendValue)
	{
		// This temporary copy is needed until AK equips AkAuxSendValue with a ctor.
		m_auxSendValues.emplace_back(AkAuxSendValue{ g_listenerId, busId, amount });
		m_needsToUpdateAuxSends = true;
	}
}
///////////////////////////////////////////////////////////////////////////
void CObject::UpdateVelocities(float const deltaTime)
{
	if ((m_flags& EObjectFlags::MovingOrDecaying) != 0)
	{
		Vec3 const deltaPos(m_position - m_previousPosition);

		if (!deltaPos.IsZero())
		{
			m_velocity = deltaPos / deltaTime;
			m_previousPosition = m_position;
		}
		else if (!m_velocity.IsZero())
		{
			// We did not move last frame, begin exponential decay towards zero.
			float const decay = std::max(1.0f - deltaTime / 0.05f, 0.0f);
			m_velocity *= decay;

			if (m_velocity.GetLengthSquared() < FloatEpsilon)
			{
				m_velocity = ZERO;
				m_flags &= ~EObjectFlags::MovingOrDecaying;
			}
		}

		if ((m_flags& EObjectFlags::TrackAbsoluteVelocity) != 0)
		{
			float const absoluteVelocity = m_velocity.GetLength();

			if (absoluteVelocity == 0.0f || fabs(absoluteVelocity - m_previousAbsoluteVelocity) > g_cvars.m_velocityTrackingThreshold)
			{
				m_previousAbsoluteVelocity = absoluteVelocity;

#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)
				AKRESULT const wwiseResult = AK::SoundEngine::SetRTPCValue(s_absoluteVelocityParameterId, static_cast<AkRtpcValue>(absoluteVelocity), m_id);

				if (CRY_AUDIO_IMPL_WWISE_IS_OK(wwiseResult))
				{
					m_parameterInfo[g_szAbsoluteVelocityParameterName] = absoluteVelocity;
				}
#else
				AK::SoundEngine::SetRTPCValue(s_absoluteVelocityParameterId, static_cast<AkRtpcValue>(absoluteVelocity), m_id);
#endif        // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE
			}
		}

		if ((m_flags& EObjectFlags::TrackRelativeVelocity) != 0)
		{
			// Approaching positive, departing negative value. Highest value of all listeners is used.
			float relativeVelocity = 0.0f;

			for (auto const& info : m_listenerInfos)
			{
				if (((m_flags& EObjectFlags::MovingOrDecaying) != 0) && !info.pListener->HasMoved())
				{
					float const tempRelativeVelocity = -m_velocity.Dot((m_position - info.pListener->GetPosition()).GetNormalized());

					if (fabs(tempRelativeVelocity) > fabs(relativeVelocity))
					{
						relativeVelocity = tempRelativeVelocity;
					}
				}
				else if (((m_flags& EObjectFlags::MovingOrDecaying) != 0) && info.pListener->HasMoved())
				{
					Vec3 const relativeVelocityVec(m_velocity - info.pListener->GetVelocity());

					float const tempRelativeVelocity = -relativeVelocityVec.Dot((m_position - info.pListener->GetPosition()).GetNormalized());

					if (fabs(tempRelativeVelocity) > fabs(relativeVelocity))
					{
						relativeVelocity = tempRelativeVelocity;
					}
				}
			}

			TryToSetRelativeVelocity(relativeVelocity);
		}
	}
	else if ((m_flags& EObjectFlags::TrackRelativeVelocity) != 0)
	{
		for (auto const& info : m_listenerInfos)
		{
			// Approaching positive, departing negative value. Highest value of all listeners is used.
			float relativeVelocity = 0.0f;

			if (info.pListener->HasMoved())
			{
				float const tempRelativeVelocity = info.pListener->GetVelocity().Dot((m_position - info.pListener->GetPosition()).GetNormalized());

				if (fabs(tempRelativeVelocity) > fabs(relativeVelocity))
				{
					relativeVelocity = tempRelativeVelocity;
				}

				TryToSetRelativeVelocity(relativeVelocity);
			}
			else if (m_previousRelativeVelocity != 0.0f)
			{
				TryToSetRelativeVelocity(0.0f);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::TryToSetRelativeVelocity(float const relativeVelocity)
{
	if (relativeVelocity == 0.0f || fabs(relativeVelocity - m_previousRelativeVelocity) > g_cvars.m_velocityTrackingThreshold)
	{
		m_previousRelativeVelocity = relativeVelocity;

#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)
		AKRESULT const wwiseResult = AK::SoundEngine::SetRTPCValue(s_relativeVelocityParameterId, static_cast<AkRtpcValue>(relativeVelocity), m_id);

		if (CRY_AUDIO_IMPL_WWISE_IS_OK(wwiseResult))
		{
			m_parameterInfo[g_szRelativeVelocityParameterName] = relativeVelocity;
		}
#else
		AK::SoundEngine::SetRTPCValue(s_relativeVelocityParameterId, static_cast<AkRtpcValue>(relativeVelocity), m_id);
#endif    // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::ToggleFunctionality(EObjectFunctionality const type, bool const enable)
{
	switch (type)
	{
	case EObjectFunctionality::TrackAbsoluteVelocity:
		{
			if (enable)
			{
				m_flags |= EObjectFlags::TrackAbsoluteVelocity;
			}
			else
			{
				m_flags &= ~EObjectFlags::TrackAbsoluteVelocity;

				AK::SoundEngine::SetRTPCValue(s_absoluteVelocityParameterId, 0.0f, m_id);
			}

#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)
			if (enable)
			{
				m_parameterInfo[g_szAbsoluteVelocityParameterName] = 0.0f;
			}
			else
			{
				m_parameterInfo.erase(g_szAbsoluteVelocityParameterName);
			}
#endif          // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE

			break;
		}
	case EObjectFunctionality::TrackRelativeVelocity:
		{
			if (enable)
			{
				if ((m_flags& EObjectFlags::TrackRelativeVelocity) == 0)
				{
					m_flags |= EObjectFlags::TrackRelativeVelocity;
					g_numObjectsWithRelativeVelocity++;
				}
			}
			else
			{
				if ((m_flags& EObjectFlags::TrackRelativeVelocity) != 0)
				{
					m_flags &= ~EObjectFlags::TrackRelativeVelocity;

					AK::SoundEngine::SetRTPCValue(s_relativeVelocityParameterId, 0.0f, m_id);

					CRY_ASSERT_MESSAGE(g_numObjectsWithRelativeVelocity > 0, "g_numObjectsWithRelativeVelocity is 0 but an object with relative velocity tracking still exists during %s", __FUNCTION__);
					g_numObjectsWithRelativeVelocity--;
				}
			}

#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)
			if (enable)
			{
				m_parameterInfo[g_szRelativeVelocityParameterName] = 0.0f;
			}
			else
			{
				m_parameterInfo.erase(g_szRelativeVelocityParameterName);
			}
#endif          // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE

			break;
		}
	default:
		{
			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CObject::DrawDebugInfo(IRenderAuxGeom& auxGeom, float const posX, float posY, char const* const szTextFilter)
{
#if defined(CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE)

	if (!m_parameterInfo.empty())
	{
		bool isVirtual = true;

		for (auto const pEventInstance : m_eventInstances)
		{
			if (pEventInstance->GetState() != EEventInstanceState::Virtual)
			{
				isVirtual = false;
				break;
			}
		}

		for (auto const& parameterPair : m_parameterInfo)
		{
			bool canDraw = true;

			if (szTextFilter != nullptr)
			{
				CryFixedStringT<MaxControlNameLength> lowerCaseParameterName(parameterPair.first);
				lowerCaseParameterName.MakeLower();

				if (lowerCaseParameterName.find(szTextFilter) == CryFixedStringT<MaxControlNameLength>::npos)
				{
					canDraw = false;
				}
			}

			if (canDraw)
			{
				auxGeom.Draw2dLabel(
					posX,
					posY,
					Debug::g_objectFontSize,
					isVirtual ? Debug::s_globalColorVirtual : Debug::s_objectColorParameter,
					false,
					"[Wwise] %s: %2.2f\n",
					parameterPair.first,
					parameterPair.second);

				posY += Debug::g_objectLineHeight;
			}
		}
	}

#endif  // CRY_AUDIO_IMPL_WWISE_USE_DEBUG_CODE
}
} // namespace Wwise
} // namespace Impl
} // namespace CryAudio
