#include "serialization.h"
#include "discord_rpc.h"

#include <cstring>
#include <string>

namespace
{
	size_t WriteJsonToBuffer(char* dest, size_t maxLen, const nlohmann::json& obj)
	{
		if (!dest || maxLen == 0)
		{
			return 0;
		}

		const std::string payload = obj.dump();
		size_t writeLen = payload.size();
		if (writeLen >= maxLen)
		{
			writeLen = maxLen - 1;
		}

		if (writeLen > 0)
		{
			std::memcpy(dest, payload.data(), writeLen);
		}
		dest[writeLen] = 0;
		return writeLen;
	}

	std::string NonceString(int nonce)
	{
		return std::to_string(nonce);
	}
} // namespace

size_t JsonWriteRichPresenceObj(char* dest,
	size_t maxLen,
	int nonce,
	int pid,
	const DiscordRichPresence* presence)
{
	nlohmann::json root = nlohmann::json::object();
	root["nonce"] = NonceString(nonce);
	root["cmd"] = "SET_ACTIVITY";

	nlohmann::json args = nlohmann::json::object();
	args["pid"] = pid;

	if (presence != nullptr)
	{
		nlohmann::json activity = nlohmann::json::object();

		if (presence->state && presence->state[0])
		{
			activity["state"] = presence->state;
		}
		if (presence->details && presence->details[0])
		{
			activity["details"] = presence->details;
		}

		if (presence->startTimestamp || presence->endTimestamp)
		{
			nlohmann::json timestamps = nlohmann::json::object();
			if (presence->startTimestamp)
			{
				timestamps["start"] = presence->startTimestamp;
			}
			if (presence->endTimestamp)
			{
				timestamps["end"] = presence->endTimestamp;
			}
			activity["timestamps"] = std::move(timestamps);
		}

		if ((presence->largeImageKey && presence->largeImageKey[0]) ||
			(presence->largeImageText && presence->largeImageText[0]) ||
			(presence->smallImageKey && presence->smallImageKey[0]) ||
			(presence->smallImageText && presence->smallImageText[0]))
		{
			nlohmann::json assets = nlohmann::json::object();
			if (presence->largeImageKey && presence->largeImageKey[0])
			{
				assets["large_image"] = presence->largeImageKey;
			}
			if (presence->largeImageText && presence->largeImageText[0])
			{
				assets["large_text"] = presence->largeImageText;
			}
			if (presence->smallImageKey && presence->smallImageKey[0])
			{
				assets["small_image"] = presence->smallImageKey;
			}
			if (presence->smallImageText && presence->smallImageText[0])
			{
				assets["small_text"] = presence->smallImageText;
			}
			activity["assets"] = std::move(assets);
		}

		if ((presence->partyId && presence->partyId[0]) || presence->partySize ||
			presence->partyMax)
		{
			nlohmann::json party = nlohmann::json::object();
			if (presence->partyId && presence->partyId[0])
			{
				party["id"] = presence->partyId;
			}
			if (presence->partySize && presence->partyMax)
			{
				party["size"] = {presence->partySize, presence->partyMax};
			}
			activity["party"] = std::move(party);
		}

		if ((presence->matchSecret && presence->matchSecret[0]) ||
			(presence->joinSecret && presence->joinSecret[0]) ||
			(presence->spectateSecret && presence->spectateSecret[0]))
		{
			nlohmann::json secrets = nlohmann::json::object();
			if (presence->matchSecret && presence->matchSecret[0])
			{
				secrets["match"] = presence->matchSecret;
			}
			if (presence->joinSecret && presence->joinSecret[0])
			{
				secrets["join"] = presence->joinSecret;
			}
			if (presence->spectateSecret && presence->spectateSecret[0])
			{
				secrets["spectate"] = presence->spectateSecret;
			}
			activity["secrets"] = std::move(secrets);
		}

		activity["instance"] = (presence->instance != 0);
		args["activity"] = std::move(activity);
	}

	root["args"] = std::move(args);

	return WriteJsonToBuffer(dest, maxLen, root);
}

size_t JsonWriteHandshakeObj(char* dest, size_t maxLen, int version, const char* applicationId)
{
	nlohmann::json obj = nlohmann::json::object();
	obj["v"] = version;
	obj["client_id"] = applicationId ? applicationId : "";
	return WriteJsonToBuffer(dest, maxLen, obj);
}

size_t JsonWriteSubscribeCommand(char* dest, size_t maxLen, int nonce, const char* evtName)
{
	nlohmann::json obj = nlohmann::json::object();
	obj["nonce"] = NonceString(nonce);
	obj["cmd"] = "SUBSCRIBE";
	obj["evt"] = evtName ? evtName : "";
	return WriteJsonToBuffer(dest, maxLen, obj);
}

size_t JsonWriteUnsubscribeCommand(char* dest, size_t maxLen, int nonce, const char* evtName)
{
	nlohmann::json obj = nlohmann::json::object();
	obj["nonce"] = NonceString(nonce);
	obj["cmd"] = "UNSUBSCRIBE";
	obj["evt"] = evtName ? evtName : "";
	return WriteJsonToBuffer(dest, maxLen, obj);
}

size_t JsonWriteJoinReply(char* dest, size_t maxLen, const char* userId, int reply, int nonce)
{
	nlohmann::json obj = nlohmann::json::object();

	if (reply == DISCORD_REPLY_YES)
	{
		obj["cmd"] = "SEND_ACTIVITY_JOIN_INVITE";
	}
	else
	{
		obj["cmd"] = "CLOSE_ACTIVITY_JOIN_REQUEST";
	}

	obj["args"] = nlohmann::json::object({{"user_id", userId ? userId : ""}});
	obj["nonce"] = NonceString(nonce);

	return WriteJsonToBuffer(dest, maxLen, obj);
}
