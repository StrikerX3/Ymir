#pragma once

#include <stdint.h>

#include <nlohmann/json.hpp>

// if only there was a standard library function for this
template <size_t Len>
inline size_t StringCopy(char (&dest)[Len], const char* src)
{
	if (!src || !Len)
	{
		return 0;
	}
	size_t copied;
	char* out = dest;
	for (copied = 1; *src && copied < Len; ++copied)
	{
		*out++ = *src++;
	}
	*out = 0;
	return copied - 1;
}

size_t JsonWriteHandshakeObj(char* dest, size_t maxLen, int version, const char* applicationId);

// Commands
struct DiscordRichPresence;
size_t JsonWriteRichPresenceObj(char* dest,
	size_t maxLen,
	int nonce,
	int pid,
	const DiscordRichPresence* presence);
size_t JsonWriteSubscribeCommand(char* dest, size_t maxLen, int nonce, const char* evtName);

size_t JsonWriteUnsubscribeCommand(char* dest, size_t maxLen, int nonce, const char* evtName);

size_t JsonWriteJoinReply(char* dest, size_t maxLen, const char* userId, int reply, int nonce);

class JsonDocument
{
public:
	void ParseInsitu(char* data)
	{
		if (!data)
		{
			value = nlohmann::json::object();
			return;
		}

		try
		{
			value = nlohmann::json::parse(data);
		}
		catch (...)
		{
			value = nlohmann::json::object();
		}
	}

	nlohmann::json value = nlohmann::json::object();
};

using JsonValue = nlohmann::json;

inline JsonValue* GetObjMember(JsonValue* obj, const char* name)
{
	if (!obj || !obj->is_object())
	{
		return nullptr;
	}

	auto it = obj->find(name);
	if (it != obj->end() && it->is_object())
	{
		return &(*it);
	}

	return nullptr;
}

inline JsonValue* GetObjMember(JsonDocument* obj, const char* name)
{
	if (!obj)
	{
		return nullptr;
	}

	return GetObjMember(&obj->value, name);
}

inline int GetIntMember(JsonValue* obj, const char* name, int notFoundDefault = 0)
{
	if (!obj || !obj->is_object())
	{
		return notFoundDefault;
	}

	auto it = obj->find(name);
	if (it != obj->end() && it->is_number_integer())
	{
		return it->get<int>();
	}

	return notFoundDefault;
}

inline int GetIntMember(JsonDocument* obj, const char* name, int notFoundDefault = 0)
{
	if (!obj)
	{
		return notFoundDefault;
	}

	return GetIntMember(&obj->value, name, notFoundDefault);
}

inline const char* GetStrMember(JsonValue* obj,
	const char* name,
	const char* notFoundDefault = nullptr)
{
	if (!obj || !obj->is_object())
	{
		return notFoundDefault;
	}

	auto it = obj->find(name);
	if (it != obj->end() && it->is_string())
	{
		return it->get_ref<const std::string&>().c_str();
	}

	return notFoundDefault;
}

inline const char* GetStrMember(JsonDocument* obj,
	const char* name,
	const char* notFoundDefault = nullptr)
{
	if (!obj)
	{
		return notFoundDefault;
	}

	return GetStrMember(&obj->value, name, notFoundDefault);
}
