/*
 *   Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU AGPL3 v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *   Copyright (C) 2013      Emu-Devstore <http://emu-devstore.com/>
 *
 *   Written by Teiby <http://www.teiby.de/>
 *   Adjusted by fr4z3n for azerothcore
 *   Reworked by XDev
 */

#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "BattlegroundQueue.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "Chat.h"

//Const for 1v1 arena
constexpr uint32 ARENA_TEAM_1V1 = 1;
constexpr uint32 ARENA_TYPE_1V1 = 1;
constexpr uint32 BATTLEGROUND_QUEUE_1V1 = 11;
constexpr BattlegroundQueueTypeId bgQueueTypeId = (BattlegroundQueueTypeId)((int)BATTLEGROUND_QUEUE_5v5 + 1);
uint32 ARENA_SLOT_1V1 = 3;

//Config
std::vector<uint32> forbiddenTalents;

class configloader_1v1arena : public WorldScript
{
public:
    configloader_1v1arena() : WorldScript("configloader_1v1arena") {}


    virtual void OnAfterConfigLoad(bool /*Reload*/) override
    {
        std::stringstream ss(sConfigMgr->GetOption<std::string>("Arena1v1.ForbiddenTalentsIDs", "0"));

        for (std::string blockedTalentsStr; std::getline(ss, blockedTalentsStr, ',');)
        {
            forbiddenTalents.push_back(stoi(blockedTalentsStr));
        }		
				
        ARENA_SLOT_1V1 = sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3);
        
        ArenaTeam::ArenaSlotByType.emplace(ARENA_TEAM_1V1, ARENA_SLOT_1V1);
        ArenaTeam::ArenaReqPlayersForType.emplace(ARENA_TYPE_1V1, 2);
        
        BattlegroundMgr::queueToBg.insert({ BATTLEGROUND_QUEUE_1V1,   BATTLEGROUND_AA });
        BattlegroundMgr::QueueToArenaType.emplace(BATTLEGROUND_QUEUE_1V1, (ArenaType) ARENA_TYPE_1V1);
        BattlegroundMgr::ArenaTypeToQueue.emplace(ARENA_TYPE_1V1, (BattlegroundQueueTypeId) BATTLEGROUND_QUEUE_1V1);
    }

};

class playerscript_1v1arena : public PlayerScript
{
public:
    playerscript_1v1arena() : PlayerScript("playerscript_1v1arena") { }

    void OnLogin(Player* pPlayer) override
    {
        if (sConfigMgr->GetOption<bool>("Arena1v1.Announcer", true))
            ChatHandler(pPlayer->GetSession()).SendSysMessage("This server is running the |cff4CFF00Arena 1v1 |rmodule.");
    }

    

    void GetCustomGetArenaTeamId(const Player* player, uint8 slot, uint32& id) const override
    {
        if (slot == sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_1V1))
            {
                id = at->GetId();
            }
        }
    }


    void GetCustomArenaPersonalRating(const Player* player, uint8 slot, uint32& rating) const override
    {
        if (slot == sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_1V1))
            {
                rating = at->GetRating();
            }
        }
    }


    void OnGetMaxPersonalArenaRatingRequirement(const Player* player, uint32 minslot, uint32& maxArenaRating) const override
    {
        if (sConfigMgr->GetOption<bool>("Arena1v1.VendorRating", false) && minslot < (uint32)sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_1V1))
            {
                maxArenaRating = std::max(at->GetRating(), maxArenaRating);
            }
        }
    }
};

