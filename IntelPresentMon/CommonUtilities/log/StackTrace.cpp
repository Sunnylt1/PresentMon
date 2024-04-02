#include "StackTrace.h"
#include "../str/String.h"
#include <ranges>
#include "PanicLogger.h"

namespace pmon::util::log
{
	StackTrace::StackTrace(std::stacktrace trace)
		:
		trace_{ std::move(trace) }
	{}
	void StackTrace::Resolve()
	{
		if (trace_.empty()) {
			pmlog_panic_(L"Resolving empty trace");
			return;
		}
		else if (!frames_.empty()) {
			pmlog_panic_(L"Resolving when already resolved");
			return;
		}
		frames_.reserve(trace_.size());
		for (auto&& [i, f] : std::views::zip(std::views::iota(0), trace_)) {
			frames_.push_back(FrameInfo{
				.description = str::ToWide(f.description()),
				.file = str::ToWide(f.source_file()),
				.line = (int)f.source_line(),
				.index = i,
			});
		}
	}
	std::span<const StackTrace::FrameInfo> StackTrace::GetFrames() const
	{
		if (frames_.empty()) {
			pmlog_panic_(L"Getting frames from and unresolved/empty stack trace.");
		}
		return frames_;
	}
	bool StackTrace::Empty() const
	{
		return trace_.empty() && frames_.empty();
	}
	bool StackTrace::Resolved() const
	{
		return !frames_.empty();
	}
	std::unique_ptr<StackTrace> StackTrace::Here()
	{
		return std::make_unique<StackTrace>(std::stacktrace::current());
	}
}