// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "WbemListener.h"
#include "WbemSink.h"
#include <Core/source/infra/Logging.h>
#include <CommonUtilities/Exception.h>
#include <Core/source/infra/util/Util.h>
#include "Comdef.h"

namespace p2c::win::com
{
	using namespace ::pmon::util;
	using Microsoft::WRL::ComPtr;

	WbemListener::WbemListener(
		WbemSink* pSink_,
		Microsoft::WRL::ComPtr<IWbemServices> pConnection_)
		:
		pSink{ pSink_ },
		pConnection{ std::move(pConnection_) }
	{
		// Use an unsecured apartment for security
		// we can do this because we only use this object with
		// trusted WMI server objects / connections
		ComPtr<IUnsecuredApartment> pUnsecApp;
		if (auto hr = CoCreateInstance(
			CLSID_UnsecuredApartment,
			nullptr,
			CLSCTX_LOCAL_SERVER,
			IID_IUnsecuredApartment,
			&pUnsecApp
		); FAILED(hr))
		{
			pmlog_error(L"Failed to create unsecured apartment").hr(hr);
			throw Except<Exception>();
		}

		// create stub wrapping our sink object, forwarding to assist in async recv
		ComPtr<IUnknown> pStubUnk;
		if (auto hr = pUnsecApp->CreateObjectStub(pSink_, &pStubUnk); FAILED(hr))
		{
			pmlog_error(L"Failed to create stub for this sink").hr(hr);
			throw Except<Exception>();
		}
		if (auto hr = pStubUnk->QueryInterface<IWbemObjectSink>(&pStub); FAILED(hr))
		{
			pmlog_error(L"Failed to query sink interface from stub").hr(hr);
			throw Except<Exception>();
		}

		// Register the notification query
		if (auto hr = pConnection->ExecNotificationQueryAsync(
			_bstr_t("WQL"),
			_bstr_t(pSink->GetQueryString().c_str()),
			0,
			nullptr,
			pStub.Get()
		); FAILED(hr))
		{
			pmlog_error(std::format(L"Failed to execute notification query: ",
				infra::util::ToWide(pSink->GetQueryString())))
				.hr(hr);
			throw Except<Exception>();
		}
	}
	WbemListener::~WbemListener()
	{
		// cancel the notification query
		if (auto hr = pConnection->CancelAsyncCall(pStub.Get()); FAILED(hr))
		{
			try {
				pmlog_error(std::format(L"Failed to cancel notification query: ",
					infra::util::ToWide(pSink->GetQueryString())))
					.hr(hr);
				throw Except<Exception>();
			}
			catch (...) {}
		}
		// release sink reference
		pSink->Release();
	}
}