class npc_1v1arena : public CreatureScript
{
public:
    npc_1v1arena() : CreatureScript("npc_1v1arena") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player || !creature)
            return true;

        if (sConfigMgr->GetOption<bool>("Arena1v1.Enable", true) == false)
        {
            ChatHandler(player->GetSession()).SendSysMessage("Las arenas 1v1 está desabilitada");
            return true;
        }

        if (player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Salir de la cola de la arena 1v1", GOSSIP_SENDER_MAIN, 3, "¿Estás seguro?", 0, false);
        }
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Entrar en la cola de la Arena 1v1 (sin calificación)", GOSSIP_SENDER_MAIN, 20);
        }

        if (!player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1V1)))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Crear un nuevo equipo para la Arena 1v1", GOSSIP_SENDER_MAIN, 1, "¿Estás seguro, tu honor y arena se reiniciara a 0?", sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000), false);
        }
        else
        {
            std::string pname = player->GetName();

            QueryResult Result = CharacterDatabase.Query("SELECT `type` FROM `arena_team` WHERE `name` = '{}'", pname);
			if (Result)
            {
                do
                {
                    Field* fields = Result->Fetch();
                    uint32 type = fields[0].Get<int32>();
                    
                    if (type)
                    {
                        if (!player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
                        {
                            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Entrar en la cola de la Arena 1v1 (con calificación)", GOSSIP_SENDER_MAIN, 2);
                            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Borrar el equipo de la Arena", GOSSIP_SENDER_MAIN, 5, "¿Estás seguro, tu honor y arena se reiniciara a 0?", sConfigMgr->GetOption<uint64>("Arena1v1.Costsdel", 10000000), false);
                        }
                        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Mostrar tus estadísticas", GOSSIP_SENDER_MAIN, 4);

                    }
                    

                    
                } while (Result->NextRow());

                
            }

            if (!Result) {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Crear un nuevo equipo para la Arena 1v1", GOSSIP_SENDER_MAIN, 1, "¿Estás seguro, tu honor y arenas se reiniciaran a 0?", sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000), false);
            }
            
		}

        SendGossipMenuFor(player, 68, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (!player || !creature)
            return true;

        ClearGossipMenuFor(player);

        ChatHandler handler(player->GetSession());

        switch (action)
        {
        case 1: // Create new Arenateam
        {
            if (sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 80) <= player->getLevel())
            {
                if (player->GetMoney() >= uint32(sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000)) && CreateArenateam(player, creature))
                    player->ModifyMoney(sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000) * -1);
            }
            else
            {
                handler.PSendSysMessage("Debes ser de nivel %u o superior para crear un equipo de arena 1v1", sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 70));
                CloseGossipMenuFor(player);
                return true;
            }
        }
        break;

        case 2: // Join Queue Arena (rated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, true))
                handler.SendSysMessage("Algo salió mal al unirse a la cola");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 20: // Join Queue Arena (unrated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, false))
                handler.SendSysMessage("Algo salió mal al unirse a la cola");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 3: // Leave Queue
        {
            uint8 arenaType = ARENA_TYPE_1V1;

            if (!player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
                return true;

            WorldPacket data;
            data << arenaType << (uint8)0x0 << (uint32)BATTLEGROUND_AA << (uint16)0x0 << (uint8)0x0;
            player->GetSession()->HandleBattleFieldPortOpcode(data);
            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 4: // get statistics
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1V1)));
            if (at)
            {
                std::stringstream s;
                s << "Calificación: " << at->GetStats().Rating;
                s << "\nRango: " << at->GetStats().Rank;
                s << "\nPartidas: " << at->GetStats().SeasonGames;
                s << "\nVictorias: " << at->GetStats().SeasonWins;
                s << "\nPartidas de la semana: " << at->GetStats().WeekGames;
                s << "\nVictorias de la semana: " << at->GetStats().WeekWins;

                ChatHandler(player->GetSession()).PSendSysMessage(SERVER_MSG_STRING, s.str().c_str());
            }
        }
        break;

        case 5: // Disband arenateam
        {
            if (player->GetMoney() >= uint32(sConfigMgr->GetOption<uint64>("Arena1v1.Costsdel", 10000000)))
            {
                player->ModifyMoney(sConfigMgr->GetOption<uint64>("Arena1v1.Costsdel", 10000000) * -1);
                WorldPacket Data;
                Data << player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_1V1));
                player->GetSession()->HandleArenaTeamLeaveOpcode(Data);
                handler.SendSysMessage("Team de Arena Borrado!");
                CloseGossipMenuFor(player);
                
            }
            return true;
        }
        break;
        }

        OnGossipHello(player, creature);
        return true;
    }

