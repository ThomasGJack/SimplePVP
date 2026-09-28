//------------------------------------------------------------------------------------------------
// SimplePVP — Logique d'apparition (Spawn Logic du SCR_RespawnSystemComponent du mode de jeu)
// - Connexion : apparition au lobby (L21).
// - Déploiement (ordinateur du lobby) : l'équipement est lu, le personnage du lobby est remplacé par un personnage
//   en ville avec le même équipement, chargeurs pleins (A22, L13). Parti sans rien choisi ni arme : kit au hasard (A09).
// - Mort en ville pendant une manche : écran de mort (D01, D16) ; le joueur choisit même équipement, un des 3
//   derniers (D02), un kit prêt (A07, A08) ou le lobby. Réapparition 5 s après la mort (D10) ; sans choix au bout de
//   90 s, retour au lobby.
// - Fin de manche : tout le monde revient au lobby avec son dernier équipement (D03).
// Un seul chemin pour tout : on supprime le personnage et on en crée un autre au bon endroit.
// Livraisons 1, 2 et 3
//------------------------------------------------------------------------------------------------

[BaseContainerProps(category: "Respawn")]
class SPVP_SpawnLogic : SCR_SpawnLogic
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Personnage de base des joueurs (faction SimplePVP, sans arme)", "et", category: "SimplePVP")]
	protected ResourceName m_sCharacterPrefab;

	[Attribute("SPVP", UIWidgets.EditBox, "Clé de la faction des joueurs", category: "SimplePVP")]
	protected FactionKey m_sFactionKey;

	[Attribute("", UIWidgets.ResourceNamePicker, "Kits prêts : soldats du jeu dont l'équipement sert de kit (A08)", "et", category: "SimplePVP")]
	protected ref array<ResourceName> m_aKitsPrets;

	[Attribute("", UIWidgets.EditBox, "Noms des kits prêts (même ordre)", category: "SimplePVP")]
	protected ref array<string> m_aKitsNoms;

	protected static SPVP_SpawnLogic s_Instance;

	protected static const int ESSAIS_EQUIPEMENT = 3;
	protected static const int RECENTS_MAX = 3;
	protected static const int ATTENTE_MAX_S = 90;

	//------------------------------------------------------------------------------------------------
	static SPVP_SpawnLogic GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnInit(SCR_RespawnSystemComponent owner)
	{
		super.OnInit(owner);
		s_Instance = this;
		SPVP_Kits.Reset();
		GetGame().GetCallqueue().CallLater(PrepareKits, 3000, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Nom d'un kit prêt (réglage du mode de jeu : connu du serveur comme des joueurs)
	string GetPresetName(int index)
	{
		if (!m_aKitsNoms || index < 0 || index >= m_aKitsNoms.Count())
			return "";
		if (!m_aKitsPrets || index >= m_aKitsPrets.Count())
			return "";
		return m_aKitsNoms[index];
	}

	//------------------------------------------------------------------------------------------------
	protected void PrepareKits()
	{
		if (!Replication.IsServer() || !m_aKitsPrets)
			return;

		array<string> noms = {};
		if (m_aKitsNoms)
			noms.Copy(m_aKitsNoms);
		SPVP_Kits.Prepare(m_aKitsPrets, noms);
	}

	//================================================================================================
	// Événements du système d'apparition
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditSuccess_S(int playerId)
	{
		super.OnPlayerAuditSuccess_S(playerId);

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		info.m_eDestination = SPVP_ELieu.LOBBY;
		info.m_bApparitionPrevue = true;

		// Comme le jeu de base : dans Workbench, une image d'attente pour que le personnage
		// « Play from camera » soit disponible (ou non)
		#ifdef WORKBENCH
		GetGame().GetCallqueue().Call(SPVP_InitialSpawn, playerId);
		#else
		SPVP_InitialSpawn(playerId);
		#endif
	}

	//------------------------------------------------------------------------------------------------
	protected void SPVP_InitialSpawn(int playerId)
	{
		if (!GetGame().GetPlayerManager().GetPlayerController(playerId))
			return;

		DoInitialSpawn_S(playerId);

		// Workbench, « Play from camera » : le personnage de la caméra est gardé. Il compte comme au lobby
		// et sera ramené au point d'arrivée par la garde du lobby.
		if (GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId))
		{
			SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
			info.m_bApparitionPrevue = false;
			info.m_eLieu = SPVP_ELieu.LOBBY;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Personnage perdu : mort, ou supprimé exprès (déploiement, fin de manche)
	override protected void OnPlayerEntityLost_S(int playerId)
	{
		super.OnPlayerEntityLost_S(playerId);

		if (!GetGame().GetPlayerManager().IsPlayerConnected(playerId))
			return;

		// Le lieu (m_eLieu) reste celui de la mort jusqu'à la prochaine apparition : le score en a besoin
		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);

		// Suppression voulue : l'apparition est déjà programmée par Transfer()
		if (info.m_bTransfert || info.m_bApparitionPrevue || info.m_bAttenteChoix)
			return;

		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (rounds && rounds.GetEtat() == SPVP_EEtat.EN_COURS && info.m_eLieu == SPVP_ELieu.VILLE)
		{
			// Mort en ville pendant la manche : écran de mort, le joueur choisit (D01)
			info.m_bAttenteChoix = true;
			info.m_fMortA = GetGame().GetWorld().GetWorldTime();
			info.m_iMortJeton++;
			GetGame().GetCallqueue().CallLater(SendDeathScreen, 300, false, playerId, info.m_iMortJeton);
			GetGame().GetCallqueue().CallLater(AutoLobby, ATTENTE_MAX_S * 1000, false, playerId, info.m_iMortJeton);
			return;
		}

		// Autre mort (lobby, pause) : retour au lobby
		info.m_eDestination = SPVP_ELieu.LOBBY;
		info.m_bApparitionPrevue = true;
		GetGame().GetCallqueue().CallLater(DoSpawn_S, 2000, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnPlayerSpawnFailed_S(int playerId)
	{
		super.OnPlayerSpawnFailed_S(playerId);
		SPVP_Log.Warn(string.Format("Apparition échouée pour le joueur %1, nouvel essai dans 2 s", playerId));

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		info.m_bApparitionPrevue = true;
		GetGame().GetCallqueue().CallLater(DoSpawn_S, 2000, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerSpawned_S(int playerId, IEntity entity)
	{
		super.OnPlayerSpawned_S(playerId, entity);

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		info.m_eLieu = info.m_eDestination;
		info.m_bApparitionPrevue = false;
		info.m_bTransfert = false;
		info.m_bAttenteChoix = false;
		info.m_iHorsZone = 0;

		// L'écran de mort se ferme chez le joueur
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (pc)
			pc.SPVP_EndDeath();

		// Apparu en ville alors que la manche vient de finir : retour au lobby
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (info.m_eLieu == SPVP_ELieu.VILLE && (!rounds || !rounds.IsVilleOuverte()))
		{
			GetGame().GetCallqueue().CallLater(Transfer, 100, false, playerId, SPVP_ELieu.LOBBY);
			return;
		}

		// Protection d'apparition en ville (D12-D14)
		if (info.m_eLieu == SPVP_ELieu.VILLE)
		{
			SPVP_Combat.OnSpawnedInCity(entity);
			SPVP_Notify.ToPlayer(playerId, "Protégé", "5 s de protection : elle s'arrête dès que tu vises ou tires.", 5);
		}

		// Équipement : même moment que les loadouts d'arsenal du jeu de base, avec une seconde de marge
		// et une vérification (sur serveur dédié, l'inventaire n'est pas toujours prêt tout de suite)
		if (!info.m_sKit.IsEmpty())
			GetGame().GetCallqueue().CallLater(ApplyKit, 1000, false, playerId, entity, 0);
	}

	//================================================================================================
	// Apparition
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	override protected void DoSpawn_S(int playerId)
	{
		PlayerManager players = GetGame().GetPlayerManager();
		if (!players.GetPlayerController(playerId))
			return;

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);

		// Déjà un personnage vivant (apparition programmée deux fois) : rien à faire
		IEntity current = players.GetPlayerControlledEntity(playerId);
		if (current && !SPVP_Players.IsDead(current) && !info.m_bTransfert)
		{
			info.m_bApparitionPrevue = false;
			return;
		}

		// La manche a pu finir pendant l'attente
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (info.m_eDestination == SPVP_ELieu.VILLE && (!rounds || !rounds.IsVilleOuverte()))
			info.m_eDestination = SPVP_ELieu.LOBBY;

		// Faction « chacun pour soi »
		Faction faction = GetGame().GetFactionManager().GetFactionByKey(m_sFactionKey);
		if (!faction)
		{
			SPVP_Log.Error(string.Format("Faction '%1' introuvable : le gestionnaire de factions SimplePVP est-il dans le monde ?", m_sFactionKey));
			return;
		}
		auto factionComp = GetPlayerFactionComponent_S(playerId);
		if (factionComp && factionComp.GetAffiliatedFaction() != faction)
			factionComp.RequestFaction(faction);

		if (m_sCharacterPrefab.IsEmpty())
		{
			SPVP_Log.Error("Aucun personnage de base réglé dans SPVP_SpawnLogic (mode de jeu > Respawn System > Spawn Logic)");
			return;
		}

		// Position
		vector angles;
		vector position;
		if (info.m_eDestination == SPVP_ELieu.VILLE)
			position = SPVP_SpawnPicker.PickInCity(playerId, angles);
		else
			position = SPVP_Lobby.GetSpawnPosition(angles);

		SCR_FreeSpawnData data = new SCR_FreeSpawnData(m_sCharacterPrefab, position, angles);
		SCR_RespawnComponent respawn = GetPlayerRespawnComponent_S(playerId);
		if (!respawn || !respawn.CanSpawn(data))
		{
			SPVP_Log.Warn(string.Format("Apparition refusée pour %1 (SCR_FreeSpawnHandlerComponent présent sur le mode de jeu ?)", players.GetPlayerName(playerId)));
			OnPlayerSpawnFailed_S(playerId);
			return;
		}

		respawn.RequestSpawn(data);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur : envoie le joueur ailleurs (lobby ou ville) en remplaçant son personnage.
	//! L'équipement est relu sur le personnage s'il part du lobby (celui choisi à l'arsenal).
	void Transfer(int playerId, SPVP_ELieu destination)
	{
		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		IEntity character = SPVP_Players.GetAliveCharacter(playerId);

		if (character && info.m_eLieu == SPVP_ELieu.LOBBY && destination == SPVP_ELieu.VILLE)
			CaptureDeparture(info, character);

		info.m_eDestination = destination;
		info.m_bTransfert = true;
		info.m_bApparitionPrevue = true;
		info.m_bAttenteChoix = false;

		if (character)
			RplComponent.DeleteRplEntity(character, false);

		GetGame().GetCallqueue().CallLater(DoSpawn_S, 300, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Départ du lobby : l'équipement porté devient le kit du joueur ; parti sans rien ni arme la première fois,
	//! il reçoit un kit prêt au hasard (A09) ; sinon il part tel quel, même sans arme (L14)
	protected void CaptureDeparture(SPVP_PlayerInfo info, IEntity character)
	{
		string nom = GetGame().GetPlayerManager().GetPlayerName(info.m_iPlayerId);
		if (info.m_sKit.IsEmpty() && !SPVP_Kit.HasFirearm(character))
		{
			int index = SPVP_Kits.GetRandomIndex();
			if (index >= 0)
			{
				info.m_sKit = SPVP_Kits.GetKit(index);
				info.m_sKitNom = SPVP_Kits.GetNom(index);
				PushRecent(info, info.m_sKit, info.m_sKitNom);
				SPVP_Notify.ToPlayer(info.m_iPlayerId, "Kit au hasard", "Tu n'as rien choisi : tu pars avec le kit " + info.m_sKitNom + ".", 5);
				return;
			}
		}

		string kit = SPVP_Kit.Capture(character);
		if (kit.IsEmpty())
		{
			SPVP_Log.Warn(nom + " : équipement illisible, départ avec le précédent");
			return;
		}

		info.m_sKit = kit;
		info.m_sKitNom = SPVP_Kit.Label(character);
		PushRecent(info, kit, info.m_sKitNom);
	}

	//------------------------------------------------------------------------------------------------
	//! D02 : les 3 derniers équipements, le plus récent en tête, sans doublon
	protected void PushRecent(SPVP_PlayerInfo info, string kit, string nom)
	{
		int index = info.m_aKitsRecents.Find(kit);
		if (index >= 0)
		{
			info.m_aKitsRecents.RemoveOrdered(index);
			info.m_aKitsNoms.RemoveOrdered(index);
		}

		info.m_aKitsRecents.InsertAt(kit, 0);
		info.m_aKitsNoms.InsertAt(nom, 0);
		while (info.m_aKitsRecents.Count() > RECENTS_MAX)
		{
			info.m_aKitsRecents.Remove(info.m_aKitsRecents.Count() - 1);
			info.m_aKitsNoms.Remove(info.m_aKitsNoms.Count() - 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A08 : kit prêt pris à l'ordinateur du lobby (le personnage du lobby est recréé avec ce kit)
	void TakePreset(int playerId, int index)
	{
		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		string kit = SPVP_Kits.GetKit(index);
		if (kit.IsEmpty() || info.m_eLieu != SPVP_ELieu.LOBBY || info.m_bApparitionPrevue)
			return;

		info.m_sKit = kit;
		info.m_sKitNom = SPVP_Kits.GetNom(index);
		Transfer(playerId, SPVP_ELieu.LOBBY);
		SPVP_Notify.ToPlayer(playerId, "Kit prêt", "Kit " + info.m_sKitNom + " : tu peux le modifier à l'arsenal.", 4);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur : départ groupé (R3) — les joueurs en file partent en ville ensemble
	static void DeployQueued()
	{
		SPVP_SpawnLogic logic = GetInstance();
		if (!logic)
			return;

		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		int delai = 0;
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (!info.m_bEnFile)
				continue;

			info.m_bEnFile = false;
			if (info.m_bBot || info.m_eLieu != SPVP_ELieu.LOBBY || !SPVP_Players.GetAliveCharacter(info.m_iPlayerId))
				continue;

			info.m_bApparitionPrevue = true;
			GetGame().GetCallqueue().CallLater(logic.Transfer, delai, false, info.m_iPlayerId, SPVP_ELieu.VILLE);
			delai += 100;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur : fin de manche, tous les joueurs en ville (vivants ou morts) reviennent au lobby (D03)
	static void SendEveryoneToLobby()
	{
		SPVP_SpawnLogic logic = GetInstance();
		if (!logic)
			return;

		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		int delai = 0;
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (info.m_bBot || info.m_eLieu != SPVP_ELieu.VILLE)
				continue;

			// Étalé : un joueur toutes les 100 ms
			GetGame().GetCallqueue().CallLater(logic.Transfer, delai, false, info.m_iPlayerId, SPVP_ELieu.LOBBY);
			delai += 100;
		}
	}

	//================================================================================================
	// Écran de mort (D01, D02, D16, A07)
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! Envoie au joueur mort ce qu'il faut afficher : son tueur et ses choix
	protected void SendDeathScreen(int playerId, int jeton)
	{
		SPVP_PlayerInfo info = SPVP_Players.Find(playerId);
		if (!info || !info.m_bAttenteChoix || info.m_iMortJeton != jeton)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!pc)
			return;

		string contenu = "M|" + info.m_sMortTexte + "\n";
		string nomKit = info.m_sKitNom;
		if (nomKit.IsEmpty())
			nomKit = "celui du lobby";
		contenu += "I|meme|Même équipement : " + nomKit + "\n";

		for (int i = 0; i < info.m_aKitsRecents.Count(); i++)
		{
			if (info.m_aKitsRecents[i] == info.m_sKit)
				continue;
			contenu += "I|r" + i.ToString() + "|Équipement récent : " + info.m_aKitsNoms[i] + "\n";
		}

		if (SPVP_Kits.Count() > 0)
		{
			contenu += "H|Kits prêts\n";
			for (int k = 0; k < SPVP_Kits.Count(); k++)
			{
				if (SPVP_Kits.GetKit(k).IsEmpty())
					continue;
				contenu += "I|p" + k.ToString() + "|Kit " + SPVP_Kits.GetNom(k) + "\n";
			}
		}

		contenu += "H|Autre\n";
		contenu += "I|lobby|Retour au lobby (changer d'équipement à l'arsenal)\n";

		int delai = SPVP_Settings.Get().m_iDelaiReapparition;
		pc.SPVP_ShowDeath(contenu, info.m_vMortCorps, info.m_vMortDirection, delai);
	}

	//------------------------------------------------------------------------------------------------
	//! 90 s sans choix : retour au lobby
	protected void AutoLobby(int playerId, int jeton)
	{
		SPVP_PlayerInfo info = SPVP_Players.Find(playerId);
		if (!info || !info.m_bAttenteChoix || info.m_iMortJeton != jeton)
			return;

		OnDeathChoice(playerId, "lobby");
	}

	//------------------------------------------------------------------------------------------------
	//! Choix du joueur sur l'écran de mort (reçu par son contrôleur)
	void OnDeathChoice(int playerId, string cle)
	{
		SPVP_PlayerInfo info = SPVP_Players.Find(playerId);
		if (!info || !info.m_bAttenteChoix)
			return;

		SPVP_ELieu destination = SPVP_ELieu.VILLE;
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds || rounds.GetEtat() != SPVP_EEtat.EN_COURS)
			destination = SPVP_ELieu.LOBBY;

		if (cle == "lobby")
		{
			destination = SPVP_ELieu.LOBBY;
		}
		else if (cle.StartsWith("r"))
		{
			int recent = cle.Substring(1, cle.Length() - 1).ToInt();
			if (recent >= 0 && recent < info.m_aKitsRecents.Count())
			{
				info.m_sKit = info.m_aKitsRecents[recent];
				info.m_sKitNom = info.m_aKitsNoms[recent];
			}
		}
		else if (cle.StartsWith("p"))
		{
			int preset = cle.Substring(1, cle.Length() - 1).ToInt();
			string kit = SPVP_Kits.GetKit(preset);
			if (!kit.IsEmpty())
			{
				info.m_sKit = kit;
				info.m_sKitNom = SPVP_Kits.GetNom(preset);
				PushRecent(info, kit, info.m_sKitNom);
			}
		}

		info.m_bAttenteChoix = false;
		info.m_bApparitionPrevue = true;
		info.m_eDestination = destination;

		// D10 : jamais avant 5 s après la mort
		float reste = info.m_fMortA + SPVP_Settings.Get().m_iDelaiReapparition * 1000 - GetGame().GetWorld().GetWorldTime();
		int delai = Math.Max(reste, 0);
		GetGame().GetCallqueue().CallLater(DoSpawn_S, delai, false, playerId);
	}

	//================================================================================================
	// Équipement
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void ApplyKit(int playerId, IEntity character, int essai)
	{
		if (!character || character.IsDeleted() || GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId) != character)
			return;

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		if (!SPVP_Kit.Apply(character, info.m_sKit))
			SPVP_Log.Warn(string.Format("%1 : équipement refusé (essai %2)", GetGame().GetPlayerManager().GetPlayerName(playerId), essai + 1));

		GetGame().GetCallqueue().CallLater(VerifyKit, 1000, false, playerId, character, essai);
	}

	//------------------------------------------------------------------------------------------------
	//! L'équipement relu sur le personnage doit être celui voulu ; sinon on réessaie (3 fois en tout)
	protected void VerifyKit(int playerId, IEntity character, int essai)
	{
		if (!character || character.IsDeleted() || GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId) != character)
			return;

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		if (SPVP_Kit.Capture(character) == info.m_sKit)
			return;

		if (essai + 1 < ESSAIS_EQUIPEMENT)
		{
			GetGame().GetCallqueue().CallLater(ApplyKit, 500, false, playerId, character, essai + 1);
			return;
		}

		SPVP_Log.Warn(GetGame().GetPlayerManager().GetPlayerName(playerId) + " : équipement différent de celui voulu après 3 essais");
	}
}
