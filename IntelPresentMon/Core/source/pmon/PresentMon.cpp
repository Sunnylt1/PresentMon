// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#include "PresentMon.h"
#include <Core/source/infra/Logging.h>
#include <PresentMonAPI2/PresentMonAPI.h>
#include <PresentMonAPIWrapper/PresentMonAPIWrapper.h>
#include <PresentMonAPIWrapperCommon/EnumMap.h>
#include <Core/source/infra/util/Util.h>
#include "RawFrameDataWriter.h"

namespace p2c::pmon
{
	using namespace ::pmapi;

	class FrameEventFlusher
	{
	public:
		FrameEventFlusher(Session& sesh)
		{
			// register minimal query used for flushing frame events
			std::array queryElements{ PM_QUERY_ELEMENT{ PM_METRIC_PRESENT_MODE, PM_STAT_MID_POINT } };
			query_ = sesh.RegisterFrameQuery(queryElements);
			blobs_ = query_.MakeBlobContainer(50);
		}
		void Flush(ProcessTracker& tracker)
		{
			query_.ForEachConsume(tracker, blobs_, [](auto) {});
		}
	private:
		FrameQuery query_;
		BlobContainer blobs_;
	};

	PresentMon::PresentMon(std::optional<std::string> namedPipeName, std::optional<std::string> sharedMemoryName, double window_in, double offset_in, uint32_t telemetrySamplePeriodMs_in)
	{
		const auto RemoveDoubleQuotes = [](std::string s) {
			if (s.front() == '"' && s.back() == '"' && s.size() >= 2) {
				s = s.substr(1, s.size() - 2);
			}
			return s;
		};
		if (namedPipeName && sharedMemoryName) {
			auto pipeName = RemoveDoubleQuotes(*namedPipeName);
			auto shmName = RemoveDoubleQuotes(*sharedMemoryName);
			pmlog_info(std::format(L"Connecting to service with custom pipe [{}] and nsm [{}]",
				infra::util::ToWide(pipeName),
				infra::util::ToWide(shmName)
			));
			pSession = std::make_unique<pmapi::Session>(std::move(pipeName), std::move(shmName));
		}
		else {
			pmlog_info(L"Connecting to service with default pipe name");
			pSession = std::make_unique<pmapi::Session>();
		}

		// acquire introspection data
		pIntrospectionRoot = pSession->GetIntrospectionRoot();

		// establish initial sampling / window / processing setting values
		SetWindow(window_in);
		SetOffset(offset_in);
		SetGpuTelemetryPeriod(telemetrySamplePeriodMs_in);

		// create flusher used to clear out piled-up old frame events before capture
		pFlusher = std::make_unique<FrameEventFlusher>(*pSession);
	}
	PresentMon::~PresentMon() = default;
	void PresentMon::StartTracking(uint32_t pid_)
	{
		if (processTracker) {
			if (processTracker.GetPid() == pid_) {
				return;
			}
			pmlog_warn(std::format(L"Starting stream [{}] while previous stream [{}] still active",
				pid_, processTracker.GetPid()));
		}
		processTracker = pSession->TrackProcess(pid_);
	}
	void PresentMon::StopTracking()
	{
		if (!processTracker) {
			pmlog_warn(L"Cannot stop stream: no stream active");
			return;
		}
		const auto pid = processTracker.GetPid();
		processTracker.Reset();
		// TODO: caches cleared here maybe
		pmlog_info(std::format(L"stopped pmon stream for pid {}", pid));
	}
	double PresentMon::GetWindow() const { return window; }
	void PresentMon::SetWindow(double window_) { window = window_; }
	double PresentMon::GetOffset() const { return offset; }
	void PresentMon::SetOffset(double offset_) { offset = offset_; }
	void PresentMon::SetGpuTelemetryPeriod(uint32_t period)
	{
		pSession->SetTelemetryPollingPeriod(1, period);
		telemetrySamplePeriod = period;
	}
	uint32_t PresentMon::GetGpuTelemetryPeriod()
	{
		return telemetrySamplePeriod;
	}
	//std::wstring PresentMon::GetCpuName() const
	//{
	//	char buffer[512];
	//	uint32_t bufferSize = sizeof(buffer);
	//	if (auto sta = pmGetCpuName(buffer, &bufferSize); sta != PM_STATUS_SUCCESS) {
	//		pmlog_warn(L"could not get cpu name").code(sta);
	//		return {};
	//	}
	//	pmapi::PollStatic(*pSession, )
	//	if (bufferSize >= sizeof(buffer)) {
	//		pmlog_warn(std::format(L"insufficient buffer size to get cpu name. written: {}", bufferSize));
	//	}
	//	return infra::util::ToWide(std::string{ buffer, bufferSize });
	//}
	std::vector<AdapterInfo> PresentMon::EnumerateAdapters() const
	{
		std::vector<AdapterInfo> infos;
		for (const auto& info : pIntrospectionRoot->GetDevices()) {
			if (info.GetType() != PM_DEVICE_TYPE_GRAPHICS_ADAPTER) {
				continue;
			}
			infos.push_back(AdapterInfo{
				.id = info.GetId(),
				.vendor = info.IntrospectVendor().GetName(),
				.name = info.GetName(),
			});
		}
		return infos;
	}
	void PresentMon::SetAdapter(uint32_t id)
	{
		pmlog_info(std::format(L"Set active adapter to [{}]", id));
		if (id == 0) {
			pmlog_warn(L"Adapter was set to id 0; resetting");
			selectedAdapter.reset();
		}
		else {
			selectedAdapter = id;
		}
	}
	std::optional<uint32_t> PresentMon::GetPid() const {
		return bool(processTracker) ? processTracker.GetPid() : std::optional<uint32_t>{};
	}
	const pmapi::ProcessTracker& PresentMon::GetTracker() const
	{
		return processTracker;
	}
	std::shared_ptr<RawFrameDataWriter> PresentMon::MakeRawFrameDataWriter(std::wstring path,
		std::optional<std::wstring> statsPath, uint32_t pid, std::wstring procName)
	{
		// flush any buffered present events before starting capture
		pFlusher->Flush(processTracker);

		// make the frame data writer
		return std::make_shared<RawFrameDataWriter>(std::move(path), processTracker, std::move(procName),
			selectedAdapter.value_or(1), *pSession, std::move(statsPath), *pIntrospectionRoot);
	}
	std::optional<uint32_t> PresentMon::GetSelectedAdapter() const
	{
		return selectedAdapter;
	}
	const pmapi::intro::Root& PresentMon::GetIntrospectionRoot() const
	{
		return *pIntrospectionRoot;
	}
	pmapi::Session& PresentMon::GetSession()
	{
		return *pSession;
	}
}