private:
    bool JoinQueueArena(Player* player, Creature* me, bool isRated)
    {
        if (!player || !me)
            return false;

        if (sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 80) > player->getLevel())
            return false;

        uint8 arenaslot = ArenaTeam::GetSlotByType(ARENA_TEAM_1V1);
        uint8 arenatype = ARENA_TYPE_1V1;
        uint32 arenaRating = 0;
        uint32 matchmakerRating = 0;

        // ignore if we already in BG or BG queue
        if (player->InBattleground())
            return false;

        //check existance
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        if (!bg)
        {
            LOG_ERROR("module", "Battleground: template bg (all arenas) not found");
            return false;
        }

        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
            return false;
        }

        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
        if (!bracketEntry)
            return false;

        // check if already in queue
        if (player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            return false; // //player is already in this queue

        // check if has free queue slots
        if (!player->HasFreeBattlegroundQueueId())
            return false;

        uint32 ateamId = 0;

        if (isRated)
        {
            ateamId = player->GetArenaTeamId(arenaslot);
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
            if (!at)
            {
                player->GetSession()->SendNotInArenaTeamPacket(arenatype);
                return false;
            }

            // get the team rating for queueing
            arenaRating = at->GetRating();
            matchmakerRating = arenaRating;
            // the arenateam id must match for everyone in the group

            if (arenaRating <= 0)
                arenaRating = 1;
        }

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        BattlegroundTypeId bgTypeId = BATTLEGROUND_AA;

        bg->SetRated(isRated);
        bg->SetMaxPlayersPerTeam(1);

        GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bgTypeId, bracketEntry, arenatype, isRated != 0, false, arenaRating, matchmakerRating, ateamId, 0);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        // send status packet (in queue)
        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, TEAM_NEUTRAL, isRated);
        player->GetSession()->SendPacket(&data);

        sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, arenatype, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());

        return true;
    }

    bool CreateArenateam(Player* player, Creature* me)
    {
        if (!player || !me)
            return false;

        
        player->ModifyArenaPoints(-10000);
        player->ModifyHonorPoints(-75000);

        uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_1V1);
        //Just to make sure as some other module might edit this value
        if (slot == 0)
            return false;

        // Check if player is already in an arena team
        if (player->GetArenaTeamId(slot))
        {
            player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "¡Ya estás en un equipo de arena!", ERR_ALREADY_IN_ARENA_TEAM);
            return false;
        }

        // Teamname = playername
        // if teamname exist, we have to choose another name (playername + number)
        int i = 1;
        std::stringstream teamName;
        teamName << player->GetName();
        do
        {
            if (sArenaTeamMgr->GetArenaTeamByName(teamName.str()) != NULL) // teamname exist, so choose another name
            {
                teamName.str(std::string());
                teamName << player->GetName() << (i++);
            }
            else
                break;
        } while (i < 100); // should never happen

        // Create arena team
        ArenaTeam* arenaTeam = new ArenaTeam();
        if (!arenaTeam->Create(player->GetGUID(), ARENA_TEAM_1V1, teamName.str(), 4283124816, 45, 4294242303, 5, 4294705149))
        {
            delete arenaTeam;
            return false;
        }

        // Register arena team
        sArenaTeamMgr->AddArenaTeam(arenaTeam);

        ChatHandler(player->GetSession()).SendSysMessage("¡Equipo de arena 1v1 creado exitosamente!");

        std::string frase[43] = {
            
            "te desafía a que pruebes que eres capaz de vencerlo.",
            "no cree que puedes su mi nivel!",
            "dice que si quieres enfrentarte a alguien de verdad, que lo desafies",
            "te reta a que intentes superar su habilidad.",
            "no teme a ningún reto, ¿te atreves a enfrentarte a él?",
            "no se deja vencer fácilmente, ¿tienes lo que se necesita para superarlo?",
            "está listo para cualquier desafío, ¿y tú?",
            "ha enfrentado a los enemigos más temibles de Azeroth, ¿te atreves a desafiarlo?",
            "ha luchado en las batallas más épicas de la historia de WoW, ¿crees que eres lo suficientemente fuerte para enfrentarlo?",
            "ha explorado los rincones más peligrosos de Azeroth, ¿estás listo para seguir sus pasos?",
            "puede hacerte morder el polvo más rápido que un elfo corriendo de un enano borracho.",
            "tal vez debería retarme en arena, claro si crees que tienes lo que se necesita. Si no, mejor ve a hacer una misión de recolección de hierbas.",
            "puede hacerte sudar más que un ogro en un sauna.",
            "dice que lo retes, que te enseñara la habiliadad del MORDEDOR DE ALMOHADA",
            "dice que es rey de Azeroth, y tú eres solo una hormiga en su camino.",
            "te está esperando, pero tal vez deberías practicar primero tu Huida veloz o te verás muy ridículo tratando de escapar.",
            "te reta a las arenas, pero no se hace responsable de tus huesos rotos o de tu ego lastimado.",
            "no solo es fuerte, también es listo. ¿Crees que puedes vencerlo en inteligencia? Tal vez deberías practicar primero con un gnomo.",
            "es tan fuerte que puede levantar más peso que un Tauren. ¿Crees que puedes derrotarlo en un duelo? Tal vez deberías intentarlo en otra vida.",
            "no es solo un nombre, es un estado de ánimo. ¿Estás listo para enfrentarte a su mentalidad épica?",
            "te reta a un duelo, pero tal vez deberías entrenar primero para no morir como un personaje de nivel 1.",
            "está listo para luchar, ¿estás listo para hacer frente a su furia de gladiador?",
            "es tan feroz como un dragón enojado. ¿Estás listo para desafiar su ira?",
            "es el rey de la arena. Si quieres vencerlo, necesitarás algo más que suerte.",
            "dice que si quieres una verdadera batalla, desafialo. Pero no te sorprendas si terminas con más heridas que un personaje de bajo nivel en una mazmorra.",
            "es más peligroso que Kil'jaeden en su forma más poderosa. ¿Estás seguro de que quieres desafiarlo?",
            "dice que si quieres busques a tu amigo paladin, si el de las sentencias de 25k",
            "es más astuto que Illidan y más mortal que Arthas. ¿Estás preparado para enfrentarte a su oscuridad?",
            "es como una lucha contra Al'Akir: un verdadero desafío. ¿Crees que puedes superarlo?",
            "es más rápido que la velocidad de ataque de Lady Vashj. ¿Crees que puedes superarlo?",
            "es más hábil que la mayoría de los jefes de las mazmorras. ¿Estás listo para enfrentarlo como si fuera Kel'Thuzad?",
            "es más poderoso que un ejército de No-Muertos liderados por el Rey Exánime. ¿Estás seguro de que quieres desafiarlo?",
            "dice que necesitarás más suerte que la que tiene un cazador con su equipo de agilidad.",
            "es tan despiadado como un grupo de bandidos gnolls. ¿Estás listo para enfrentarte a su ferocidad?",
            "es más duro que la pelea contra Alamuerte en su forma de dragón. ¿Crees que tienes lo que se necesita para vencerlo?",
            "es más implacable que un ataque de murlocs en la costa. ¿Estás listo para enfrentarlo como si fuera un jefe de raid?",
            "dice que si quieres derrotarlo, tendrás que tener tanta suerte como la que tiene un ladrón con un golpe crítico.",
            "es más hábil que un pícaro con su arsenal de habilidades. ¿Crees que puedes vencerlo?",
            "es más rápido que un corcel de guerra de la Alianza. ¿Crees que puedes mantener el ritmo?",
            "es tan letal como un grupo de dragones escupefuego. ¿Estás listo para enfrentarte a su furia?",
            "es más peligroso que un cazador con su mascota más letal. ¿Estás preparado para enfrentarlo?",
            "es tan implacable como un grupo de esqueletos en un cementerio. ¿Estás listo para enfrentarlo?",
            "es más poderoso que un grupo de demonios liderados por Archimonde. ¿Estás seguro de que quieres desafiarlo?"
   
        };
        std::srand(std::time(nullptr));
        int index = std::rand() % 43;
        

        std::string rewardMsg = "";
        std::string victimMsg = "";
        std::string rewardVal = "";

        rewardMsg.append("|cff676767[|cffFFFF00PVP 1v1|cff676767]|r:|cff4CFF00 ").append(player->GetName()).append(" |cffFF0000");
        rewardMsg.append(frase[index]);
        sWorld->SendServerMessage(SERVER_MSG_STRING, rewardMsg.c_str());

        return true;
    }

    bool Arena1v1CheckTalents(Player* player)
    {
        if (!player)
            return false;

        if (sConfigMgr->GetOption<bool>("Arena1v1.BlockForbiddenTalents", true) == false)
            return true;

        uint32 count = 0;

        for (uint32 talentId = 0; talentId < sTalentStore.GetNumRows(); ++talentId)
        {
            TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentId);

            if (!talentInfo)
                continue;

            if (std::find(forbiddenTalents.begin(), forbiddenTalents.end(), talentInfo->TalentID) != forbiddenTalents.end())
            {
                ChatHandler(player->GetSession()).SendSysMessage("No puedes unirte porque tienes talentos prohibidos");
                return false;
            }

            for (int8 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
                if (talentInfo->RankID[rank] == 0)
                    continue;
        }

        if (count >= 36)
        {
            ChatHandler(player->GetSession()).SendSysMessage("No puedes unirte porque tienes demasiados puntos de talento en un árbol prohibido (Sanación/Tanque)");
            return false;
        }

        return true;
    }
};

