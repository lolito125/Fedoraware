#include "LuaEngine.h"
#include <boost/algorithm/string/join.hpp>
#include "Interfaces.hpp"
#include "../Commands/Commands.h"

std::unordered_map<std::string, std::unordered_map<std::string, luabridge::LuaRef>> Callbacks;

/* Prints the last lua error */
void CLuaEngine::PrintError()
{
	std::string errorMessage = "LUA Error: ";
	errorMessage.append(lua_tostring(LuaState, -1));
	errorMessage.append("\n");

	I::Cvar->ConsoleColorPrintf({235, 59, 90, 255}, "%s", errorMessage.c_str());
	lua_pop(LuaState, -1);
}

/* Executes the given file */
void CLuaEngine::ExecuteFile(const char* file)
{
	if (!file || !LuaState) { return; }

	if (luaL_dofile(LuaState, file))
	{
		PrintError();
	}
}

/* Executes the given expression */
void CLuaEngine::ExecuteString(const char* expression)
{
	if (!expression || !LuaState)
	{
		I::Cvar->ConsoleColorPrintf({235, 59, 90, 255}, _("Lua Error: null expression passed to script engine\n"));
		return;
	}

	if (luaL_dostring(LuaState, expression))
	{
		PrintError();
	}
}

std::unordered_map<std::string, luabridge::LuaRef>& CLuaEngine::GetCallbacks(const std::string& type)
{
	return Callbacks[type];
}

void RegisterCallback(const char* type, const char* name, luabridge::LuaRef callback)
{
	if (callback.isFunction())
	{
		Callbacks[type].emplace(name, callback);
	}
}

void UnregisterCallback(const char* type, const char* name)
{
	Callbacks[type].erase(name);
}

void CLuaEngine::Init()
{
	LOCKLUA();

	/* Initialize LuaBridge */
	{
		ExportedInterfaces exInterfaces;

		using namespace luabridge;
		getGlobalNamespace(LuaState)
			.beginNamespace("Game")

			/* Utils */
			.beginNamespace("Utils")

			// Vec3
			.beginClass<Vec3>("Vec3")
			.addConstructor<void(*)(float, float, float)>()
			.addProperty("x", &Vec3::x)
			.addProperty("y", &Vec3::y)
			.addProperty("z", &Vec3::z)
			.endClass()

			.endNamespace()

			// CEngineClient
			.beginClass<WEngineClient>("EngineClient")
			.addFunction("GetLocalPlayer", &WEngineClient::GetLocalPlayer)
			.addFunction("ExecuteCommand", &WEngineClient::ExecuteCommand)
			.addFunction("IsConnected", &WEngineClient::IsConnected)
			.addFunction("IsInGame", &WEngineClient::IsInGame)
			.endClass()

			// CBaseEntity
			.beginClass<WBaseEntity>("BaseEntity")
			.addFunction("AddCond", &WBaseEntity::AddCond)
			.addFunction("GetAmmo", &WBaseEntity::GetAmmo)
			.addFunction("GetAbsOrigin", &WBaseEntity::GetAbsOrigin)
			.endClass()

			// Add Interfaces
			.beginClass<ExportedInterfaces>("InterfaceClass")
			.addFunction("GetEngine", &ExportedInterfaces::GetEngine)
			.addFunction("GetLocalPlayer", &ExportedInterfaces::GetLocalPlayer)
			.endClass()

			// Global Vars, Props and Functions
			.addProperty("Interfaces", &exInterfaces, false)
			.endNamespace()

			.beginNamespace("Draw")
			.addFunction("Text", LuaUtils::Text)
			.endNamespace()

			// Callbacks
			.beginNamespace("Callbacks")
			.addFunction("Register", RegisterCallback)
			.addFunction("Unregister", UnregisterCallback)
			.endNamespace()

			/* Global functions */
			.addFunction("print", LuaUtils::Print);
	}
	
	/* Register commands */
	{
		F::Commands.Register("lua_load", [&](const std::deque<std::string>& args) {
			if (args.empty())
			{
				F::Commands.Error("Usage: lua_load <path>");
				return;
			}

			ExecuteFile(args.front().c_str());
		});

		F::Commands.Register("lua_do", [&](const std::deque<std::string>& args) {
			if (args.empty())
			{
				F::Commands.Error("Usage: lua_do <expression>");
				return;
			}

			const std::string expr = boost::algorithm::join(args, " ");
			ExecuteString(expr.c_str());
		});
	}
}
