#pragma once
#include "../../PresentMonAPI2/source/PresentMonAPI.h"
#include "../../PresentMonAPIWrapperCommon/source/Introspection.h"
#include <format>

namespace pmapi
{
    class SessionException : public Exception { using Exception::Exception; };

	class Session
	{
    public:
        Session();
        Session(Session&& rhs) noexcept
            :
            token{ rhs.token }
        {
            rhs.token = false;
        }
        Session& operator=(Session&& rhs) noexcept
        {
            token = rhs.token;
            rhs.token = false;
            return *this;
        }
        ~Session()
        {
            if (token) {
                pmCloseSession();
            }
        }
        intro::Dataset GetIntrospectionDataset() const
        {
            // throw an exception on error or non-token
            if (!token) {
                throw SessionException{ "introspection call failed due to empty session object" };
            }
            const PM_INTROSPECTION_ROOT* pRoot{};
            if (auto sta = pmEnumerateInterface(&pRoot); sta != PM_STATUS_SUCCESS) {
                throw SessionException{ std::format("introspection call failed with error id={}", (int)sta) };
            }
            return { pRoot, [](const PM_INTROSPECTION_ROOT* ptr) { pmFreeInterface(ptr); } };
        }
    private:
        bool token = true;
	};
}