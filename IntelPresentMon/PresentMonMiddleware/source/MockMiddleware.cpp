#include "MockMiddleware.h"
#include <cstring>
#include <string>
#include <vector>
#include <memory>

namespace pmid
{
	MockMiddleware::MockMiddleware() = default;

	void MockMiddleware::Speak(char* buffer) const
	{
		strcpy_s(buffer, 256, "mock-middle");
	}

	// implement intro string
	struct String : PM_INTROSPECTION_STRING
	{
		String(std::string s) : buffer_{ std::move(s) }
		{
			pData = buffer_.data();
		}
		String& operator=(std::string rhs)
		{
			buffer_ = std::move(rhs);
			pData = buffer_.data();
			return *this;
		}
	private:
		std::string buffer_;
	};
	// implement intro objarr
	template<class T>
	struct ObjArray : PM_INTROSPECTION_OBJARRAY
	{
		ObjArray()
			:
			PM_INTROSPECTION_OBJARRAY{ nullptr, 0 }
		{}
		ObjArray(std::vector<T*> v)
			:
			buffer_{ std::move(v) }
		{
			Sync_();
		}
		~ObjArray()
		{
			for (auto pObj : buffer_) {
				delete pObj;
			}
		}
		ObjArray& operator=(std::vector<T*> rhs)
		{
			buffer_ = std::move(rhs);
			Sync_();
			return *this;
		}
		void PushBack(std::unique_ptr<T> pObj)
		{
			buffer_.push_back(pObj.release());
			Sync_();
		}
	private:
		void Sync_()
		{
			pData = (const void**)buffer_.data();
			size = buffer_.size();
		}
		std::vector<T*> buffer_;
	};
	// implement intro enum structs
	struct EnumKey : PM_INTROSPECTION_ENUM_KEY
	{
		EnumKey(PM_ENUM enumId_, int value_, std::string symbol, std::string name, std::string description)
		{
			enumId = enumId_;
			value = value_;
			pSymbol = new String{ std::move(symbol) };
			pName = new String{ std::move(name) };
			pDescription = new String{ std::move(description) };
		}
		~EnumKey()
		{
			delete static_cast<String*>(pSymbol);
			delete static_cast<String*>(pName);
			delete static_cast<String*>(pDescription);
		}
	};
	struct Enum : PM_INTROSPECTION_ENUM
	{
		Enum(PM_ENUM id_, std::string symbol, std::string description)
		{
			id = id_;
			pSymbol = new String{ std::move(symbol) };
			pDescription = new String{ std::move(description) };
			pKeys = new ObjArray<EnumKey>();
		}
		~Enum()
		{
			delete static_cast<String*>(pSymbol);
			delete static_cast<String*>(pDescription);
			delete &Keys_();
		}
		void AddKey(std::unique_ptr<EnumKey> pKey)
		{
			Keys_().PushBack(std::move(pKey));
		}
	private:
		ObjArray<EnumKey>& Keys_()
		{
			return *static_cast<ObjArray<EnumKey>*>(pKeys);
		}
	};
	struct IntrospectionRoot : PM_INTROSPECTION_ROOT
	{
		IntrospectionRoot()
		{
			pMetrics = nullptr;
			pEnums = new ObjArray<Enum>();
		}
		~IntrospectionRoot()
		{
			delete &Enums_();
		}
		void AddEnum(std::unique_ptr<Enum> pEnum)
		{
			Enums_().PushBack(std::move(pEnum));
		}
	private:
		ObjArray<Enum>& Enums_()
		{
			return *static_cast<ObjArray<Enum>*>(pEnums);
		}
	};

	// implement enum annotation mechanics
	#define ENUM_KEY_LIST_PM_UNIT \
		X_(PM_UNIT_DIMENSIONLESS, "", "Dimensionless metric") \
		X_(PM_UNIT_FPS, "FPS", "Rate of application frames being presented per unit time") \
		X_(PM_UNIT_MILLISECONDS, "ms", "Time duration in milliseconds") \
		X_(PM_UNIT_WATTS, "Watts", "Power in watts (Joules per second)") \
		X_(PM_UNIT_PERCENT, "%", "Proportion or ratio represented as a fraction of 100")
	#define ENUM_KEY_LIST_PM_STATUS \
		X_(PM_STATUS_SUCCESS, "Success", "Operation succeeded") \
		X_(PM_STATUS_FAILURE, "Failure", "Operation failed") \
		X_(PM_STATUS_SESSION_NOT_OPEN, "Session Not Open", "Operation failed because session was not open")

