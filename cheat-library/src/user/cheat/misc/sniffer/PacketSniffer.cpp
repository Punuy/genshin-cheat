#include "pch-il2cpp.h"
#include "PacketSniffer.h"

#include "SnifferWindow.h"

#include <fstream>

#include <helpers.h>

namespace cheat::feature 
{

	static int32_t KcpNative_kcp_client_send_packet_Hook(void* __this, void* kcp_client, app::KcpPacket_1* packet, MethodInfo* method);
	static bool KcpClient_TryDequeueEvent_Hook(void* __this, app::ClientKcpEvent* evt, MethodInfo* method);

	PacketSniffer::PacketSniffer() : Feature(),
		NF(m_CapturingEnabled, "Capturing", "PacketSniffer", false),
		NF(m_ManipulationEnabled, "Manipulation", "PacketSniffer", false),
		NF(m_PipeEnabled, "Pipe", "PacketSniffer", false),
		NF(m_ProtoDirPath, "Proto Dir Path", "PacketSniffer", ""),
		NF(m_ProtoIDFilePath, "Proto ID File Path", "PacketSniffer", ""),

		m_ProtoManager(),
		m_NextTimeToConnect(0),
		m_Pipe({ "genshin_packet_pipe" })
	{
		m_ProtoManager.Load(m_ProtoIDFilePath, m_ProtoDirPath);
		HookManager::install(app::KcpNative_kcp_client_send_packet, KcpNative_kcp_client_send_packet_Hook);
		HookManager::install(app::KcpClient_TryDequeueEvent, KcpClient_TryDequeueEvent_Hook);

		if (m_CapturingEnabled && m_PipeEnabled && !TryConnectToPipe())
			LOG_WARNING("Failed connect to pipe.");
	}

	const FeatureGUIInfo& PacketSniffer::GetGUIInfo() const
	{
		static const FeatureGUIInfo info{ "Packet Sniffer", "Settings", true };
		return info;
	}

	bool PacketSniffer::OnCapturingChanged()
	{
		if (!m_CapturingEnabled)
			return true;

		if (!m_ProtoDirPath.value().empty() && !m_ProtoIDFilePath.value().empty())
		{
			m_ProtoManager.LoadIDFile(m_ProtoIDFilePath);
			m_ProtoManager.LoadProtoDir(m_ProtoDirPath);
			return true;
		}

		return false;
	}

	void PacketSniffer::DrawMain()
	{
		//ImGui::Text("Dev: for working needs server for named pipe 'genshin_packet_pipe'.\nCheck 'packet-handler' project like example.");
		if (ConfigWidget(m_CapturingEnabled, "Enabling capturing of packet info and sending to pipe, if it exists."))
		{ 
			bool result = OnCapturingChanged();
			if (!result)
			{
				m_CapturingEnabled = false;
				ImGui::OpenPopup("Error");
			}
		}
		
		if (ImGui::BeginPopup("Error"))
		{
			ImGui::Text("Please fill 'Proto Dir Path' and 'Proto ID File Path' before enabling capture.");
			ImGui::EndPopup();
		}
		auto& window = sniffer::SnifferWindow::GetInstance();
		ConfigWidget(window.m_Show, "Show capturing window.");

		ConfigWidget(m_PipeEnabled, "Enable sending of packet data to pipe with name 'genshin_packet_pipe'.\n"\
			"This feature can be used with external monitoring tools.");
		//ConfigWidget(m_ManipulationEnabled, "Enabling manipulation packet feature, that allows to replace, block incoming/outcoming packets." \
		//	"\nThis feature often needs, to read-write pipe operation, so can decrease network bandwidth.");

		if (m_CapturingEnabled)
		{
			ImGui::Text("These parameters can only be changed when 'Capturing' is disabled.");
			ImGui::BeginDisabled();
		}

		ConfigWidget(m_ProtoDirPath, "Path to directory containing Genshin .proto files.");
		ConfigWidget(m_ProtoIDFilePath, "Path to JSON file containing packet id->packet name info.");

		if (m_CapturingEnabled)
			ImGui::EndDisabled();
	}
	
	void PacketSniffer::DrawExternal()
	{
		auto& window = sniffer::SnifferWindow::GetInstance();
		if (window.m_Show)
			window.Draw();
	}

	PacketSniffer& PacketSniffer::GetInstance()
	{
		static PacketSniffer instance;
		return instance;
	}

