// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "BaseObject.h"

namespace CryAudio
{
namespace Impl
{
namespace Fmod
{
class CGlobalObject final : public CBaseObject
{
public:

	CGlobalObject(CGlobalObject const&) = delete;
	CGlobalObject(CGlobalObject&&) = delete;
	CGlobalObject& operator=(CGlobalObject const&) = delete;
	CGlobalObject& operator=(CGlobalObject&&) = delete;

	CGlobalObject(int const listenerMask, Listeners const& listeners);
	virtual ~CGlobalObject() override = default;

	// CryAudio::Impl::IObject
	virtual void SetOcclusion(IListener* const pIListener, float const occlusion, uint8 const numRemainingListeners) override;
	virtual void SetOcclusionType(EOcclusionType const occlusionType) override;
	virtual void ToggleFunctionality(EObjectFunctionality const type, bool const enable) override;
	// ~CryAudio::Impl::IObject
};
} // namespace Fmod
} // namespace Impl
} // namespace CryAudio