	// invoke key list by concatenating with symbol from x macro list of master enum
	// switch on master will tell us whether all enums are registered

	// function not reference or called, but compiling this allows compiler to check if all enums/enum keys have coverage
	bool ValidateEnumCompleteness()
	{
		switch (PM_UNIT::PM_UNIT_DIMENSIONLESS) {
#define X_(key, name, description) case key: return false;
		ENUM_KEY_LIST_PM_UNIT
#undef X_
		}
		return true;
	}

#define XSTR_(macro) #macro
#define STRINGIFY_MACRO_CALL(macro) XSTR_(macro)
#define MAKE_MASTER_SYMBOL(base_symbol) PM_ENUM_ ## base_symbol
#define MAKE_ENUM_SYMBOL(base_symbol) PM_ ## base_symbol
#define CREATE_INTROSPECTION_ENUM(base_symbol, description) \
	[=]() { \
		auto pEnum = std::make_unique<Enum>(MAKE_MASTER_SYMBOL(base_symbol), STRINGIFY_MACRO_CALL(MAKE_ENUM_SYMBOL(base_symbol)), description); \
		return pEnum; \
	}()
#define MAKE_LIST_SYMBOL(base_symbol) ENUM_KEY_LIST_PM_ ## base_symbol
#define REGISTER_ENUM_KEY(p_enum_obj, base_symbol, key_symbol, name, description) p_enum_obj->AddKey(std::make_unique<EnumKey>(MAKE_MASTER_SYMBOL(base_symbol), key_symbol, #key_symbol, name, description))

	const PM_INTROSPECTION_ROOT* MockMiddleware::GetIntrospectionData() const
	{		
		auto pRoot = std::make_unique<IntrospectionRoot>();
		auto pEnum1 = std::make_unique<Enum>(PM_ENUM::PM_ENUM_UNIT, "PM_UNIT", "Units of measurement for metrics");
		pEnum1->AddKey(std::make_unique<EnumKey>(PM_ENUM::PM_ENUM_UNIT, PM_UNIT::PM_UNIT_FPS, "PM_UNIT_FPS", "FPS", "Rate of application frames being presented per unit time"));
		pEnum1->AddKey(std::make_unique<EnumKey>(PM_ENUM::PM_ENUM_UNIT, PM_UNIT::PM_UNIT_WATTS, "PM_UNIT_WATTS", "Watts", "Power in watts (Joules per second)"));
		pRoot->AddEnum(std::move(pEnum1));
		auto pEnum2 = std::make_unique<Enum>(PM_ENUM::PM_ENUM_STAT, "PM_STAT", "Statistical derivatives of metrics, as well as the raw unprocessed metric.");
		pEnum2->AddKey(std::make_unique<EnumKey>(PM_ENUM::PM_ENUM_STAT, PM_STAT::PM_STAT_AVG, "PM_STAT_AVG", "Average", "Average (mean) value of metric samples as calculated over a sliding window."));
		pEnum2->AddKey(std::make_unique<EnumKey>(PM_ENUM::PM_ENUM_STAT, PM_STAT::PM_STAT_MIN, "PM_STAT_MIN", "Minimum", "Minimum value of metric samples within a sliding window."));
		pRoot->AddEnum(std::move(pEnum2));
		auto pEnum3 = CREATE_INTROSPECTION_ENUM(UNIT, "units of measure");
		REGISTER_ENUM_KEY(pEnum3, UNIT, PM_UNIT_FPS, "FPS", "Frames per second");
		pRoot->AddEnum(std::move(pEnum3));
		return pRoot.release();
	}	

	void MockMiddleware::FreeIntrospectionData(const PM_INTROSPECTION_ROOT* pRoot) const
	{
		delete static_cast<const IntrospectionRoot*>(pRoot);
	}
}