	void PacketSniffer::ProcessUnionMessage(const PacketData& packetData)
	{
		nlohmann::json cmdListObject = nlohmann::json::parse(packetData.messageJson);

		for (auto& cmd : cmdListObject["cmdList"])
		{
			uint32_t id = cmd["messageId"];
			std::string body = cmd["body"];
			auto bodyBytes = util::base64_decode(body);

			auto combatJsonString = m_ProtoManager.GetJson(id, bodyBytes);
			if (!combatJsonString)
				continue;

			if (id != 347)
			{
				PacketData newPacketData;
				newPacketData.headData = packetData.headData;
				newPacketData.headJson = packetData.headJson;
				newPacketData.messageId = id;
				newPacketData.messageData = bodyBytes;
				newPacketData.messageJson = *combatJsonString;
				auto name = m_ProtoManager.GetName(packetData.messageId);
				newPacketData.name = !name ? "<Unknown>" : *name;
				newPacketData.type = packetData.type;
				newPacketData.valid = true;

				sniffer::PacketInfo packetInfo = sniffer::PacketInfo(newPacketData);
				sniffer::SnifferWindow::GetInstance().OnPacketIO(packetInfo);
				continue;
			}

			auto combatJsonObject = nlohmann::json::parse(*combatJsonString);
			for (auto& invokeJson : combatJsonObject["invokeList"])
			{
				std::string argumentType = invokeJson["argumentType"];
				static std::map<std::string, std::string> typeMap = {
					{ "ENTITY_MOVE", "EntityMoveInfo" },
					{ "COMBAT_EVT_BEING_HIT", "EvtBeingHitInfo" },
					{ "COMBAT_ANIMATOR_STATE_CHANGED", "EvtAnimatorStateChangedInfo" },
					{ "COMBAT_FACE_TO_DIR", "EvtFaceToDirInfo" },
					{ "COMBAT_SET_ATTACK_TARGET", "EvtSetAttackTargetInfo" },
					{ "COMBAT_RUSH_MOVE", "EvtRushMoveInfo" },
					{ "COMBAT_ANIMATOR_PARAMETER_CHANGED", "EvtAnimatorParameterInfo" },
					{ "SYNC_ENTITY_POSITION", "EvtSyncEntityPositionInfo" },
					{ "COMBAT_STEER_MOTION_INFO", "EvtCombatSteerMotionInfo" },
					{ "COMBAT_FORCE_SET_POSITION_INFO", "EvtCombatForceSetPosInfo" },
					{ "COMBAT_COMPENSATE_POS_DIFF", "EvtCompensatePosDiffInfo" },
					{ "COMBAT_MONSTER_DO_BLINK", "EvtMonsterDoBlink" },
					{ "COMBAT_FIXED_RUSH_MOVE", "EvtFixedRushMove" },
					{ "COMBAT_SYNC_TRANSFORM", "EvtSyncTransform" },
					{ "COMBAT_LIGHT_CORE_MOVE", "EvtLightCoreMove" }
				};

				if (typeMap.count(argumentType) == 0)
				{
					LOG_WARNING("Failed to find argument type %s", argumentType.c_str());
					continue;
				}

				PacketData newPacketData;
				newPacketData.name = typeMap[argumentType];
				newPacketData.messageId = packetData.messageId;
				newPacketData.headData = packetData.headData;
				newPacketData.headJson = packetData.headJson;
				newPacketData.messageData = util::base64_decode(invokeJson["combatData"]);

				auto jsonData = m_ProtoManager.GetJson(newPacketData.name, newPacketData.messageData);
				newPacketData.messageJson = jsonData ? *jsonData : "";
				newPacketData.type = packetData.type;
				newPacketData.valid = true;

				sniffer::SnifferWindow::GetInstance().OnPacketIO(sniffer::PacketInfo(newPacketData));
			}
		}
	}

	bool PacketSniffer::OnPacketIO(app::KcpPacket_1* packet, PacketType type)
	{
		if (!m_CapturingEnabled)
			return true;

		PacketData packetData = ParseRawPacketData((char*)packet->data, packet->dataLen);
		if (!packetData.valid)
			return true;

		auto name = m_ProtoManager.GetName(packetData.messageId);
		if (!name)
			return true;
		packetData.name = *name;
		packetData.type = type;

		auto message = m_ProtoManager.GetJson(packetData.messageId, packetData.messageData);
		if (!message)
			return true;
		packetData.messageJson = *message;

		if (packetData.messageId == 55)
			ProcessUnionMessage(packetData);

		sniffer::PacketInfo info(packetData);
		sniffer::SnifferWindow::GetInstance().OnPacketIO(info);

		if (!m_PipeEnabled || (!m_Pipe.IsPipeOpened() && !TryConnectToPipe()))
			return true;

		packetData.waitForModifyData = false; // m_ManipulationEnabled;
		SendData(packetData);

		//if (m_ManipulationEnabled)
		//{
		//	auto modifyData = ReceiveData();
		//	if (modifyData.type == PacketModifyType::Blocked)
		//		return false;

		//	if (modifyData.type == PacketModifyType::Modified)
		//	{
		//		auto dataSize = modifyData.modifiedData.size();
		//		packet->packetData = new byte[dataSize]();
		//		memcpy_s(packet->packetData, dataSize, modifyData.modifiedData.packetData(), dataSize);
		//		packet->dataLen = dataSize;
		//	}
		//}
		return true;
	}