class team_1v1arena : public ArenaTeamScript
{
public:
    team_1v1arena() : ArenaTeamScript("team_1v1arena") {}


    void OnGetSlotByType(const uint32 type, uint8& slot) override
    {
        if (type == ARENA_TEAM_1V1)
        {
            slot = sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3);
        }
    }


    void OnGetArenaPoints(ArenaTeam* at, float& points) override
    {
        if (at->GetType() == ARENA_TEAM_1V1)
        {
            points *= sConfigMgr->GetOption<float>("Arena1v1.ArenaPointsMulti", 0.64f);
        }
    }


    void OnTypeIDToQueueID(const BattlegroundTypeId, const uint8 arenaType, uint32& _bgQueueTypeId) override
    {
        if (arenaType == ARENA_TYPE_1V1)
        {
            _bgQueueTypeId = bgQueueTypeId;
        }
    }


    void OnQueueIdToArenaType(const BattlegroundQueueTypeId _bgQueueTypeId, uint8& arenaType) override
    {
        if (_bgQueueTypeId == bgQueueTypeId)
        {
            arenaType = ARENA_TYPE_1V1;
        }
    }


    void OnSetArenaMaxPlayersPerTeam(const uint8 type, uint32& maxPlayersPerTeam) override
    {
        if (type == ARENA_TYPE_1V1)
        {
            maxPlayersPerTeam = 1;
        }
    }

};

void AddSC_npc_1v1arena()
{
    new configloader_1v1arena();
    new playerscript_1v1arena();
    new npc_1v1arena();
    new team_1v1arena();
}