	bool PacketSniffer::TryConnectToPipe()
	{
		std::time_t currTime = std::time(0);
		if (m_NextTimeToConnect > currTime)
			return false;
		
		bool result = m_Pipe.Connect();
		if (result)
			LOG_INFO("Connected to pipe successfully.");
		else
			m_NextTimeToConnect = currTime + 5; // delay in 5 sec
		return result;
	}

	char* PacketSniffer::EncryptXor(void* content, uint32_t length)
	{
		app::Byte__Array* byteArray = (app::Byte__Array*)new char[length + 0x20];
		byteArray->max_length = length;
		memcpy_s(byteArray->vector, length, content, length);

		app::Packet_XorEncrypt(nullptr, &byteArray, length, nullptr);

		auto result = new char[length];
		memcpy_s(result, length, byteArray->vector, length);
		delete[] byteArray;

		return (char*)result;
	}

	bool PacketSniffer::isLittleEndian()
	{
		unsigned int i = 1;
		char* c = (char*)&i;
		return (*c);
	}

	PacketData PacketSniffer::ParseRawPacketData(char* encryptedData, uint32_t length)
	{
		// Decrypting packetData
		auto data = EncryptXor(encryptedData, length);

		uint16_t magicHead = read<uint16_t>(data, 0);

		if (magicHead != 0x4567)
		{
			LOG_ERROR("Head magic value for packet is not valid.");
			return {};
		}

		uint16_t magicEnd = read<uint16_t>(data, length - 2);
		if (magicEnd != 0x89AB)
		{
			LOG_ERROR("End magic value for packet is not valid.");
			return {};
		}

		uint16_t messageId = read<uint16_t>(data, 2);
		uint16_t headSize = read<uint16_t>(data, 4);
		uint32_t contenSize = read<uint32_t>(data, 6);

		if (length < headSize + contenSize + 12)
		{
			LOG_ERROR("Packet size is not valid.");
			return {};
		}

		PacketData packetData = {};
		packetData.valid = true;
		packetData.messageId = messageId;

		packetData.headData = std::vector<byte>((size_t)headSize, 0);
		memcpy_s(packetData.headData.data(), headSize, data + 10, headSize);

		packetData.messageData = std::vector<byte>((size_t)contenSize, 0);
		memcpy_s(packetData.messageData.data(), contenSize, data + 10 + headSize, contenSize);

		delete[] data;

		return packetData;
	}

	void PacketSniffer::SendData(PacketData& data)
	{
		if (m_Pipe.IsPipeOpened())
		{
			//LOG_DEBUG("%s packetData with mid %d.", magic_enum::enum_name(packetData.type).packetData(), packetData.messageId);
			m_Pipe.WriteObject(data);
		}
	}

	PacketModifyData PacketSniffer::ReceiveData()
	{
		PacketModifyData md{};
		if (m_Pipe.IsPipeOpened())
		{
			m_Pipe.ReadObject(md);
		}
		return md;
	}

	static bool KcpClient_TryDequeueEvent_Hook(void* __this, app::ClientKcpEvent* evt, MethodInfo* method)
	{
		auto result = CALL_ORIGIN(KcpClient_TryDequeueEvent_Hook, __this, evt, method);

		if (!result || evt->_evt.type != app::KcpEventType__Enum::EventRecvMsg ||
			evt->_evt.packet == nullptr || evt->_evt.packet->data == nullptr)
			return result;

		auto& sniffer = PacketSniffer::GetInstance();
		return sniffer.OnPacketIO(evt->_evt.packet, PacketType::Receive);
	}

	static int32_t KcpNative_kcp_client_send_packet_Hook(void* __this, void* kcp_client, app::KcpPacket_1* packet, MethodInfo* method)
	{
		auto& sniffer = PacketSniffer::GetInstance();
		if (!sniffer.OnPacketIO(packet, PacketType::Send))
			return 0;

		return CALL_ORIGIN(KcpNative_kcp_client_send_packet_Hook, __this, kcp_client, packet, method);
	}
}

